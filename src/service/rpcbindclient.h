// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Tiny synchronous client for the system rpcbind/portmap (program 100000, version 2) on localhost.

#include <QByteArray>
#include <QtGlobal>

class RpcbindClient
{
public:
  explicit RpcbindClient(quint16 port = 111, int timeoutMs = 1000) : m_port(port), m_timeoutMs(timeoutMs) {}

  // Does an RPC portmapper answer the NULL procedure on 127.0.0.1:<port> (TCP)?
  bool isAvailable();
  // PMAPPROC_SET: program/version -> TCP port. False if unreachable or refused.
  bool set(quint32 prog, quint32 vers, quint16 tcpPort);
  // PMAPPROC_UNSET: remove the entry.
  bool unset(quint32 prog, quint32 vers);
  // PMAPPROC_GETPORT: registered TCP port, 0 if none (or on error).
  quint16 getPort(quint32 prog, quint32 vers);

private:
  bool call(quint32 proc, const QByteArray &args, quint32 *firstResult);

  quint16 m_port;
  int m_timeoutMs;
  quint32 m_xid = 0x56583131; // "VX11"
};
