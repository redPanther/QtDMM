// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// HislipServer in front of the real ScpiServer interpreter, over TCP on
// localhost: a session's two channels, queries in one and in several
// pieces, status byte, device clear, and what a broken or greedy client
// gets (wrong order, too large, too many sessions).

#include <QtCore>
#include <QtNetwork>

#include "service/hislipserver.h"
#include "service/scpiserver.h"
#include "testutil.h"

// message types used here (IVI-6.1)
enum : quint8
{
  Initialize = 0, InitializeResponse = 1, FatalError = 2, Error = 3, AsyncLock = 4, AsyncLockResponse = 5,
  Data = 6, DataEnd = 7, DeviceClearComplete = 8, DeviceClearAcknowledge = 9, AsyncMaximumMessageSize = 15,
  AsyncMaximumMessageSizeResponse = 16, AsyncInitialize = 17, AsyncInitializeResponse = 18,
  AsyncDeviceClear = 19, AsyncStatusQuery = 21, AsyncStatusResponse = 22, AsyncDeviceClearAcknowledge = 23
};

struct Msg
{
  bool    ok = false;
  quint8  type = 0xff;
  quint8  ctrl = 0;
  quint32 param = 0;
  QByteArray payload;
};

static QByteArray frame(quint8 type, quint8 ctrl, quint32 param, const QByteArray &payload = {})
{
  QByteArray m("HS");
  m.append(char(type));
  m.append(char(ctrl));
  char b[8];
  qToBigEndian(param, b);
  m.append(b, 4);
  qToBigEndian(quint64(payload.size()), b);
  m.append(b, 8);
  return m + payload;
}

// One channel of a client.
struct Channel
{
  QTcpSocket sock;
  QByteArray in;

  bool open(quint16 port)
  {
    sock.connectToHost(QHostAddress::LocalHost, port);
    return waitFor([this] { return sock.state() == QAbstractSocket::ConnectedState; });
  }
  void send(quint8 type, quint8 ctrl, quint32 param, const QByteArray &payload = {})
  {
    sock.write(frame(type, ctrl, param, payload));
    sock.flush();
  }
  Msg receive(int ms = 2000)
  {
    Msg m;
    auto complete = [this]
    {
      in += sock.readAll();
      return in.size() >= 16 && quint64(in.size()) >= 16 + qFromBigEndian<quint64>(in.constData() + 8);
    };
    if (!waitFor(complete, ms))
      return m;
    m.ok = in.startsWith("HS");
    m.type = quint8(in[2]);
    m.ctrl = quint8(in[3]);
    m.param = qFromBigEndian<quint32>(in.constData() + 4);
    const qsizetype len = qsizetype(qFromBigEndian<quint64>(in.constData() + 8));
    m.payload = in.mid(16, len);
    in.remove(0, 16 + len);
    return m;
  }
  bool closed(int ms = 2000)
  {
    return waitFor([this] { return sock.state() == QAbstractSocket::UnconnectedState; }, ms);
  }
};

// A session: Initialize on the synchronous channel, AsyncInitialize with its id on the second.
struct Session
{
  Channel sync, async;
  quint16 id = 0;
  quint32 msgId = 0xffffff00;   // the client's first MessageID, +2 per message

