// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// VXI-11 core and abort channel as RPC services.

#include "service/rpc.h"
#include "service/scpiendpoint.h"

#include <QHash>
#include <QList>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcVxi11)

class Vxi11Core : public RpcService
{
public:
  explicit Vxi11Core(const ScpiEndpoint &endpoint) : m_ep(endpoint) {}

  quint32 program() const override { return 0x0607AF; } // DEVICE_CORE
  quint32 version() const override { return 1; }
  RpcStatus call(quint32 proc, XdrReader &a, XdrWriter &r) override;

  void setAbortPort(quint16 port) { m_abortPort = port; }
  qint32 abortLink(qint32 lid);
  void reset(); // drops all links (server stopped)

private:
  struct Link
  {
    QByteArray in;        // incomplete command received so far
    QList<QByteArray> out; // responses waiting for device_read, one entry per response message
  };

  qint32 checkAccess(qint32 lid) const;

  ScpiEndpoint m_ep;
  quint16 m_abortPort = 0;
  QHash<qint32, Link> m_links;
  qint32 m_nextLid = 1;
  qint32 m_lockOwner = -1;
};

class Vxi11Abort : public RpcService
{
public:
  explicit Vxi11Abort(Vxi11Core *core) : m_core(core) {}
  quint32 program() const override { return 0x0607B0; } // DEVICE_ASYNC
  quint32 version() const override { return 1; }
  RpcStatus call(quint32 proc, XdrReader &a, XdrWriter &r) override;

private:
  Vxi11Core *m_core;
};
