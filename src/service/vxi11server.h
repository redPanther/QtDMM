// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// VXI-11 server: core + abort channel and the portmapper question. If a system rpcbind answers on
// localhost the programs are registered there; otherwise an own portmapper serves port 111.
//
// A host has ONE portmapper entry per RPC program, so only one process per machine can offer VXI-11.
// Further processes get an error from start() (use HiSLIP, which has no such limit, instead).

#include "service/rpc.h"
#include "service/rpcbindclient.h"
#include "service/scpiendpoint.h"
#include "service/vxi11core.h"

#include <QList>
#include <QObject>
#include <QString>

class Vxi11Server : public QObject
{
  Q_OBJECT
public:
  enum class RpcbindMode
  {
    Auto, // system rpcbind if it answers, otherwise own portmapper
    Yes,  // system rpcbind only
    No    // own portmapper only
  };

  explicit Vxi11Server(const ScpiEndpoint &endpoint, QObject *parent = nullptr);
  ~Vxi11Server() override;

  // corePort / abortPort 0 = pick free ports (the portmapper tells the clients). portmapperPort is
  // only used with an own portmapper; clients always ask 111.
  bool start(RpcbindMode mode = RpcbindMode::Auto, quint16 corePort = 0, quint16 abortPort = 0,
             quint16 portmapperPort = 111);
  void stop();

  bool isRunning() const { return m_running; }
  bool usesSystemRpcbind() const { return m_systemRpcbind; }
  quint16 corePort() const { return m_coreSrv.port(); }
  QString error() const { return m_error; }

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
  bool m_running = false;
  bool m_systemRpcbind = false;
  QString m_error;
};
