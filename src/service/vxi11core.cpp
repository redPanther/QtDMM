// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "service/vxi11core.h"

Q_LOGGING_CATEGORY(lcVxi11, "qtdmm.scpi.vxi11", QtWarningMsg)

namespace
{
// Device_ErrorCode of the VXI-11 spec
constexpr qint32 kErrNone = 0;
constexpr qint32 kErrInvalidLink = 4;
constexpr qint32 kErrNotSupported = 8;
constexpr qint32 kErrOutOfResources = 9;
constexpr qint32 kErrLocked = 11;
constexpr qint32 kErrNoLock = 12;
constexpr qint32 kErrTimeout = 15;
constexpr qint32 kErrIo = 17;

constexpr quint32 kFlagEnd = 0x08;      // device_write: last block of the message
constexpr quint32 kReasonReqCnt = 1;    // device_read: requested size reached
constexpr quint32 kReasonEnd = 4;       // device_read: end of the message
constexpr quint32 kMaxRecvSize = 65536; // largest block a client may send us
} // namespace

bool Vxi11Core::owns(qint32 lid, quint64 conn) const
{
  // lids count up from 1 and are easy to guess; another connection's lid is no link of this one
  const auto it = m_links.constFind(lid);
  return it != m_links.cend() && it->conn == conn;
}

qint32 Vxi11Core::checkAccess(qint32 lid, quint64 conn) const
{
  if (!owns(lid, conn))
    return kErrInvalidLink;
  if (m_lockOwner != -1 && m_lockOwner != lid)
    return kErrLocked;
  return kErrNone;
}

qint32 Vxi11Core::abortLink(qint32 lid)
{
  auto it = m_links.find(lid);
  if (it == m_links.end())
    return kErrInvalidLink;
  it->in.clear();
  it->out.clear();
  it->discard = false; // the message too big is gone too: the next write starts a new one
  return kErrNone;
}

void Vxi11Core::reset()
{
  const bool had = !m_links.isEmpty();
  m_links.clear();
  m_lockOwner = -1;
  if (had && m_linksChanged)
    m_linksChanged();
}

void Vxi11Core::connectionClosed(quint64 conn)
{
  bool removed = false;
  for (auto it = m_links.begin(); it != m_links.end();)
  {
    if (it->conn != conn)
    {
      ++it;
      continue;
    }
    qCDebug(lcVxi11) << "connection closed, dropping link" << it.key();
    if (m_lockOwner == it.key())
      m_lockOwner = -1;
    it = m_links.erase(it);
    removed = true;
  }
  if (removed && m_linksChanged)
    m_linksChanged();
}

