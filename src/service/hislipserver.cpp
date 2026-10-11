// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "service/hislipserver.h"

#include <QLoggingCategory>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

Q_LOGGING_CATEGORY(lcHislip, "qtdmm.scpi.hislip", QtWarningMsg)

namespace
{
// Message types (IVI-6.1, HiSLIP 1.0)
enum : quint8
{
  kInitialize = 0,
  kInitializeResponse = 1,
  kFatalError = 2,
  kError = 3,
  kAsyncLock = 4,
  kAsyncLockResponse = 5,
  kData = 6,
  kDataEnd = 7,
  kDeviceClearComplete = 8,
  kDeviceClearAcknowledge = 9,
  kAsyncRemoteLocalControl = 10,
  kAsyncRemoteLocalResponse = 11,
  kTrigger = 12,
  kAsyncMaximumMessageSize = 15,
  kAsyncMaximumMessageSizeResponse = 16,
  kAsyncInitialize = 17,
  kAsyncInitializeResponse = 18,
  kAsyncDeviceClear = 19,
  kAsyncStatusQuery = 21,
  kAsyncStatusResponse = 22,
  kAsyncDeviceClearAcknowledge = 23,
  kAsyncLockInfo = 24,
  kAsyncLockInfoResponse = 25,
};

constexpr quint16 kServerVersion = 0x0100;  // HiSLIP 1.0
// connections that have not said which channel they are, on top of two per session
constexpr int kMaxPendingConnections = 4;
// after a FatalError: how long what the client still sends is dropped before closing
constexpr int kFatalLingerMs = 500;
constexpr qsizetype kHeaderSize = 16;
} // namespace

HislipServer::HislipServer(const ScpiEndpoint &endpoint, QObject *parent) : QObject(parent), m_ep(endpoint)
{
  connect(&m_server, &QTcpServer::newConnection, this, &HislipServer::onNewConnection);
}

HislipServer::~HislipServer()
{
  stop();
}

bool HislipServer::start(const QHostAddress &address, quint16 port, int tries)
{
  stop();
  m_error.clear();
  for (int i = 0; i < tries && port + i <= 65535; ++i)
  {
    if (m_server.listen(address, quint16(port + i)))
    {
      qCDebug(lcHislip) << "listening on port" << m_server.serverPort();
      return true;
    }
    m_error = m_server.errorString();
    if (m_server.serverError() != QAbstractSocket::AddressInUseError)
      break;
  }
  return false;
}

void HislipServer::stop()
{
  if (m_server.isListening())
    m_server.close();
  const QList<QTcpSocket *> sockets = m_conns.keys();
  const bool had = !m_sessions.isEmpty();
  m_conns.clear();
  m_sessions.clear();
  for (QTcpSocket *s : sockets)
  {
    s->disconnect(this);
    s->abort();
    s->deleteLater();
  }
  if (had)
    Q_EMIT sessionsChanged(0);
}

// ---------------------------------------------------------------- connections

void HislipServer::onNewConnection()
{
  while (QTcpSocket *s = m_server.nextPendingConnection())
  {
    // two channels per session and a few on their way in; more is someone flooding the port
    if (m_conns.size() >= 2 * kMaxSessions + kMaxPendingConnections)
    {
      s->abort();
      s->deleteLater();
      continue;
    }
    s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_conns.insert(s, Conn{});
    connect(s, &QTcpSocket::readyRead, this, [this, s]() { onReadyRead(s); });
    connect(s, &QTcpSocket::disconnected, this, [this, s]() { onDisconnected(s); });
  }
}

void HislipServer::onDisconnected(QTcpSocket *s)
{
  const Conn c = m_conns.take(s);
  if (c.kind != Kind::Unknown)
  {
    auto it = m_sessions.find(c.session);
    if (it != m_sessions.end())
    {
      // when one channel closes the whole session is over
      QTcpSocket *other = (c.kind == Kind::Sync) ? it->async : it->sync;
      m_sessions.erase(it);
      qCDebug(lcHislip) << "session" << c.session << "closed";
      if (other && other != s)
        other->disconnectFromHost();
      Q_EMIT sessionsChanged(int(m_sessions.size()));
    }
  }
  s->deleteLater();
}

quint16 HislipServer::newSessionId()
{
  quint16 id;
  do
  {
    id = m_nextSession++;
  } while (id == 0 || m_sessions.contains(id));
  return id;
}

// ---------------------------------------------------------------- framing

