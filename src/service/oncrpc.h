// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ONC-RPC (RFC 5531) minimal server: dispatcher, TCP (record marking), UDP, portmapper v2.

#include "service/xdr.h"

#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QUdpSocket>

class QTcpSocket;

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
  // answered by the dispatcher. 'conn' names the TCP connection the call came on (0: UDP), so
  // state can be tied to it.
  virtual RpcStatus call(quint32 proc, XdrReader &args, XdrWriter &result, quint64 conn) = 0;
  // The TCP connection 'conn' is gone: what belonged to it goes too.
  virtual void connectionClosed(quint64 conn) { Q_UNUSED(conn) }
};

// Processes one complete RPC call message and returns the reply message (empty: no reply).
class RpcDispatcher
{
public:
  void addService(RpcService *service) { m_services.append(service); }
  QByteArray handle(const QByteArray &callMsg, quint64 conn = 0) const;
  void connectionClosed(quint64 conn) const;

private:
  QList<RpcService *> m_services;
};

// Portmapper v2 (program 100000): GETPORT, DUMP and CALLIT (NULL procedure only, for broadcast
// discovery). SET and UNSET are refused: only this process registers, directly.
class Portmapper : public RpcService
{
public:
  quint32 program() const override { return 100000; }
  quint32 version() const override { return 2; }
  void registerProgram(quint32 prog, quint32 vers, quint16 tcpPort);
  void clear() { m_entries.clear(); }
  RpcStatus call(quint32 proc, XdrReader &args, XdrWriter &result, quint64 conn) override;

private:
  struct Entry
  {
    quint32 prog;
    quint32 vers;
    quint16 port;
  };
  QList<Entry> m_entries;
};

class TcpRpcServer : public QObject
{
  Q_OBJECT
public:
  // Largest record (all fragments of one call) accepted; VXI-11 blocks are at most 64 KiB.
  static constexpr qsizetype kMaxRecord = 1024 * 1024;

  explicit TcpRpcServer(const RpcDispatcher *dispatcher, QObject *parent = nullptr);
  bool listen(const QHostAddress &address, quint16 port);
  void close(); // stops listening and drops all connections
  bool isListening() const { return m_server.isListening(); }
  quint16 port() const { return m_server.serverPort(); }
  QString errorString() const { return m_server.errorString(); }
  QAbstractSocket::SocketError serverError() const { return m_server.serverError(); }

private:
  void onNewConnection();
  const RpcDispatcher *m_dispatcher;
  QTcpServer m_server;
  quint64 m_nextConn = 1;
};

class UdpRpcServer : public QObject
{
  Q_OBJECT
public:
  explicit UdpRpcServer(const RpcDispatcher *dispatcher, QObject *parent = nullptr);
  bool bind(const QHostAddress &address, quint16 port);
  void close();
  QString errorString() const { return m_socket.errorString(); }

private:
  const RpcDispatcher *m_dispatcher;
  QUdpSocket m_socket;
};
