// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ONC-RPC (RFC 5531) minimal server: dispatcher, TCP (record marking), UDP, portmapper v2.

#include "service/xdr.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QUdpSocket>

enum class RpcStatus
{
  Success,
  ProcUnavail,
  GarbageArgs,
  NoReply
};

// One RPC program in exactly one version.
class RpcService
{
public:
  virtual ~RpcService() = default;
  virtual quint32 program() const = 0;
  virtual quint32 version() const = 0;
  // Read the arguments from 'args', write the result to 'result'. The NULL procedure (0) is
  // answered by the dispatcher.
  virtual RpcStatus call(quint32 proc, XdrReader &args, XdrWriter &result) = 0;
};

// Processes one complete RPC call message and returns the reply message (empty: no reply).
class RpcDispatcher
{
public:
  void addService(RpcService *service) { m_services.append(service); }
  QByteArray handle(const QByteArray &callMsg) const;

private:
  QList<RpcService *> m_services;
};

// Portmapper v2 (program 100000): GETPORT and CALLIT (NULL procedure only, for broadcast discovery).
class Portmapper : public RpcService
{
public:
  quint32 program() const override { return 100000; }
  quint32 version() const override { return 2; }
  void registerProgram(quint32 prog, quint16 tcpPort) { m_ports[prog] = tcpPort; }
  void clear() { m_ports.clear(); }
  RpcStatus call(quint32 proc, XdrReader &args, XdrWriter &result) override;

private:
  QHash<quint32, quint16> m_ports;
};

class TcpRpcServer : public QObject
{
  Q_OBJECT
public:
  explicit TcpRpcServer(const RpcDispatcher *dispatcher, QObject *parent = nullptr);
  bool listen(quint16 port);
  void close(); // stops listening and drops all connections
  quint16 port() const { return m_server.serverPort(); }
  QString errorString() const { return m_server.errorString(); }

private:
  void onNewConnection();
  const RpcDispatcher *m_dispatcher;
  QTcpServer m_server;
};

class UdpRpcServer : public QObject
{
  Q_OBJECT
public:
  explicit UdpRpcServer(const RpcDispatcher *dispatcher, QObject *parent = nullptr);
  bool bind(quint16 port);
  void close();

private:
  const RpcDispatcher *m_dispatcher;
  QUdpSocket m_socket;
};