void HislipServer::onReadyRead(QTcpSocket *s)
{
  auto it = m_conns.find(s);
  if (it == m_conns.end())
    return;
  if (it->closing)
  {
    s->readAll();
    return;
  }
  it->in += s->readAll();

  for (;;)
  {
    it = m_conns.find(s); // the entry may be gone after dispatch()
    if (it == m_conns.end() || it->closing)
      return;
    QByteArray &in = it->in;
    if (in.size() < kHeaderSize)
      return;

    const char *d = in.constData();
    if (d[0] != 'H' || d[1] != 'S')
    {
      sendFatal(s, 1, QStringLiteral("Poorly formed message header"));
      return;
    }
    const quint8 type = quint8(d[2]);
    const quint8 ctrl = quint8(d[3]);
    const quint32 param = qFromBigEndian<quint32>(d + 4);
    const quint64 len = qFromBigEndian<quint64>(d + 8);
    if (len > HislipServer::kMaxMessage)
    {
      sendFatal(s, 0, QStringLiteral("Message too large"));
      return;
    }
    if (quint64(in.size()) < kHeaderSize + len)
      return;

    const QByteArray payload = in.mid(kHeaderSize, qsizetype(len));
    in.remove(0, kHeaderSize + qsizetype(len));
    dispatch(s, type, ctrl, param, payload);
  }
}

void HislipServer::send(QTcpSocket *s, quint8 type, quint8 ctrl, quint32 param, const QByteArray &payload)
{
  QByteArray msg;
  msg.reserve(kHeaderSize + payload.size());
  msg.append("HS", 2);
  msg.append(char(type));
  msg.append(char(ctrl));
  char b[8];
  qToBigEndian(param, b);
  msg.append(b, 4);
  qToBigEndian(quint64(payload.size()), b);
  msg.append(b, 8);
  msg.append(payload);
  s->write(msg);
}

// Sends a response in pieces that fit the client's maximum message size (conservatively without header).
void HislipServer::sendData(QTcpSocket *s, const Session &sess, const QByteArray &data, quint32 msgId)
{
  const qsizetype maxMsg = qsizetype(qMin<quint64>(sess.clientMaxMsg, 1u << 30));
  const qsizetype chunk = qMax<qsizetype>(1, maxMsg - kHeaderSize);
  qsizetype pos = 0;
  while (pos < data.size())
  {
    const QByteArray part = data.mid(pos, chunk);
    pos += part.size();
    send(s, pos >= data.size() ? kDataEnd : kData, 0, msgId, part);
  }
}

void HislipServer::sendFatal(QTcpSocket *s, quint8 code, const QString &text)
{
  qCWarning(lcHislip) << "FatalError:" << text;
  send(s, kFatalError, code, 0, text.toUtf8());
  // Not closed at once: a client still sending (a message too large) would
  // leave unread bytes here, and closing over them resets the connection -
  // on Windows the FatalError is then lost on the way. Drop what comes for
  // a moment, then close.
  auto it = m_conns.find(s);
  if (it == m_conns.end())
  {
    s->disconnectFromHost();
    return;
  }
  it->closing = true;
  it->in.clear();
  QTimer::singleShot(kFatalLingerMs, s, [s]()
  {
    s->readAll();
    s->disconnectFromHost();
  });
}

void HislipServer::sendError(QTcpSocket *s, quint8 code, const QString &text)
{
  qCWarning(lcHislip) << "Error:" << text;
  send(s, kError, code, 0, text.toUtf8());
}

// ---------------------------------------------------------------- messages

