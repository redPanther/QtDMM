// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "service/rpcbindclient.h"

#include "service/xdr.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpSocket>

namespace
{
constexpr quint32 kPmapProg = 100000;
constexpr quint32 kPmapVers = 2;
constexpr quint32 kProtTcp = 6;
constexpr quint32 kMaxReply = 64 * 1024;

bool readExact(QTcpSocket &sock, qsizetype n, const QElapsedTimer &timer, int timeoutMs, QByteArray &out)
{
  while (sock.bytesAvailable() < n)
  {
    const int remaining = timeoutMs - int(timer.elapsed());
    if (remaining <= 0 || !sock.waitForReadyRead(remaining))
      return false;
  }
  out = sock.read(n);
  return out.size() == n;
}
} // namespace

bool RpcbindClient::call(quint32 proc, const QByteArray &args, quint32 *firstResult)
{
  QTcpSocket sock;
  sock.connectToHost(QHostAddress::LocalHost, m_port);
  if (!sock.waitForConnected(m_timeoutMs))
    return false;

  const quint32 xid = ++m_xid;
  XdrWriter call;
  call.u32(xid);
  call.u32(0); // CALL
  call.u32(2); // RPC version
  call.u32(kPmapProg);
  call.u32(kPmapVers);
  call.u32(proc);
  call.u32(0); // credentials: AUTH_NONE
  call.u32(0);
  call.u32(0); // verifier:    AUTH_NONE
  call.u32(0);
  call.raw(args);

  XdrWriter frame; // record marking: a single, last fragment
  frame.u32(0x80000000u | quint32(call.data().size()));
  frame.raw(call.data());
  sock.write(frame.data());
  if (!sock.waitForBytesWritten(m_timeoutMs))
    return false;

  QElapsedTimer timer;
  timer.start();
  QByteArray reply;
  for (;;)
  {
    QByteArray hdr;
    if (!readExact(sock, 4, timer, m_timeoutMs, hdr))
      return false;
    const quint32 h = qFromBigEndian<quint32>(hdr.constData());
    const quint32 len = h & 0x7FFFFFFFu;
    if (len > kMaxReply)
      return false;
    QByteArray frag;
    if (!readExact(sock, len, timer, m_timeoutMs, frag))
      return false;
    reply += frag;
    if (h & 0x80000000u)
      break;
  }

  XdrReader r(reply);
  if (r.u32() != xid)
    return false;
  if (r.u32() != 1) // REPLY
    return false;
  if (r.u32() != 0) // MSG_ACCEPTED
    return false;
  r.u32(); // verifier
  r.opaque();
  if (r.u32() != 0) // accept_stat == SUCCESS
    return false;
  if (firstResult)
    *firstResult = r.u32();
  return r.ok();
}

bool RpcbindClient::isAvailable()
{
  return call(0, {}, nullptr);
}

bool RpcbindClient::set(quint32 prog, quint32 vers, quint16 tcpPort)
{
  XdrWriter a;
  a.u32(prog);
  a.u32(vers);
  a.u32(kProtTcp);
  a.u32(tcpPort);
  quint32 ok = 0;
  return call(1, a.data(), &ok) && ok != 0;
}

bool RpcbindClient::unset(quint32 prog, quint32 vers)
{
  XdrWriter a; // like pmap_unset(): protocol and port are 0
  a.u32(prog);
  a.u32(vers);
  a.u32(0);
  a.u32(0);
  quint32 ok = 0;
  return call(2, a.data(), &ok) && ok != 0;
}

quint16 RpcbindClient::getPort(quint32 prog, quint32 vers)
{
  XdrWriter a;
  a.u32(prog);
  a.u32(vers);
  a.u32(kProtTcp);
  a.u32(0);
  quint32 port = 0;
  if (!call(3, a.data(), &port))
    return 0;
  return quint16(port);
}
