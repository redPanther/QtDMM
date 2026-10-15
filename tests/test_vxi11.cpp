// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The VXI-11 server over TCP and UDP on localhost, with a small RPC client of its own: the own
// portmapper (on a high port, not 111), create_link, write/read, readstb with MAV, locks that a
// vanished client does not keep, the limits, the abort channel - and the registration with a
// system rpcbind, played by a fake one on a Unix socket in a thread of its own (the server asks
// it synchronously).

#include <QtCore>
#include <QtNetwork>

#include "service/oncrpc.h"
#include "service/vxi11server.h"

static int failed = 0;

static void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAIL:" << what;
    ++failed;
  }
}

static bool waitFor(const std::function<bool()> &cond, int ms = 2000)
{
  QElapsedTimer t;
  t.start();
  while (!cond())
  {
    if (t.elapsed() > ms)
      return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  return true;
}

constexpr quint32 kCore = 0x0607AF;
constexpr quint32 kAbort = 0x0607B0;
constexpr quint32 kPmap = 100000;

static QByteArray callMessage(quint32 xid, quint32 prog, quint32 vers, quint32 proc, const QByteArray &args)
{
  XdrWriter w;
  w.u32(xid);
  w.u32(0);
  w.u32(2);
  w.u32(prog);
  w.u32(vers);
  w.u32(proc);
  w.u32(0);
  w.u32(0);
  w.u32(0);
  w.u32(0);
  w.raw(args);
  return w.data();
}

// The result part of a reply (after accept_stat SUCCESS), or nothing.
static std::optional<QByteArray> resultOf(const QByteArray &reply, quint32 xid)
{
  XdrReader r(reply);
  if (r.u32() != xid || r.u32() != 1 || r.u32() != 0)
    return std::nullopt;
  r.u32();
  r.opaque();
  if (r.u32() != 0 || !r.ok())
    return std::nullopt;
  return reply.mid(24);
}

// A VXI-11 client on one TCP connection, driven by the event loop (the server runs in this thread).
class Client
{
public:
  bool connect(quint16 port)
  {
    m_sock.connectToHost(QHostAddress::LocalHost, port);
    return waitFor([this] { return m_sock.state() == QAbstractSocket::ConnectedState; });
  }
  void abort() { m_sock.abort(); }

  std::optional<QByteArray> call(quint32 prog, quint32 vers, quint32 proc, const QByteArray &args)
  {
    const quint32 xid = ++m_xid;
    const QByteArray msg = callMessage(xid, prog, vers, proc, args);
    XdrWriter frame;
    frame.u32(0x80000000u | quint32(msg.size()));
    frame.raw(msg);
    m_sock.write(frame.data());
    QByteArray in;
    QByteArray reply;
    const bool done = waitFor([&]
    {
      in += m_sock.readAll();
      if (in.size() < 4)
        return false;
      const quint32 len = qFromBigEndian<quint32>(in.constData()) & 0x7FFFFFFFu;
      if (in.size() < 4 + qsizetype(len))
        return false;
      reply = in.mid(4, len);
      return true;
    });
    if (!done)
      return std::nullopt;
    return resultOf(reply, xid);
  }

  // create_link -> (error, lid, abort port, max receive size)
  QList<qint32> createLink(bool lock = false)
  {
    XdrWriter a;
    a.u32(1234);
    a.u32(lock ? 1 : 0);
    a.u32(0);
    a.opaque("inst0");
    const auto r = call(kCore, 1, 10, a.data());
    if (!r)
      return {};
    XdrReader x(*r);
    return { x.i32(), x.i32(), x.i32(), x.i32() };
  }
  qint32 write(qint32 lid, const QByteArray &data, bool end = true)
  {
    XdrWriter a;
    a.i32(lid);
    a.u32(1000);
    a.u32(0);
    a.u32(end ? 8 : 0);
    a.opaque(data);
    const auto r = call(kCore, 1, 11, a.data());
    return r ? XdrReader(*r).i32() : -1;
  }
  // device_read -> error, reason, data
  std::tuple<qint32, quint32, QByteArray> read(qint32 lid, quint32 size = 4096)
  {
    XdrWriter a;
    a.i32(lid);
    a.u32(size);
    a.u32(1000);
    a.u32(0);
    a.u32(0);
    a.u32(0);
    const auto r = call(kCore, 1, 12, a.data());
    if (!r)
      return { -1, 0, {} };
    XdrReader x(*r);
    const qint32 err = x.i32();
    const quint32 reason = x.u32();
    return { err, reason, x.opaque() };
  }
  std::pair<qint32, quint32> readStb(qint32 lid)
  {
    const auto r = call(kCore, 1, 13, generic(lid));
    if (!r)
      return { -1, 0 };
    XdrReader x(*r);
    const qint32 err = x.i32();
    return { err, x.u32() };
  }
  qint32 simple(quint32 proc, qint32 lid)
  {
    XdrWriter a;
    a.i32(lid);
    if (proc == 18)   // device_lock: lid, flags, lock_timeout
    {
      a.u32(0);
      a.u32(0);
    }
    const auto r = call(kCore, 1, proc, proc == 15 ? generic(lid) : a.data());
    return r ? XdrReader(*r).i32() : -1;
  }

private:
  static QByteArray generic(qint32 lid)
  {
    XdrWriter a;
    a.i32(lid);
    a.u32(0);
    a.u32(0);
    a.u32(1000);
    return a.data();
  }
  QTcpSocket m_sock;
  quint32 m_xid = 100;
};

static quint32 getPortTcp(quint16 pmapPort, quint32 prog)
{
  Client c;
  if (!c.connect(pmapPort))
    return 0;
  XdrWriter a;
  a.u32(prog);
  a.u32(1);
  a.u32(6);
  a.u32(0);
  const auto r = c.call(kPmap, 2, 3, a.data());
  return r ? XdrReader(*r).u32() : 0;
}

static quint32 getPortUdp(quint16 pmapPort, quint32 prog)
{
  QUdpSocket u;
  XdrWriter a;
  a.u32(prog);
  a.u32(1);
  a.u32(6);
  a.u32(0);
  u.writeDatagram(callMessage(77, kPmap, 2, 3, a.data()), QHostAddress::LocalHost, pmapPort);
  QByteArray reply;
  waitFor([&]
  {
    if (!u.hasPendingDatagrams())
      return false;
    reply = u.receiveDatagram().data();
    return true;
  });
  const auto r = resultOf(reply, 77);
  return r ? XdrReader(*r).u32() : 0;
}

// --- a fake system rpcbind: portmapper v2 with SET/UNSET on a Unix socket, in its own thread ---

class FakeRpcbind : public RpcService
{
public:
  quint32 program() const override { return kPmap; }
  quint32 version() const override { return 2; }
  RpcStatus call(quint32 proc, XdrReader &a, XdrWriter &r, quint64) override
  {
    const quint32 prog = a.u32();
    const quint32 vers = a.u32();
    a.u32();
    const quint32 port = a.u32();
    if (!a.ok())
      return RpcStatus::GarbageArgs;
    QMutexLocker lock(&mutex);
    const quint64 key = (quint64(prog) << 32) | vers;
    switch (proc)
    {
      case 1:   // SET: refused when taken, like rpcbind
        r.u32(entries.contains(key) ? 0 : 1);
        if (!entries.contains(key))
          entries.insert(key, quint16(port));
        return RpcStatus::Success;
      case 2:   // UNSET
        r.u32(entries.remove(key) ? 1 : 0);
        return RpcStatus::Success;
      case 3:   // GETPORT
        r.u32(entries.value(key, 0));
        return RpcStatus::Success;
      default:
        return RpcStatus::ProcUnavail;
    }
  }
  quint16 port(quint32 prog)
  {
    QMutexLocker lock(&mutex);
    return entries.value((quint64(prog) << 32) | 1, 0);
  }
  void put(quint32 prog, quint16 port)
  {
    QMutexLocker lock(&mutex);
    entries.insert((quint64(prog) << 32) | 1, port);
  }

  QMutex mutex;
  QHash<quint64, quint16> entries;
};

class FakeRpcbindServer : public QObject
{
public:
  FakeRpcbindServer(const QString &path, FakeRpcbind *service) : m_path(path)
  {
    m_disp.addService(service);
  }
  void start()
  {
    m_server = new QLocalServer(this);
    m_server->listen(m_path);
    QObject::connect(m_server, &QLocalServer::newConnection, this, [this]
    {
      while (QLocalSocket *s = m_server->nextPendingConnection())
      {
        auto in = std::make_shared<QByteArray>();
        QObject::connect(s, &QLocalSocket::readyRead, s, [this, s, in]
        {
          *in += s->readAll();
          while (in->size() >= 4)
          {
            const quint32 len = qFromBigEndian<quint32>(in->constData()) & 0x7FFFFFFFu;
            if (in->size() < 4 + qsizetype(len))
              return;
            const QByteArray reply = m_disp.handle(in->mid(4, len));
            in->remove(0, 4 + len);
            XdrWriter w;
            w.u32(0x80000000u | quint32(reply.size()));
            w.raw(reply);
            s->write(w.data());
          }
        });
        QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
      }
    });
  }

private:
  QString m_path;
  RpcDispatcher m_disp;
  QLocalServer *m_server = nullptr;
};

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);

  ScpiEndpoint ep;
  int cleared = 0;
  ep.process = [](const QByteArray &line) -> QByteArray
  {
    if (line.trimmed() == "*IDN?")
      return "QtDMM,Test,0,1\n";
    if (line.trimmed() == "BIG?")
      return QByteArray(10000, 'x') + '\n';
    return {};
  };
  ep.statusByte = [] { return quint8(0); };
  ep.deviceClear = [&cleared] { ++cleared; };

  // --- 1. own portmapper on a high port; no system rpcbind to be found ---
  Vxi11Server srv(ep);
  srv.setRpcbind({}, 1);   // nothing answers on port 1
  int links = 0;
  QObject::connect(&srv, &Vxi11Server::linksChanged, [&links](int n) { links = n; });
  check(srv.start(QHostAddress::LocalHost, Vxi11Server::RpcbindMode::Auto, 0, 0, 0), "start, own portmapper: " + srv.error());
  check(!srv.usesSystemRpcbind() && srv.portmapperPort() != 0, "an own portmapper");
  check(srv.address() == QHostAddress(QHostAddress::LocalHost), "localhost only");
  const quint16 pmap = srv.portmapperPort();
  check(getPortTcp(pmap, kCore) == srv.corePort(), "GETPORT over TCP gives the core port");
  check(getPortUdp(pmap, kCore) == srv.corePort(), "GETPORT over UDP (the broadcast way) too");
  check(getPortTcp(pmap, 0x12345) == 0, "an unknown program: 0");

  // --- 2. link, write, read, status byte ---
  Client a;
  check(a.connect(srv.corePort()), "core channel connects");
  const QList<qint32> la = a.createLink();
  check(la.size() == 4 && la[0] == 0 && la[1] > 0 && la[2] > 0 && la[3] == 65536, "create_link");
  const qint32 lidA = la.value(1);
  check(links == 1 && srv.linkCount() == 1, "one link counted");
  check(a.write(lidA, "*IDN?\n") == 0, "device_write *IDN?");
  check((a.readStb(lidA).second & 0x10) != 0, "readstb: MAV while a response waits");
  auto [err, reason, data] = a.read(lidA);
  check(err == 0 && (reason & 4) && data == "QtDMM,Test,0,1\n", "device_read the answer: " + QString::fromLatin1(data));
  check((a.readStb(lidA).second & 0x10) == 0, "readstb: no MAV after reading");
  check(std::get<0>(a.read(lidA)) == 15, "read without a response: timeout");
  // a message in two blocks, a big answer in pieces
  check(a.write(lidA, "BI", false) == 0 && a.write(lidA, "G?\n") == 0, "a message in two blocks");
  auto [e1, r1, d1] = a.read(lidA, 6000);
  auto [e2, r2, d2] = a.read(lidA, 6000);
  check(e1 == 0 && r1 == 1 && d1.size() == 6000 && e2 == 0 && (r2 & 4) && d2.size() == 4001, "a big answer in pieces");
  check(a.simple(15, lidA) == 0 && cleared == 1, "device_clear reaches the interpreter");

  // --- 3. a lock does not outlive its client ---
  Client b;
  check(b.connect(srv.corePort()), "second client");
  const qint32 lidB = b.createLink().value(1);
  check(a.simple(18, lidA) == 0, "A locks");
  check(b.write(lidB, "*IDN?\n") == 11, "B is locked out");
  // lids are easy to guess: B cannot use, unlock or destroy A's link (R11-04)
  check(b.write(lidA, "*IDN?\n") == 4 && std::get<0>(b.read(lidA)) == 4, "B cannot write or read on A's lid");
  check(b.simple(19, lidA) == 4 && b.simple(23, lidA) == 4, "B cannot unlock or destroy A's link");
  check(b.simple(18, lidA) == 4 && b.simple(15, lidA) == 4, "B cannot lock or clear on A's lid");
  check(srv.linkCount() == 2 && b.write(lidB, "*IDN?\n") == 11, "A's link and lock are still there");
  check(a.write(lidA, "*IDN?\n") == 0 && std::get<2>(a.read(lidA)) == "QtDMM,Test,0,1\n", "and A works on");
  a.abort();   // no destroy_link, no unlock: the client is gone
  check(waitFor([&] { return srv.linkCount() == 1; }), "A's link went with its connection");
  check(b.write(lidB, "*IDN?\n") == 0, "B writes again: the lock went too");
  check(std::get<2>(b.read(lidB)) == "QtDMM,Test,0,1\n", "and reads");

  // --- 4. limits ---
  {
    Client c;
    c.connect(srv.corePort());
    int ok = 0;
    qint32 last = 0;
    for (int i = 0; i < Vxi11Core::kMaxLinks + 1; ++i)
    {
      const QList<qint32> l = c.createLink();
      if (l.value(0) == 0)
        ++ok;
      last = l.value(0, -1);
    }
    check(ok == Vxi11Core::kMaxLinks - 1 && last == 9, QString("at most %1 links, then 9 (got %2 ok, last %3)")
                                                      .arg(Vxi11Core::kMaxLinks).arg(ok).arg(last));
  }
  check(waitFor([&] { return srv.linkCount() == 1; }), "the extra links went with their connection");
  {
    const QByteArray block(60000, 'a');
    qint32 e = 0;
    for (int i = 0; i < 20 && e == 0; ++i)
      e = b.write(lidB, block, false);
    check(e == 17, QString("a message over 1 MiB: I/O error (%1)").arg(e));
    check(b.write(lidB, "*IDN?\n") == 17, "the rest of that message up to its END is dropped too");
    // device_clear and device_abort end the dropping: the next command runs (R11-09)
    for (int i = 0; i < 20 && b.write(lidB, block, false) == 0; ++i)
      ;
    check(b.simple(15, lidB) == 0 && b.write(lidB, "*IDN?\n") == 0 && std::get<2>(b.read(lidB)) == "QtDMM,Test,0,1\n",
          "after a message too big, device_clear: the next command answers");
    for (int i = 0; i < 20 && b.write(lidB, block, false) == 0; ++i)
      ;
    Client ab;
    XdrWriter args;
    args.i32(lidB);
    check(ab.connect(quint16(la.value(2))) && ab.call(kAbort, 1, 1, args.data()), "device_abort on the abort channel");
    check(b.write(lidB, "*IDN?\n") == 0 && std::get<2>(b.read(lidB)) == "QtDMM,Test,0,1\n",
          "after a message too big, device_abort: the next command answers");
    check(b.write(lidB, "*IDN?\n") == 0 && std::get<2>(b.read(lidB)) == "QtDMM,Test,0,1\n", "the link works on");
  }

  // --- 5. abort channel ---
  {
    Client ab;
    check(ab.connect(quint16(la.value(2))), "abort channel connects");
    XdrWriter args;
    args.i32(lidB);
    const auto r = ab.call(kAbort, 1, 1, args.data());
    check(r && XdrReader(*r).i32() == 0, "device_abort");
  }

  // --- 6. a second server cannot have the same portmapper port ---
  {
    Vxi11Server other(ep);
    other.setRpcbind({}, 1);
    check(!other.start(QHostAddress::LocalHost, Vxi11Server::RpcbindMode::Own, 0, 0, pmap), "second own portmapper refused");
    check(other.error().contains(QString::number(pmap)), "the error names the port: " + other.error());
    check(!other.start(QHostAddress::LocalHost, Vxi11Server::RpcbindMode::System), "system mode without rpcbind refused");
    check(!other.error().isEmpty(), "and says why: " + other.error());
  }
  srv.stop();
  check(links == 0, "stop: no links");

  // --- 7. the system rpcbind (a fake one on a Unix socket) ---
  // rpcbind and its Unix socket are Unix only. On Windows QLocalServer is a
  // named pipe whose instances the quick reconnects below can find busy, and
  // QtDMM there always runs its own portmapper.
