// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// VXI-11 core and abort channel as RPC services.

#include "service/oncrpc.h"
#include "service/scpiendpoint.h"

#include <QHash>
#include <QList>
#include <QLoggingCategory>

#include <functional>

Q_DECLARE_LOGGING_CATEGORY(lcVxi11)

class Vxi11Core : public RpcService
{
public:
  // Links served at the same time; one more gets "out of resources" (9).
  static constexpr int kMaxLinks = 16;
  // Largest message a link may collect over several device_write blocks; SCPI lines are short.
  static constexpr qsizetype kMaxMessage = 1024 * 1024;

  explicit Vxi11Core(const ScpiEndpoint &endpoint) : m_ep(endpoint) {}

  quint32 program() const override { return 0x0607AF; } // DEVICE_CORE
  quint32 version() const override { return 1; }
  RpcStatus call(quint32 proc, XdrReader &a, XdrWriter &r, quint64 conn) override;
  // A client that goes away without destroy_link leaves no link and no lock behind.
  void connectionClosed(quint64 conn) override;

  void setAbortPort(quint16 port) { m_abortPort = port; }
  qint32 abortLink(qint32 lid);
  void reset(); // drops all links (server stopped)
  int linkCount() const { return int(m_links.size()); }
  // Called when links come or go (the sidebar counts them as clients).
  void setLinksChanged(std::function<void()> f) { m_linksChanged = std::move(f); }

private:
  struct Link
  {
    quint64 conn = 0;      // the TCP connection that created it
    QByteArray in;         // incomplete command received so far
    bool discard = false;  // the message grew too big: its blocks up to the END are dropped
    QList<QByteArray> out; // responses waiting for device_read, one entry per response message
  };

  // A lid counts only on the connection that created it (the abort channel is the exception).
  /// The procedures with more to do than an answer (call() has the rest).
  RpcStatus createLink(XdrReader &a, XdrWriter &r, quint64 conn);
  RpcStatus deviceWrite(XdrReader &a, XdrWriter &r, quint64 conn);
  RpcStatus deviceRead(XdrReader &a, XdrWriter &r, quint64 conn);
  bool owns(qint32 lid, quint64 conn) const;
  qint32 checkAccess(qint32 lid, quint64 conn) const;

  ScpiEndpoint m_ep;
  quint16 m_abortPort = 0;
  QHash<qint32, Link> m_links;
  qint32 m_nextLid = 1;
  qint32 m_lockOwner = -1;
  std::function<void()> m_linksChanged;
};

class Vxi11Abort : public RpcService
{
public:
  explicit Vxi11Abort(Vxi11Core *core) : m_core(core) {}
  quint32 program() const override { return 0x0607B0; } // DEVICE_ASYNC
  quint32 version() const override { return 1; }
  RpcStatus call(quint32 proc, XdrReader &a, XdrWriter &r, quint64 conn) override;

private:
  Vxi11Core *m_core;
};