RpcStatus Vxi11Core::call(quint32 proc, XdrReader &a, XdrWriter &r, quint64 conn)
{
  qint32 lid = 0;
  // Device_GenericParms: lid, flags, lock_timeout, io_timeout
  auto generic = [&]()
  {
    lid = a.i32();
    a.u32();
    a.u32();
    a.u32();
    return a.ok();
  };

  switch (proc)
  {
    case 10: // create_link(clientId, lockDevice, lock_timeout, device)
    {
      a.u32();
      const bool lock = a.u32() != 0;
      a.u32();
      const QByteArray dev = a.opaque();
      if (!a.ok())
        return RpcStatus::GarbageArgs;
      qCDebug(lcVxi11) << "create_link, device" << dev;
      if (lock && m_lockOwner != -1)
      {
        r.i32(kErrLocked);
        r.i32(0);
        r.u32(0);
        r.u32(0);
        return RpcStatus::Success;
      }
      if (m_links.size() >= kMaxLinks)
      {
        r.i32(kErrOutOfResources);
        r.i32(0);
        r.u32(0);
        r.u32(0);
        return RpcStatus::Success;
      }
      lid = m_nextLid++;
      Link link;
      link.conn = conn;
      m_links.insert(lid, link);
      if (lock)
        m_lockOwner = lid;
      if (m_linksChanged)
        m_linksChanged();
      r.i32(kErrNone);
      r.i32(lid);
      r.u32(m_abortPort);
      r.u32(kMaxRecvSize);
      return RpcStatus::Success;
    }

    case 11: // device_write(lid, io_timeout, lock_timeout, flags, data)
    {
      lid = a.i32();
      a.u32();
      a.u32();
      const quint32 flags = a.u32();
      const QByteArray data = a.opaque();
      if (!a.ok())
        return RpcStatus::GarbageArgs;
      const qint32 err = checkAccess(lid, conn);
      if (err)
      {
        r.i32(err);
        r.u32(0);
        return RpcStatus::Success;
      }
      Link &l = m_links[lid];
      if (l.discard || l.in.size() + data.size() > kMaxMessage)
      {
        // too big: the rest of this message is dropped too, not run as a command of its own
        l.in.clear();
        l.discard = !(flags & kFlagEnd);
        r.i32(kErrIo);
        r.u32(0);
        return RpcStatus::Success;
      }
      l.in += data;
      if (flags & kFlagEnd)
      {
        const QByteArray message = l.in;
        l.in.clear();
        const QList<QByteArray> responses = m_ep.run(message);
        // run() may have re-entered us (device cleared, link destroyed): look the link up again
        auto it = m_links.find(lid);
        if (it != m_links.end())
          it->out += responses;
      }
      r.i32(kErrNone);
      r.u32(quint32(data.size()));
      return RpcStatus::Success;
    }

    case 12: // device_read(lid, requestSize, io_timeout, lock_timeout, flags, termChar)
    {
      lid = a.i32();
      const quint32 requestSize = a.u32();
      a.u32();
      a.u32();
      a.u32();
      a.u32();
      if (!a.ok())
        return RpcStatus::GarbageArgs;
      qint32 err = checkAccess(lid, conn);
      if (!err && m_links[lid].out.isEmpty())
        err = kErrTimeout; // nothing to read -> I/O timeout
      if (err)
      {
        r.i32(err);
        r.u32(0);
        r.opaque({});
        return RpcStatus::Success;
      }
      // Always one response message per read; a big one (screen dump) is handed out in pieces
      QByteArray &msg = m_links[lid].out.first();
      const qsizetype n = requestSize > 0 ? qMin<qsizetype>(requestSize, msg.size()) : msg.size();
      const QByteArray chunk = msg.left(n);
      msg.remove(0, n);
      const bool done = msg.isEmpty();
      if (done)
        m_links[lid].out.removeFirst();
      r.i32(kErrNone);
      r.u32(done ? kReasonEnd : kReasonReqCnt);
      r.opaque(chunk);
      return RpcStatus::Success;
    }

    case 13: // device_readstb
    {
      if (!generic())
        return RpcStatus::GarbageArgs;
      const qint32 err = checkAccess(lid, conn);
      quint32 stb = 0;
      if (!err)
      {
        stb = m_ep.stb();
        if (!m_links[lid].out.isEmpty())
          stb |= 0x10; // MAV: a response is waiting
      }
      r.i32(err);
      r.u32(stb);
      return RpcStatus::Success;
    }

    case 14: // device_trigger
    case 16: // device_remote
    case 17: // device_local
    {
      if (!generic())
        return RpcStatus::GarbageArgs;
      r.i32(checkAccess(lid, conn));
      return RpcStatus::Success;
    }

    case 15: // device_clear
    {
      if (!generic())
        return RpcStatus::GarbageArgs;
      const qint32 err = checkAccess(lid, conn);
      if (!err)
      {
        m_links[lid].in.clear();
        m_links[lid].out.clear();
        m_links[lid].discard = false;
        if (m_ep.deviceClear)
          m_ep.deviceClear();
      }
      r.i32(err);
      return RpcStatus::Success;
    }

    case 18: // device_lock(lid, flags, lock_timeout) - does not wait, answers at once
    {
      lid = a.i32();
      a.u32();
      a.u32();
      if (!a.ok())
        return RpcStatus::GarbageArgs;
      qint32 err = kErrNone;
      if (!owns(lid, conn))
        err = kErrInvalidLink;
      else if (m_lockOwner == -1)
        m_lockOwner = lid;
      else if (m_lockOwner != lid)
        err = kErrLocked;
      r.i32(err);
      return RpcStatus::Success;
    }

    case 19: // device_unlock(lid)
    {
      lid = a.i32();
      if (!a.ok())
        return RpcStatus::GarbageArgs;
      qint32 err = kErrNone;
      if (!owns(lid, conn))
        err = kErrInvalidLink;
      else if (m_lockOwner != lid)
        err = kErrNoLock;
      else
        m_lockOwner = -1;
      r.i32(err);
      return RpcStatus::Success;
    }

    case 20: // device_enable_srq   (no interrupt channel)
    case 25: // create_intr_chan
    case 26: // destroy_intr_chan
      r.i32(kErrNotSupported);
      return RpcStatus::Success;

    case 22: // device_docmd
      r.i32(kErrNotSupported);
      r.opaque({});
      return RpcStatus::Success;

    case 23: // destroy_link(lid)
    {
      lid = a.i32();
      if (!a.ok())
        return RpcStatus::GarbageArgs;
      const qint32 err = owns(lid, conn) && m_links.remove(lid) ? kErrNone : kErrInvalidLink;
      if (!err && m_lockOwner == lid)
        m_lockOwner = -1;
      if (!err && m_linksChanged)
        m_linksChanged();
      qCDebug(lcVxi11) << "destroy_link" << lid;
      r.i32(err);
      return RpcStatus::Success;
    }

    default:
      return RpcStatus::ProcUnavail;
  }
}

RpcStatus Vxi11Abort::call(quint32 proc, XdrReader &a, XdrWriter &r, quint64)
{
  if (proc != 1) // device_abort
    return RpcStatus::ProcUnavail;
  const qint32 lid = a.i32();
  if (!a.ok())
    return RpcStatus::GarbageArgs;
  r.i32(m_core->abortLink(lid));
  return RpcStatus::Success;
}
