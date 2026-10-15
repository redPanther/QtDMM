// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Tiny synchronous client for the system rpcbind/portmap (program 100000, version 2) on this host.
//
// The standard rpcbind (Linux, FreeBSD) accepts SET and UNSET only from a local transport: over
// its Unix socket it takes the caller's user id as the owner, no root needed; over TCP/UDP
// loopback it refuses them unless it runs with -i or -L. So the Unix socket comes first, TCP
// on 127.0.0.1 is the fallback (and the way to find an own portmapper of another QtDMM).

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtGlobal>

class QIODevice;

class RpcbindClient
{
public:
  // How the last call reached rpcbind.
  enum class Via
  {
    None,
    UnixSocket,
    Tcp
  };

  // @p socketPaths: the Unix sockets to try (default: /run/rpcbind.sock, /var/run/rpcbind.sock);
  // @p tcpPort: the TCP port on 127.0.0.1. Short timeouts: it runs in the GUI thread, when the
  // settings are applied.
  explicit RpcbindClient(quint16 tcpPort = 111, int timeoutMs = 300);
  void setSocketPaths(const QStringList &paths) { m_paths = paths; }

  // Does a portmapper answer the NULL procedure (Unix socket first, then TCP)? It is then used
  // for the calls below.
  bool isAvailable();
  Via via() const { return m_via; }
  // PMAPPROC_SET: program/version -> TCP port. False if unreachable or refused.
  bool set(quint32 prog, quint32 vers, quint16 tcpPort);
  // PMAPPROC_UNSET: remove the entry.
  bool unset(quint32 prog, quint32 vers);
  // PMAPPROC_GETPORT: registered TCP port, 0 if none (or on error).
  quint16 getPort(quint32 prog, quint32 vers);

private:
  bool call(quint32 proc, const QByteArray &args, quint32 *firstResult);
  bool callOn(QIODevice &dev, quint32 proc, const QByteArray &args, quint32 *firstResult);

  QStringList m_paths;
  quint16 m_port;
  int m_timeoutMs;
  Via m_via = Via::None;
  QString m_path;   ///< the Unix socket that answered
  quint32 m_xid = 0x56583131; // "VX11"
};
