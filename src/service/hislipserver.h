// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// HiSLIP server (IVI-6.1, protocol version 1.0): one synchronous and one asynchronous TCP channel
// per session on a single port (default 4880). No RPC, no portmapper. VISA finds it as
// TCPIP::<host>::hislip0[,<port>]::INSTR. Synchronized mode only, no locks.

#include "service/scpiendpoint.h"

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTcpServer>

class QTcpSocket;

class HislipServer : public QObject
{
  Q_OBJECT
public:
  explicit HislipServer(const ScpiEndpoint &endpoint, QObject *parent = nullptr);
  ~HislipServer() override;

  static constexpr quint16 kDefaultPort = 4880;
  // Sessions served at the same time; one more is refused (FatalError 4).
  static constexpr int kMaxSessions = 16;
  // Largest message accepted from a client (also announced); SCPI lines are short.
  static constexpr quint64 kMaxMessage = 1u << 20;

  // Like ScpiServer::start(): tries 'tries' consecutive ports beginning at 'port'.
  bool start(const QHostAddress &address, quint16 port = kDefaultPort, int tries = 1);
  void stop();

  bool isListening() const { return m_server.isListening(); }
  quint16 port() const { return m_server.serverPort(); }
  QHostAddress address() const { return m_server.serverAddress(); }
  int sessionCount() const { return int(m_sessions.size()); }
  QString error() const { return m_error; }

Q_SIGNALS:
  void sessionsChanged(int count);

private:
  enum class Kind
  {
    Unknown,
    Sync,
    Async
  };

  struct Conn // one per TCP connection
  {
    QByteArray in;
    Kind kind = Kind::Unknown;
    quint16 session = 0;
    bool closing = false;   ///< FatalError sent: what still comes is read and dropped
  };
  struct Session // one per HiSLIP session (two connections)
  {
    QTcpSocket *sync = nullptr;
    QTcpSocket *async = nullptr;
    QByteArray msgBuf;          // Data fragments received so far
    quint64 clientMaxMsg = 272; // default until AsyncMaximumMessageSize is negotiated
  };

  void onNewConnection();
  void onReadyRead(QTcpSocket *s);
  void onDisconnected(QTcpSocket *s);

  void dispatch(QTcpSocket *s, quint8 type, quint8 ctrl, quint32 param, const QByteArray &payload);
  void handleSync(QTcpSocket *s, Session &sess, quint8 type, quint8 ctrl, quint32 param, const QByteArray &payload);
  void handleAsync(QTcpSocket *s, Session &sess, quint8 type, quint8 ctrl, quint32 param, const QByteArray &payload);

  void send(QTcpSocket *s, quint8 type, quint8 ctrl, quint32 param, const QByteArray &payload = {});
  void sendData(QTcpSocket *s, const Session &sess, const QByteArray &data, quint32 msgId);
  void sendFatal(QTcpSocket *s, quint8 code, const QString &text);
  void sendError(QTcpSocket *s, quint8 code, const QString &text);
  quint16 newSessionId();
  /// deleteLater(), or after the command when one is running (see m_running)
  void release(QTcpSocket *s);

  ScpiEndpoint m_ep;
  QTcpServer m_server;
  QHash<QTcpSocket *, Conn> m_conns;
  QHash<quint16, Session> m_sessions;
  quint16 m_nextSession = 1;
  QString m_error;
  /// A command may spin an event loop (a dialog); a socket that goes meanwhile
  /// waits in m_released, deleting it there would pull it from under its own
  /// readyRead.
  int m_running = 0;
  QList<QTcpSocket *> m_released;
};