#ifndef Q_OS_WIN
  {
    QTemporaryDir dir;
    const QString path = dir.filePath("rpcbind.sock");
    FakeRpcbind fake;
    QThread thread;
    auto *server = new FakeRpcbindServer(path, &fake);
    server->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, server, &QObject::deleteLater);
    thread.start();
    QMetaObject::invokeMethod(server, [server] { server->start(); }, Qt::BlockingQueuedConnection);

    Vxi11Server sys(ep);
    sys.setRpcbind({ path }, 1);
    check(sys.start(QHostAddress::LocalHost, Vxi11Server::RpcbindMode::Auto), "start with the system rpcbind: " + sys.error());
    check(sys.usesSystemRpcbind(), "uses it");
    check(fake.port(kCore) == sys.corePort() && fake.port(kAbort) != 0, "core and abort registered");
    sys.stop();
    check(fake.port(kCore) == 0 && fake.port(kAbort) == 0, "stop: unregistered");

    // an entry left behind by a crashed run: taken over
    fake.put(kCore, 1);
    check(sys.start(QHostAddress::LocalHost, Vxi11Server::RpcbindMode::System), "stale entry replaced: " + sys.error());
    check(fake.port(kCore) == sys.corePort(), "now ours");
    sys.stop();

    // another live VXI-11 server: refused, nothing of ours left
    QTcpServer live;
    live.listen(QHostAddress::LocalHost);
    fake.put(kCore, live.serverPort());
    check(!sys.start(QHostAddress::LocalHost, Vxi11Server::RpcbindMode::System), "another live server: refused");
    check(sys.error().contains(QString::number(live.serverPort())), "the error names its port: " + sys.error());
    check(fake.port(kAbort) == 0, "no half registration left");

    thread.quit();
    thread.wait();
  }
#endif

  check(Vxi11Server::modeFromName("own") == Vxi11Server::RpcbindMode::Own
        && Vxi11Server::modeName(Vxi11Server::RpcbindMode::System) == "system", "mode names");
  bool ok = true;
  Vxi11Server::modeFromName("nonsense", &ok);
  check(!ok, "an unknown mode name is reported");

  if (failed)
  {
    qWarning() << failed << "VXI-11 check(s) failed";
    return 1;
  }
  qInfo() << "All VXI-11 tests passed.";
  return 0;
}