  bool open(quint16 port)
  {
    if (!sync.open(port))
      return false;
    sync.send(Initialize, 0, 0x01000000 | 0x5444, "hislip0");   // version 1.0, vendor "TD"
    const Msg r = sync.receive();
    if (r.type != InitializeResponse || (r.param >> 16) != 0x0100)
      return false;
    id = quint16(r.param & 0xffff);
    if (!async.open(port))
      return false;
    async.send(AsyncInitialize, 0, id);
    return async.receive().type == AsyncInitializeResponse;
  }
  // a program message in one DataEnd; the answers, one per response message
  QStringList query(const QByteArray &message, int answers = 1)
  {
    const quint32 id = msgId;
    msgId += 2;
    sync.send(DataEnd, 0, id, message);
    QStringList out;
    QByteArray part;
    while (out.size() < answers)
    {
      const Msg r = sync.receive();
      if (!r.ok || (r.type != Data && r.type != DataEnd) || r.param != id)
        break;
      part += r.payload;
      if (r.type == DataEnd)
      {
        out << QString::fromUtf8(part).trimmed();
        part.clear();
      }
    }
    return out;
  }
};

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  ScpiServer scpi;
  ScpiDevice scpiDev;
  scpi.addDevice(&scpiDev);
  scpiDev.setModel("UNI-T UT61E");
  HislipServer server(scpi.endpoint());
  int sessions = -1;
  QObject::connect(&server, &HislipServer::sessionsChanged, [&](int n) { sessions = n; });

  // --- 1. listens where it is told, the next port when one is taken ---
  QTcpServer blocker;
  check(blocker.listen(QHostAddress::LocalHost, 0), "a port to block");
  const bool started = server.start(QHostAddress::LocalHost, blocker.serverPort(), 3);
  check(started, "start: " + server.error());   // error() after start()
  check(server.port() == blocker.serverPort() + 1 || server.port() > blocker.serverPort(),
        QString("next free port: %1 after %2").arg(server.port()).arg(blocker.serverPort()));
  check(server.address() == QHostAddress::LocalHost, "localhost only");
  const quint16 port = server.port();

  // --- 2. a session, *IDN? in one and in several pieces ---
  {
    Session s;
    check(s.open(port), "session opened");
    check(waitFor([&] { return sessions == 1 && server.sessionCount() == 1; }), "one session counted");
    const QStringList idn = s.query("*IDN?\n");
    check(idn == QStringList{ QString("QtDMM,UNI-T UT61E,0,%1").arg(APP_VERSION) }, "*IDN? " + idn.join('|'));

    const quint32 id = s.msgId;
    s.msgId += 2;
    s.sync.send(Data, 0, id, "*ID");
    s.sync.send(Data, 0, id, "N");
    s.sync.send(DataEnd, 0, id, "?");
    const Msg r = s.sync.receive();
    check(r.type == DataEnd && r.param == id && r.payload.startsWith("QtDMM,"), "*IDN? in three pieces");

    check(s.query("*IDN?;SYST:ERR?\n") == QStringList{ QString("QtDMM,UNI-T UT61E,0,%1;0,\"No error\"").arg(APP_VERSION) },
          "two queries, one line: one answer");
    check(s.query("SYST:ERR?\nSYST:ERR?\n", 2).size() == 2, "two lines: two answers");

    // maximum message size: the server says 1 MB
    char b[8];
    qToBigEndian(quint64(4096), b);
    s.async.send(AsyncMaximumMessageSize, 0, 0, QByteArray(b, 8));
    const Msg mm = s.async.receive();
    check(mm.type == AsyncMaximumMessageSizeResponse && mm.payload.size() == 8
          && qFromBigEndian<quint64>(mm.payload.constData()) == HislipServer::kMaxMessage, "maximum message size");

    // the screen dump is larger than the client takes in one message: in pieces, the same id
    scpi.setScreenshotSource([](const QByteArray &) { return QByteArray(10000, 'x'); });
    const QStringList dump = s.query("HCOP:SDUM:DATA?\n");
    check(dump.size() == 1 && dump.first().size() > 10000, QString("screen dump in pieces: %1 bytes")
          .arg(dump.isEmpty() ? 0 : dump.first().size()));

    // status byte: an error in the queue is bit 2
    s.async.send(AsyncStatusQuery, 0, s.msgId);
    check(s.async.receive().ctrl == 0, "status byte 0");
    check(s.query("FOO?\n*IDN?\n").size() == 1, "unknown header answers nothing");
    s.async.send(AsyncStatusQuery, 0, s.msgId);
    check(s.async.receive().ctrl == 4, "status byte 4 with an error queued");
    check(s.query("*STB?\n") == QStringList{ "4" }, "*STB? 4");
    s.query("SYST:ERR?\n");

    // device clear: both halves of the handshake
    s.async.send(AsyncDeviceClear, 0, 0);
    check(s.async.receive().type == AsyncDeviceClearAcknowledge, "device clear acknowledged (async)");
    s.sync.send(DeviceClearComplete, 0, 0);
    check(s.sync.receive().type == DeviceClearAcknowledge, "device clear complete (sync)");
    check(s.query("*IDN?\n").size() == 1, "answers after device clear");

    // no locks: a request is not granted
    s.async.send(AsyncLock, 1, 0, "");
    const Msg lock = s.async.receive();
    check(lock.type == AsyncLockResponse && lock.ctrl == 0, "lock not granted");

    // the synchronous channel closes: the whole session is over
    s.sync.sock.disconnectFromHost();
    check(s.async.closed(), "async channel closed with the session");
    check(waitFor([&] { return sessions == 0 && server.sessionCount() == 0; }), "no session left");
  }

  // --- 3. wrong order and a broken header ---
  {
    Channel c;
    check(c.open(port), "channel");
    c.send(DataEnd, 0, 0xffffff00, "*IDN?");
    const Msg r = c.receive();
    check(r.type == FatalError && r.ctrl == 2, "data before Initialize: FatalError 2");
    check(c.closed(), "... and closed");

    Channel h;
    check(h.open(port), "channel");
    h.sock.write("XX0123456789ABCDEF");
    check(h.receive().type == FatalError, "poorly formed header: FatalError");
    check(h.closed(), "... and closed");

    Session half;
    check(half.sync.open(port), "sync channel");
    half.sync.send(Initialize, 0, 0x01000000, "hislip0");
    check(half.sync.receive().type == InitializeResponse, "initialized");
    half.sync.send(DataEnd, 0, 0xffffff00, "*IDN?");
    check(half.sync.receive().ctrl == 2, "data without the async channel: FatalError 2");
  }

  // --- 4. too large: in one message and in many pieces ---
  {
    Session s;
    check(s.open(port), "session for the size test");
    s.sync.send(DataEnd, 0, s.msgId, QByteArray(int(HislipServer::kMaxMessage) + 1, 'A'));
    const Msg r = s.sync.receive(5000);
    check(r.type == FatalError, "one message over 1 MB: FatalError");
    check(s.sync.closed(), "... and closed");

    Session p;
    check(p.open(port), "session for the pieces");
    const QByteArray piece(256 * 1024, 'A');
    for (int i = 0; i < 4; ++i)
      p.sync.send(Data, 0, p.msgId, piece);
    p.sync.send(Data, 0, p.msgId, "A");
    check(p.sync.receive(5000).type == FatalError, "pieces over 1 MB: FatalError");
    check(p.sync.closed(), "... and closed");
  }
  check(waitFor([&] { return server.sessionCount() == 0; }), "sessions gone after the size tests");

  // --- 5. too many sessions ---
  {
    QList<Session *> open;
    for (int i = 0; i < HislipServer::kMaxSessions; ++i)
    {
      auto *s = new Session;
      check(s->open(port), QString("session %1").arg(i + 1));
      open << s;
    }
    Channel extra;
    check(extra.open(port), "one more channel");
    extra.send(Initialize, 0, 0x01000000, "hislip0");
    const Msg r = extra.receive();
    check(r.type == FatalError && r.ctrl == 4, "session 17: FatalError 4 (too many clients)");
    qDeleteAll(open);
    check(waitFor([&] { return server.sessionCount() == 0; }), "all closed");
    Session again;
    check(again.open(port), "a new session after the others left");
    check(again.query("*IDN?\n").size() == 1, "... and it answers");
  }

  // --- 5b. a command that spins an event loop (a dialog) while the client goes ---
  // INPut ON may show a modal message box in the UI; the server must not
  // write to the socket or session afterwards (review R10-02).
  {
    auto *s = new Session;
    check(s->open(port), "session before the dialog");
    std::vector<Session *> others;
    auto conn = QObject::connect(&scpiDev, &ScpiDevice::connectRequested, [&](bool)
    {
      s->sync.sock.abort();
      s->async.sock.abort();
      // more sessions meanwhile: m_sessions may grow and move its entries
      for (int i = 0; i < 8; ++i)
      {
        others.push_back(new Session);
        others.back()->open(port);
      }
      QEventLoop loop;
      QTimer::singleShot(300, &loop, &QEventLoop::quit);
      loop.exec();
    });
    s->sync.send(DataEnd, 0, s->msgId, "INP ON\n*IDN?\n");
    check(waitFor([&] { return !others.empty(); }), "the command ran");
    QObject::disconnect(conn);
    delete s;
    // a busy runner (macOS on CI) needs more than the default for the sockets to close
    check(waitFor([&] { return server.sessionCount() == int(others.size()); }, 10000), "the gone client's session removed");
    check(others.back()->query("*IDN?\n").size() == 1, "the others still answer");
    qDeleteAll(others);
    check(waitFor([&] { return server.sessionCount() == 0; }, 10000), "all closed after the dialog");

    // the same over SCPI raw
    const bool rawStarted = scpi.start(QHostAddress::LocalHost, 0);
    check(rawStarted, "raw: " + scpi.errorString());
    auto *raw = new QTcpSocket;
    raw->connectToHost(QHostAddress::LocalHost, scpi.port());
    check(waitFor([&] { return raw->state() == QAbstractSocket::ConnectedState; }), "raw connected");
    bool ran = false;
    conn = QObject::connect(&scpiDev, &ScpiDevice::connectRequested, [&](bool)
    {
      raw->abort();
      QEventLoop loop;
      QTimer::singleShot(300, &loop, &QEventLoop::quit);
      loop.exec();
      ran = true;
    });
    raw->write("INP ON\n*IDN?\n");
    raw->flush();
    check(waitFor([&] { return ran; }), "raw: the command ran");
    QObject::disconnect(conn);
    delete raw;
    QTcpSocket next;
    next.connectToHost(QHostAddress::LocalHost, scpi.port());
    check(waitFor([&] { return next.state() == QAbstractSocket::ConnectedState; }), "raw: next client");
    next.write("*IDN?\n");
    check(waitFor([&] { return next.canReadLine(); }), "raw: still answers");
    scpi.stop();
  }

  // --- 5c. silent connections and half sessions are closed after a while ---
  {
    std::vector<Channel *> silent;
    // every place taken (two per session and four on their way in), the
    // last by a session without its asynchronous channel
    for (int i = 0; i < 2 * HislipServer::kMaxSessions + 4 - 1; ++i)
    {
      silent.push_back(new Channel);
      silent.back()->open(port);
    }
    Channel half;
    check(half.open(port), "a synchronous channel alone");
    half.send(Initialize, 0, 0x01000000, "hislip0");
    check(half.receive().type == InitializeResponse, "the half session initialized");
    Session full;
    check(!full.open(port), "no session while the silent connections block the server");
    check(half.closed(HislipServer::kSetupTimeoutMs + 2000), "the half session closed after the timeout");
    bool allClosed = true;
    for (Channel *c : silent)
      allClosed = allClosed && c->closed(100);
    check(allClosed, "the silent connections closed after the timeout");
    qDeleteAll(silent);
    check(waitFor([&] { return server.sessionCount() == 0; }), "no session left");
    Session again;
    check(again.open(port), "a session after the timeout");
    check(again.query("*IDN?\n").size() == 1, "... and it answers");
  }

  // --- 6. stop ends the sessions ---
  {
    Session s;
    check(s.open(port), "session before stop");
    server.stop();
    check(!server.isListening() && server.sessionCount() == 0, "stopped");
    check(s.sync.closed() && s.async.closed(), "clients disconnected");
  }

  if (failed)
  {
    qWarning() << failed << "HiSLIP check(s) failed";
    return 1;
  }
  qInfo() << "All HiSLIP tests passed.";
  return 0;
}
