// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "service/rpc.h"

#include <QNetworkDatagram>
#include <QTcpSocket>

// ---------------------------------------------------------------- dispatcher

QByteArray RpcDispatcher::handle(const QByteArray &callMsg) const
{
  XdrReader r(callMsg);
  const quint32 xid = r.u32();
  const quint32 msgType = r.u32();
  if (!r.ok() || msgType != 0) // 0 = CALL
    return {};
  const quint32 rpcVers = r.u32();
  const quint32 prog = r.u32();
  const quint32 vers = r.u32();
  const quint32 proc = r.u32();
  r.u32(); // credentials: flavor
  r.opaque(); //             body
  r.u32(); // verifier:    flavor
  r.opaque(); //             body
  if (!r.ok())
    return {};

  XdrWriter w;
  w.u32(xid);
  w.u32(1); // REPLY

  if (rpcVers != 2) // MSG_DENIED / RPC_MISMATCH
  {
    w.u32(1);
    w.u32(0);
    w.u32(2);
    w.u32(2);
    return w.data();
  }

  w.u32(0); // MSG_ACCEPTED
  w.u32(0); // verifier: AUTH_NONE, length 0
  w.u32(0);

  RpcService *svc = nullptr;
  bool progKnown = false;
  quint32 lo = ~0u;
  quint32 hi = 0;
  for (RpcService *s : m_services)
  {
    if (s->program() != prog)
      continue;
    progKnown = true;
    lo = qMin(lo, s->version());
    hi = qMax(hi, s->version());
    if (s->version() == vers)
      svc = s;
  }
  if (!progKnown) // PROG_UNAVAIL
  {
    w.u32(1);
    return w.data();
  }
  if (!svc) // PROG_MISMATCH (lets libtirpc fall back from rpcbind v4/v3 to v2)
  {
    w.u32(2);
    w.u32(lo);
    w.u32(hi);
    return w.data();
  }

  if (proc == 0) // NULL procedure
  {
    w.u32(0);
    return w.data();
  }

  XdrWriter res;
  const RpcStatus st = svc->call(proc, r, res);
  switch (st)
  {
    case RpcStatus::NoReply:
      return {};
    case RpcStatus::ProcUnavail:
      w.u32(3);
      return w.data();
    case RpcStatus::GarbageArgs:
      w.u32(4);
      return w.data();
    case RpcStatus::Success:
      break;
  }
  if (!r.ok()) // arguments were too short
  {
    w.u32(4);
    return w.data();
  }

  w.u32(0); // SUCCESS
  w.raw(res.data());
  return w.data();
}

// ---------------------------------------------------------------- portmapper

RpcStatus Portmapper::call(quint32 proc, XdrReader &a, XdrWriter &r)
{
  switch (proc)
  {
    case 3: // PMAPPROC_GETPORT: prog, vers, prot, port
    {
      const quint32 prog = a.u32();
      a.u32();
      const quint32 prot = a.u32(); // 6 = TCP, 17 = UDP
      a.u32();
      if (!a.ok())
        return RpcStatus::GarbageArgs;
      r.u32(prot == 6 ? m_ports.value(prog, 0) : 0);
      return RpcStatus::Success;
    }
    case 5: // PMAPPROC_CALLIT: only the NULL procedure of known programs (clnt_broadcast)
    {
      const quint32 prog = a.u32();
      a.u32();
      const quint32 p = a.u32();
      a.opaque();
      if (!a.ok())
        return RpcStatus::GarbageArgs;
      if (p != 0 || !m_ports.contains(prog))
        return RpcStatus::NoReply;
      r.u32(m_ports.value(prog));
      r.opaque({});
      return RpcStatus::Success;
    }
    default:
      return RpcStatus::ProcUnavail;
  }
}

// ---------------------------------------------------------------- TCP

namespace
{
constexpr qsizetype kMaxFragment = 4 * 1024 * 1024;

// One per socket: unpacks record marking (RFC 5531 section 11), packs the replies.
class RpcConnection : public QObject
{
public:
  RpcConnection(QTcpSocket *socket, const RpcDispatcher *dispatcher)
    : QObject(socket), m_sock(socket), m_dispatcher(dispatcher)
  {
    connect(socket, &QTcpSocket::readyRead, this, [this]() { onReadyRead(); });
  }

private:
  void onReadyRead()
  {
    m_in += m_sock->readAll();
    for (;;)
    {
      if (m_in.size() < 4)
        return;
      const quint32 hdr = qFromBigEndian<quint32>(m_in.constData());
      const bool last = (hdr & 0x80000000u) != 0;
      const qsizetype len = hdr & 0x7FFFFFFFu;
      if (len > kMaxFragment)
      {
        m_sock->disconnectFromHost();
        return;
      }
      if (m_in.size() < 4 + len)
        return;
      m_msg += m_in.mid(4, len);
      m_in.remove(0, 4 + len);
      if (!last)
        continue;

      const QByteArray reply = m_dispatcher->handle(m_msg);
      m_msg.clear();
      if (reply.isEmpty())
        continue;
      XdrWriter w;
      w.u32(0x80000000u | quint32(reply.size()));
      w.raw(reply);
      m_sock->write(w.data());
    }
  }

  QTcpSocket *m_sock;
  const RpcDispatcher *m_dispatcher;
  QByteArray m_in;  // raw bytes from the socket
  QByteArray m_msg; // fragments of the current message collected so far
};
} // namespace

TcpRpcServer::TcpRpcServer(const RpcDispatcher *dispatcher, QObject *parent)
  : QObject(parent), m_dispatcher(dispatcher)
{
  connect(&m_server, &QTcpServer::newConnection, this, &TcpRpcServer::onNewConnection);
}

bool TcpRpcServer::listen(quint16 port)
{
  return m_server.listen(QHostAddress::Any, port);
}

void TcpRpcServer::close()
{
  m_server.close();
  // accepted sockets are children of the QTcpServer
  const auto sockets = m_server.findChildren<QTcpSocket *>(Qt::FindDirectChildrenOnly);
  for (QTcpSocket *s : sockets)
  {
    s->abort();
    s->deleteLater();
  }
}

void TcpRpcServer::onNewConnection()
{
  while (QTcpSocket *sock = m_server.nextPendingConnection())
  {
    sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    new RpcConnection(sock, m_dispatcher); // child of the socket
    connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
  }
}

// ---------------------------------------------------------------- UDP

UdpRpcServer::UdpRpcServer(const RpcDispatcher *dispatcher, QObject *parent)
  : QObject(parent), m_dispatcher(dispatcher)
{
  connect(&m_socket, &QUdpSocket::readyRead, this, [this]()
  {
    while (m_socket.hasPendingDatagrams())
    {
      const QNetworkDatagram dg = m_socket.receiveDatagram();
      const QByteArray reply = m_dispatcher->handle(dg.data());
      if (!reply.isEmpty())
        m_socket.writeDatagram(dg.makeReply(reply));
    }
  });
}

bool UdpRpcServer::bind(quint16 port)
{
  return m_socket.bind(QHostAddress::AnyIPv4, port);
}

void UdpRpcServer::close()
{
  m_socket.close();
}