void HislipServer::dispatch(QTcpSocket *s, quint8 type, quint8 ctrl, quint32 param, const QByteArray &payload)
{
  auto it = m_conns.find(s);
  if (it == m_conns.end())
    return;
  const Kind kind = it->kind;
  const quint16 sid = it->session;

  if (type == kInitialize) // first connection of a session = synchronous channel
  {
    if (kind != Kind::Unknown)
    {
      sendFatal(s, 3, QStringLiteral("Invalid initialization sequence"));
      return;
    }
    if (m_sessions.size() >= kMaxSessions)
    {
      sendFatal(s, 4, QStringLiteral("Maximum number of clients exceeded"));
      return;
    }
    const quint16 id = newSessionId();
    Session sess;
    sess.sync = s;
    m_sessions.insert(id, sess);
    it->kind = Kind::Sync;
    it->session = id;
    qCDebug(lcHislip) << "Initialize, sub-address" << QString::fromUtf8(payload) << "-> session" << id;
    // control code 0 = synchronized mode; parameter = server version | session id
    send(s, kInitializeResponse, 0, (quint32(kServerVersion) << 16) | id);
    return;
  }

  if (type == kAsyncInitialize) // second connection = asynchronous channel, parameter = session id
  {
    const quint16 id = quint16(param & 0xFFFF);
    auto sit = m_sessions.find(id);
    if (kind != Kind::Unknown || sit == m_sessions.end() || sit->async)
    {
      sendFatal(s, 3, QStringLiteral("Invalid initialization sequence"));
      return;
    }
    sit->async = s;
    it->kind = Kind::Async;
    it->session = id;
    // parameter: the two ASCII characters of the server vendor id in the upper half
    send(s, kAsyncInitializeResponse, 0, (quint32('Q') << 24) | (quint32('D') << 16));
    Q_EMIT sessionsChanged(int(m_sessions.size()));
    return;
  }

  if (kind == Kind::Unknown)
  {
    sendFatal(s, 2, QStringLiteral("Channel not initialized"));
    return;
  }
  auto sit = m_sessions.find(sid);
  if (sit == m_sessions.end())
  {
    sendFatal(s, 0, QStringLiteral("Unknown session"));
    return;
  }
  if (kind == Kind::Sync)
    handleSync(s, *sit, type, ctrl, param, payload);
  else
    handleAsync(s, *sit, type, ctrl, param, payload);
}

void HislipServer::handleSync(QTcpSocket *s, Session &sess, quint8 type, quint8 ctrl, quint32 param,
                              const QByteArray &payload)
{
  Q_UNUSED(ctrl)
  if (!sess.async) // both channels must stand before data flows
  {
    sendFatal(s, 2, QStringLiteral("Asynchronous channel not established"));
    return;
  }

  switch (type)
  {
    case kData:
    case kDataEnd:
    {
      // a message in many pieces is no bigger than one in a single piece
      if (quint64(sess.msgBuf.size()) + quint64(payload.size()) > HislipServer::kMaxMessage)
      {
        sess.msgBuf.clear();
        sendFatal(s, 0, QStringLiteral("Message too large"));
        return;
      }
      sess.msgBuf += payload;
      if (type == kData)
        break;
      const QByteArray msg = sess.msgBuf;
      sess.msgBuf.clear();
      // HiSLIP streams: every response goes out at once, tagged with the message id of the request
      const QList<QByteArray> responses = m_ep.run(msg);
      for (const QByteArray &response : responses)
        sendData(s, sess, response, param);
      break;
    }

    case kTrigger:
      qCDebug(lcHislip) << "Trigger";
      break;

    case kDeviceClearComplete: // second half of the device clear handshake (synchronous channel)
      sess.msgBuf.clear();
      send(s, kDeviceClearAcknowledge, 0, 0); // feature bitmap 0 = synchronized mode
      break;

    default:
      sendError(s, 1, QStringLiteral("Unrecognized message type on synchronous channel"));
      break;
  }
}

void HislipServer::handleAsync(QTcpSocket *s, Session &sess, quint8 type, quint8 ctrl, quint32 param,
                               const QByteArray &payload)
{
  Q_UNUSED(param)

  switch (type)
  {
    case kAsyncStatusQuery:
      // control code of the response = status byte
      send(s, kAsyncStatusResponse, m_ep.stb(), 0);
      break;

    case kAsyncMaximumMessageSize:
    {
      if (payload.size() != 8)
      {
        sendError(s, 0, QStringLiteral("Bad AsyncMaximumMessageSize payload"));
        break;
      }
      sess.clientMaxMsg = qFromBigEndian<quint64>(payload.constData());
      char b[8];
      qToBigEndian(HislipServer::kMaxMessage, b);
      send(s, kAsyncMaximumMessageSizeResponse, 0, 0, QByteArray(b, 8));
      break;
    }

    case kAsyncDeviceClear: // first half of the device clear handshake
      sess.msgBuf.clear();
      if (m_ep.deviceClear)
        m_ep.deviceClear();
      send(s, kAsyncDeviceClearAcknowledge, 0, 0); // feature bitmap 0 = synchronized mode
      break;

    case kAsyncLock:
      // locks are not implemented: request -> 0 (not granted), release -> 3 (error, nothing locked)
      send(s, kAsyncLockResponse, ctrl == 1 ? 0 : 3, 0);
      break;

    case kAsyncLockInfo:
      send(s, kAsyncLockInfoResponse, 0, 0); // no exclusive lock, 0 shared locks
      break;

    case kAsyncRemoteLocalControl:
      send(s, kAsyncRemoteLocalResponse, 0, 0);
      break;

    default:
      sendError(s, 1, QStringLiteral("Unrecognized message type on asynchronous channel"));
      break;
  }
}
