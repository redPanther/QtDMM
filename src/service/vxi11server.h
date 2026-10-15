// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// VXI-11 server: core + abort channel and the portmapper question. If a system rpcbind answers on
// this host the programs are registered there; otherwise an own portmapper serves port 111.
//
// A host has ONE portmapper entry per RPC program, so only one process per machine can offer VXI-11.
// Further processes get an error from start() (use HiSLIP, which has no such limit, instead).
//
// QtDMM never raises its own rights: an own portmapper on 111 needs a system that allows it
// (Windows, macOS since 10.14, Linux with CAP_NET_BIND_SERVICE or ip_unprivileged_port_start),
// the system rpcbind needs to run. docs/user/scpi-server.md says how.

#include "service/oncrpc.h"
#include "service/rpcbindclient.h"
#include "service/scpiendpoint.h"
#include "service/vxi11core.h"

#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>

class Vxi11Server : public QObject
{
  Q_OBJECT
public:
  enum class RpcbindMode
  {
    Auto,   // system rpcbind if it answers, otherwise own portmapper
    System, // system rpcbind only
    Own     // own portmapper only
  };
  static constexpr quint16 kPortmapperPort = 111;

  /// "auto", "system", "own" (settings key Scpi/vxi11-rpc, option --vxi11-rpc)
  static QString modeName(RpcbindMode mode);
  static RpcbindMode modeFromName(const QString &name, bool *ok = nullptr);

  explicit Vxi11Server(const ScpiEndpoint &endpoint, QObject *parent = nullptr);
  ~Vxi11Server() override;

  // Core, abort channel and an own portmapper listen on @p address (localhost or any); with the
  // system rpcbind the system decides about 111. corePort / abortPort 0 = pick free ports (the
  // portmapper tells the clients). portmapperPort is only used with an own portmapper; clients
  // always ask 111 (tests use a high one).
  bool start(const QHostAddress &address, RpcbindMode mode = RpcbindMode::Auto, quint16 corePort = 0,
             quint16 abortPort = 0, quint16 portmapperPort = kPortmapperPort);
  void stop();

  bool isRunning() const { return m_running; }
  bool usesSystemRpcbind() const { return m_systemRpcbind; }
  quint16 corePort() const { return m_coreSrv.port(); }
  quint16 portmapperPort() const { return m_pmapPort; }
  QHostAddress address() const { return m_address; }
  RpcbindMode mode() const { return m_mode; }
  int linkCount() const { return m_core.linkCount(); }
  // Why start() failed, for the status line (translated).
  QString error() const { return m_error; }

  // Where the system rpcbind is looked for (tests: a fake one; default /run/rpcbind.sock,
  // /var/run/rpcbind.sock and 127.0.0.1:111).
  void setRpcbind(const QStringList &socketPaths, quint16 tcpPort);

Q_SIGNALS:
  void linksChanged(int count);

private:
  struct Reg
  {
    quint32 prog;
    quint32 vers;
    quint16 port;
  };

  bool registerWithRpcbind();

  Vxi11Core m_core;
  Vxi11Abort m_abort;
  RpcDispatcher m_coreDisp;
  RpcDispatcher m_abortDisp;
  RpcDispatcher m_pmapDisp;
  Portmapper m_pmap;
  TcpRpcServer m_coreSrv;
  TcpRpcServer m_abortSrv;
  TcpRpcServer m_pmapTcp;
  UdpRpcServer m_pmapUdp;
  RpcbindClient m_rpcbind;
  QList<Reg> m_regs;
  QHostAddress m_address;
  RpcbindMode m_mode = RpcbindMode::Auto;
  quint16 m_pmapPort = 0;
  bool m_running = false;
  bool m_systemRpcbind = false;
  QString m_error;
};
