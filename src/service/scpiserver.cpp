// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "service/scpiserver.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcScpi, "qtdmm.scpi", QtWarningMsg)

namespace
{
// SCPI-99 8.2: +INF stands for overload, NaN for "no measurement"
const char *kOverload = "9.9E+37";
const char *kNotANumber = "9.91E+37";
constexpr int kMaxErrors = 20;
}

ScpiServer::ScpiServer(QObject *parent) : QObject(parent), m_server(new QTcpServer(this))
{
  connect(m_server, &QTcpServer::newConnection, this, &ScpiServer::onNewConnection);
}

ScpiServer::~ScpiServer()
{
  stop();
}

bool ScpiServer::start(const QHostAddress &address, quint16 port, int tries)
{
  stop();
  for (int i = 0; i < tries && port + i <= 65535; ++i)
  {
    if (m_server->listen(address, quint16(port + i)))
    {
      m_error.clear();
      qCDebug(lcScpi) << "listening on" << address.toString() << m_server->serverPort();
      return true;
    }
    m_error = m_server->errorString();
    if (m_server->serverError() != QAbstractSocket::AddressInUseError)
      break;
  }
  return false;
}

void ScpiServer::stop()
{
  if (m_server->isListening())
    m_server->close();
  for (QTcpSocket *s : m_clients)
  {
    s->disconnect(this);
    s->close();
    release(s);
  }
  const bool had = !m_clients.isEmpty();
  m_clients.clear();
  if (had)
    Q_EMIT clientsChanged(0);
}

bool ScpiServer::isListening() const
{
  return m_server->isListening();
}

quint16 ScpiServer::port() const
{
  return m_server->serverPort();
}

QHostAddress ScpiServer::address() const
{
  return m_server->serverAddress();
}

void ScpiServer::setReading(int id, const Reading &reading)
{
  if (id >= 0 && id < 2)
    m_readings[id] = reading;
}

void ScpiServer::onNewConnection()
{
  while (QTcpSocket *s = m_server->nextPendingConnection())
  {
    m_clients << s;
    connect(s, &QTcpSocket::readyRead, this, [this, s]() { onReadyRead(s); });
    connect(s, &QTcpSocket::disconnected, this, [this, s]()
    {
      m_clients.removeAll(s);
      release(s);
      Q_EMIT clientsChanged(m_clients.size());
    });
    qCDebug(lcScpi) << "client" << s->peerAddress().toString();
    Q_EMIT clientsChanged(m_clients.size());
  }
}

void ScpiServer::onReadyRead(QTcpSocket *socket)
{
  // a client that never sends a terminator must not grow our buffer forever
  if (socket->bytesAvailable() > 65536 && !socket->canReadLine())
  {
    socket->readAll();
    pushError(-363, "Input buffer overrun");
    return;
  }
  while (socket->canReadLine())
  {
    QByteArray line = socket->readLine();
    while (line.endsWith('\n') || line.endsWith('\r'))
      line.chop(1);
    ++m_running;
    const QByteArray response = process(line);
    if (--m_running == 0)
    {
      for (QTcpSocket *gone : std::as_const(m_released))
        gone->deleteLater();
      m_released.clear();
    }
    // the client may have gone while the command ran
    if (!m_clients.contains(socket))
      return;
    if (!response.isEmpty())
      socket->write(response);
  }
}

void ScpiServer::release(QTcpSocket *socket)
{
  if (m_running > 0)
    m_released << socket;
  else
    socket->deleteLater();
}

// ---------------------------------------------------------------------------

// SCPI mnemonics match their long form or the short form (the upper case
// letters of the long form, e.g. "MEASure" -> MEAS); case does not matter.
bool ScpiServer::matches(const QString &mnemonic, const char *longForm)
{
  const QString lf = QString::fromLatin1(longForm);
  QString shortForm;
  for (const QChar c : lf)
    if (c.isUpper() || c.isDigit())
      shortForm += c;
  const QString m = mnemonic.toUpper();
  return m == lf.toUpper() || m == shortForm;
}

QString ScpiServer::number(double v)
{
  if (std::isnan(v))
    return kNotANumber;
  if (std::isinf(v))
    return v > 0 ? kOverload : "-9.9E+37";
  return QString::asprintf("%+.6E", v);
}

// The SCPI function name behind the meter's mode and unit, for CONF?.
QString ScpiServer::function(const Reading &r) const
{
  const bool ac = r.port.defining & SampleFlag::AC;
  switch (r.port.quantity)
  {
    case Quantity::Voltage:
      if (r.port.defining & SampleFlag::Diode)
        return "DIOD";
      return ac ? "VOLT:AC" : "VOLT:DC";
    case Quantity::Current:     return ac ? "CURR:AC" : "CURR:DC";
    case Quantity::Resistance:  return "RES";
    case Quantity::Continuity:  return "CONT";
    case Quantity::Temperature: return "TEMP";
    case Quantity::Capacitance: return "CAP";
    case Quantity::Frequency:   return "FREQ";
    case Quantity::DutyCycle:   return "PER";
    case Quantity::Power:       return "POW";
    case Quantity::PulseWidth:
    case Quantity::Time:        return "PWID";
    default:
      return r.unit.isEmpty() ? QString("NONE") : r.unit.toUpper();
  }
}

const ScpiServer::Reading *ScpiServer::reading(int channel)
{
  if (channel < 1 || channel > 2)
  {
    pushError(-114, "Header suffix out of range");
    return nullptr;
  }
  return &m_readings[channel - 1];
}

void ScpiServer::pushError(int code, const QString &text)
{
  if (m_errors.size() >= kMaxErrors)
  {
    m_errors.last() = qMakePair(-350, QString("Queue overflow"));
    return;
  }
  m_errors << qMakePair(code, text);
}

QByteArray ScpiServer::block(const QByteArray &data)
{
  const QByteArray len = QByteArray::number(data.size());
  return '#' + QByteArray::number(len.size()) + len + data;
}

ScpiEndpoint ScpiServer::endpoint()
{
  ScpiEndpoint ep;
  ep.process = [this](const QByteArray &line) { return process(line); };
  ep.statusByte = [this]() { return statusByte(); };
  return ep;
}

QByteArray ScpiServer::process(const QByteArray &message)
{
  QList<QByteArray> answers;
  bool any = false;
  // program message: units separated by ';'; a unit starting with ':' or
  // '*' is absolute, otherwise it continues at the previous unit's level
  const QString text = QString::fromUtf8(message).trimmed();
  if (text.isEmpty())
    return {};
  QStringList units;
  {
    QString cur;
    bool quoted = false;
    for (const QChar c : text)
    {
      if (c == '"')
        quoted = !quoted;
      if (c == ';' && !quoted)
      {
        units << cur;
        cur.clear();
      }
      else
        cur += c;
    }
    units << cur;
  }

  QStringList prefix;
  for (const QString &unitRaw : units)
  {
    QString unit = unitRaw.trimmed();
    if (unit.isEmpty())
      continue;
    Command cmd;
    const int space = unit.indexOf(' ');
    QString header = space < 0 ? unit : unit.left(space);
    cmd.args = space < 0 ? QString() : unit.mid(space + 1).trimmed();
    if (header.endsWith('?'))
    {
      cmd.query = true;
      header.chop(1);
    }
    bool absolute = header.startsWith(':') || header.startsWith('*');
    if (header.startsWith(':'))
      header.remove(0, 1);
    QStringList path = absolute || prefix.isEmpty() ? QStringList() : prefix;
    static const QRegularExpression mnemonicRe("^([A-Za-z*_]+)(\\d*)$");
    bool ok = true;
    for (const QString &part : header.split(':'))
    {
      const QRegularExpressionMatch m = mnemonicRe.match(part);
      if (!m.hasMatch())
      {
        ok = false;
        break;
      }
      path << m.captured(1).toUpper();
      cmd.suffix << (m.captured(2).isEmpty() ? 1 : m.captured(2).toInt());
    }
    if (!ok || path.isEmpty())
    {
      pushError(-100, "Command error");
      continue;
    }
    // the suffix list covers the inherited prefix, too
    while (cmd.suffix.size() < path.size())
      cmd.suffix.prepend(1);
    cmd.path = path;
    if (!path.first().startsWith('*'))
      prefix = path.mid(0, path.size() - 1);

    bool isQuery = false;
    const QByteArray answer = handle(cmd, isQuery);
    if (isQuery)
    {
      answers << answer;
      any = true;
    }
  }
  if (!any)
    return {};
  QByteArray out;
  for (const QByteArray &a : answers)
    out += (out.isEmpty() ? QByteArray() : QByteArray(";")) + a;
  return out + '\n';
}

// The screen dump is binary, everything else is text: HCOPy:SDUMp:DATA?
// (and Rigol's DISPlay:DATA?) is answered here, the rest in handleText().
QByteArray ScpiServer::handle(const Command &cmd, bool &isQuery)
{
  const QString head = cmd.path.first();
  const int depth = cmd.path.size();
  const bool hcopy = matches(head, "HCOPy") && depth >= 3 && matches(cmd.path[1], "SDUMp") && matches(cmd.path[2], "DATA");
  const bool dispData = matches(head, "DISPlay") && depth == 2 && matches(cmd.path[1], "DATA");
  if ((hcopy && depth == 3) || dispData)
  {
    isQuery = cmd.query;
    if (!cmd.query)
    {
      pushError(-100, "Command error");
      isQuery = false;
      return {};
    }
    const QByteArray image = m_screenshot ? m_screenshot(m_screenshotFormat) : QByteArray();
    if (image.isEmpty())
    {
      pushError(-240, "Hardware error; no screen to dump");
      isQuery = false;
      return {};
    }
    return block(image);
  }
  if (hcopy && depth == 4 && matches(cmd.path[3], "FORMat"))
  {
    isQuery = cmd.query;
    if (cmd.query)
      return m_screenshotFormat;
    const QByteArray f = cmd.args.trimmed().toUpper().toLatin1();
    if (f == "PNG" || f == "BMP" || f == "JPG" || f == "JPEG")
      m_screenshotFormat = f == "JPEG" ? QByteArray("JPG") : f;
    else
      pushError(f.isEmpty() ? -109 : -224, f.isEmpty() ? "Missing parameter" : "Illegal parameter value");
    return {};
  }
  if (matches(head, "QTDMM") && depth == 3 && matches(cmd.path[1], "RECord") && matches(cmd.path[2], "DATA"))
    return recordingData(cmd, isQuery);
  return handleText(cmd, isQuery).toUtf8();
}

// QTDMM:RECord:DATA? [<n>|MAX]: the last n readings of the graph (100 when
// none is given, at most kMaxDataPoints) as CSV in a definite-length block -
// "seconds,value" lines, an overload 9.9E+37, a gap nan
QByteArray ScpiServer::recordingData(const Command &cmd, bool &isQuery)
{
  isQuery = cmd.query;
  if (!cmd.query)
  {
    pushError(-100, "Command error");
    isQuery = false;
    return {};
  }
  int count = 100;
  const QString arg = cmd.args.trimmed();
  if (matches(arg, "MAXimum"))
    count = kMaxDataPoints;
  else if (!arg.isEmpty())
  {
    bool ok = false;
    const double n = arg.toDouble(&ok);
    if (!ok)
    {
      pushError(-104, "Data type error");
      isQuery = false;
      return {};
    }
    if (n < 1 || n != std::floor(n))
    {
      pushError(-222, "Data out of range");
      isQuery = false;
      return {};
    }
    count = int(std::min(n, double(kMaxDataPoints)));
  }
  QByteArray csv = "seconds,value\n";
  if (m_sources.recordingData)
    for (const Point &p : m_sources.recordingData(count))
    {
      const QByteArray value = p.overload ? QByteArray(kOverload)
                               : std::isnan(p.value) ? QByteArray("nan") : number(p.value).toLatin1();
      csv += QByteArray::number(p.seconds, 'f', 3) + ',' + value + '\n';
    }
  return block(csv);
}

QString ScpiServer::unquote(const QString &arg)
{
  QString s = arg.trimmed();
  if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front())
  {
    const QChar q = s.front();
    s = s.mid(1, s.size() - 2);
    s.replace(QString(2, q), QString(q));
  }
  return s;
}

namespace
{
QString quoted(const QString &s)
{
  return '"' + QString(s).replace('"', "\"\"") + '"';
}
}

// The commands of QtDMM's own: what a bench meter does not have - the
// recording, the alarms, marks, the meter's keys and the other instances
QString ScpiServer::handleQtdmm(const Command &cmd, bool &isQuery)
{
  const int depth = cmd.path.size();
  const QString sub = depth > 1 ? cmd.path[1] : QString();
  const QString leaf = depth > 2 ? cmd.path[2] : QString();
  isQuery = cmd.query;
  auto fail = [this, &isQuery](int code, const QString &text)
  {
    pushError(code, text);
    isQuery = false;
    return QString();
  };
  auto wantQuery = [&]() { return cmd.query || (fail(-100, "Command error"), false); };
  auto wantCommand = [&]() { return !cmd.query || (fail(-100, "Command error"), false); };

  // QTDMM:RECord? - IDLE|LIVE|REC|VIEW,<seconds>,<readings>,"<unit>"
  if (matches(sub, "RECord") && depth == 2)
  {
    if (!wantQuery())
      return {};
    const RecordingState r = m_sources.recording ? m_sources.recording() : RecordingState();
    static const char *const modes[] = { "IDLE", "LIVE", "REC", "VIEW" };
    return QString("%1,%2,%3,%4").arg(modes[r.mode]).arg(r.seconds, 0, 'f', 3).arg(r.count).arg(quoted(r.unit));
  }

  // QTDMM:ALARm? the alarms raised (acknowledged ones too), :LIST? each
  // with its state, :ACKnowledge as the banner's button
  if (matches(sub, "ALARm") && depth <= 3)
  {
    const QList<AlarmState> alarms = m_sources.alarms ? m_sources.alarms() : QList<AlarmState>();
    if (depth == 2)
    {
      if (!wantQuery())
        return {};
      int raised = 0;
      for (const AlarmState &a : alarms)
        raised += a.level >= AlarmState::Raised;
      return QString::number(raised);
    }
    if (matches(leaf, "LIST"))
    {
      if (!wantQuery())
        return {};
      static const char *const levels[] = { "OFF", "PEND", "RAIS", "ACKN" };
      QStringList out;
      for (const AlarmState &a : alarms)
        out << quoted(a.name) << levels[a.level];
      return out.isEmpty() ? QString("\"\"") : out.join(',');
    }
    if (matches(leaf, "ACKnowledge"))
    {
      if (wantCommand())
        Q_EMIT acknowledgeAlarmsRequested();
      return {};
    }
    return fail(-113, "Undefined header");
  }

  // QTDMM:MARK "text": a mark at the newest reading
  if (matches(sub, "MARK") && depth == 2)
  {
    if (wantCommand())
    {
      const QString text = unquote(cmd.args);
      Q_EMIT markRequested(text.isEmpty() ? QStringLiteral("SCPI") : text);
    }
    return {};
  }

  // QTDMM:KEY "hold" presses a key of the meter, QTDMM:KEY:CATalog? lists them
  if (matches(sub, "KEY") && depth <= 3)
  {
    const QStringList keys = m_sources.keys ? m_sources.keys() : QStringList();
    if (depth == 3)
    {
      if (!matches(leaf, "CATalog"))
        return fail(-113, "Undefined header");
      if (!wantQuery())
        return {};
      QStringList out;
      for (const QString &k : keys)
        out << quoted(k.toUpper());
      return out.isEmpty() ? QString("\"\"") : out.join(',');
    }
    if (!wantCommand())
      return {};
    const QString key = unquote(cmd.args).toLower();
    if (key.isEmpty())
      return fail(-109, "Missing parameter");
    if (keys.isEmpty())
      return fail(-241, "Hardware missing; the meter has no keys QtDMM can press");
    if (!keys.contains(key))
      return fail(-224, "Illegal parameter value");
    if (!m_sources.pressKey || !m_sources.pressKey(key))
      return fail(-240, "Hardware error; the key was not sent");
    return {};
  }

  // QTDMM:INSTances? - "<id>",<value>,"<unit>" for each running instance
  if (matches(sub, "INSTances") && depth == 2)
  {
    if (!wantQuery())
      return {};
    QStringList out;
    if (m_sources.instances)
      for (const Instance &i : m_sources.instances())
        out << quoted(i.id) << (i.valid ? number(i.value) : QString(kNotANumber)) << quoted(i.unit);
    return out.isEmpty() ? QString("\"\"") : out.join(',');
  }

  return fail(-113, "Undefined header");
}

QString ScpiServer::handleText(const Command &cmd, bool &isQuery)
{
  const QString head = cmd.path.first();
  const int depth = cmd.path.size();
  isQuery = cmd.query;
  auto undefined = [this, &isQuery]()
  {
    pushError(-113, "Undefined header");
    isQuery = false;
    return QString();
  };
  auto queryOnly = [this, &cmd, &isQuery]()
  {
    if (!cmd.query)
    {
      pushError(-100, "Command error");
      isQuery = false;
      return false;
    }
    return true;
  };
  auto noQuery = [this, &cmd, &isQuery]()
  {
    if (cmd.query)
    {
      pushError(-100, "Command error");
      isQuery = false;
      return false;
    }
    return true;
  };

  // IEEE 488.2 common commands
  if (head.startsWith('*'))
  {
    if (depth != 1)
      return undefined();
    if (head == "*IDN")
      return queryOnly() ? QString("QtDMM,%1,0,%2").arg(m_model.isEmpty() ? QString("no meter") : m_model,
                                                         QString::fromLatin1(APP_VERSION)) : QString();
    if (head == "*RST" || head == "*CLS")
    {
      if (noQuery())
        m_errors.clear();
      return {};
    }
    if (head == "*OPC")
      return cmd.query ? QString("1") : QString();
    if (head == "*WAI")
      return noQuery() ? QString() : QString();
    if (head == "*TST")
      return queryOnly() ? QString("0") : QString();
    if (head == "*STB")
      return queryOnly() ? QString::number(statusByte()) : QString();
    if (head == "*ESR" || head == "*ESE" || head == "*SRE")
    {
      if (!cmd.query && (head == "*ESE" || head == "*SRE"))
        return {};
      return queryOnly() ? QString("0") : QString();
    }
    return undefined();
  }

  // readings: READ? FETCh? MEASure? [SENSe:]DATA? VALue1?/VALue2?  - a
  // numeric suffix picks the main (1) or second (2) value
  const bool isRead = matches(head, "READ") || matches(head, "FETCh") || matches(head, "MEASure")
                      || matches(head, "VALue") || matches(head, "DATA")
                      || (matches(head, "SENSe") && depth == 2 && matches(cmd.path[1], "DATA"));
  if (isRead && depth <= 2)
  {
    if (!queryOnly())
      return {};
    // MEASure:VOLTage:DC? style sub-functions are accepted and ignored:
    // the meter decides what it measures
    const int channel = cmd.suffix.last() != 1 ? cmd.suffix.last() : cmd.suffix.first();
    const Reading *r = reading(channel);
    if (!r)
    {
      isQuery = false;
      return {};
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!r->valid || !m_connected || now - r->msecs > kStaleMs)
    {
      pushError(-230, "Data corrupt or stale");
      return kNotANumber;
    }
    if (r->overload)
      return number(std::signbit(r->value) ? -INFINITY : INFINITY);   // -OL: a negative NaN
    return number(r->value);
  }
  if (matches(head, "MEASure") && depth > 2)
  {
    // MEASure:VOLTage:DC? etc.
    if (!queryOnly())
      return {};
    const Reading *r = reading(cmd.suffix.first());
    if (!r)
    {
      isQuery = false;
      return {};
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!r->valid || !m_connected || now - r->msecs > kStaleMs)
    {
      pushError(-230, "Data corrupt or stale");
      return kNotANumber;
    }
    return r->overload ? number(std::signbit(r->value) ? -INFINITY : INFINITY) : number(r->value);
  }

  if (matches(head, "UNIT") && depth == 1)
  {
    if (!queryOnly())
      return {};
    const Reading *r = reading(cmd.suffix.first());
    if (!r)
    {
      isQuery = false;
      return {};
    }
    return QString("\"%1\"").arg(r->unit);
  }

  if (matches(head, "CONFigure") && depth == 1)
  {
    if (!queryOnly())
      return {};
    const Reading *r = reading(cmd.suffix.first());
    if (!r)
    {
      isQuery = false;
      return {};
    }
    QString range = r->range.toUpper() == "AUTO" ? QString("AUTO") : r->range;
    if (range.isEmpty() || range.toUpper() == "MANU")
      range = "MANUAL";
    return QString("\"%1 %2\"").arg(function(*r), range);
  }

  // recorder and connection
  if (matches(head, "INITiate") && depth <= 2)
  {
    if (noQuery())
      Q_EMIT startRecording();
    return {};
  }
  if (matches(head, "ABORt") && depth == 1)
  {
    if (noQuery())
      Q_EMIT stopRecording();
    return {};
  }
  if (matches(head, "INPut") && (depth == 1 || (depth == 2 && matches(cmd.path[1], "STATe"))))
  {
    if (cmd.query)
      return m_connected ? "1" : "0";
    const QString a = cmd.args.toUpper();
    if (a == "ON" || a == "1")
      Q_EMIT connectRequested(true);
    else if (a == "OFF" || a == "0")
      Q_EMIT connectRequested(false);
    else
      pushError(a.isEmpty() ? -109 : -224, a.isEmpty() ? "Missing parameter" : "Illegal parameter value");
    return {};
  }

  // CALCulate:AVERage: the min/max memory, as on Keysight's bench meters
  if (matches(head, "CALCulate") && depth == 3 && matches(cmd.path[1], "AVERage"))
  {
    const QString what = cmd.path[2];
    if (matches(what, "CLEar"))
    {
      if (noQuery())
        Q_EMIT resetMinMaxRequested();
      return {};
    }
    const bool min = matches(what, "MINimum"), max = matches(what, "MAXimum");
    const bool mean = matches(what, "AVERage"), count = matches(what, "COUNt");
    if (!min && !max && !mean && !count)
      return undefined();
    if (!queryOnly())
      return {};
    const Stats s = m_sources.stats ? m_sources.stats() : Stats();
    if (count)
      return QString::number(s.count);
    if (s.count == 0)
      return kNotANumber;
    return number(min ? s.min : max ? s.max : s.mean);
  }

  if (matches(head, "QTDMM"))
    return handleQtdmm(cmd, isQuery);

  // STATus
  if (matches(head, "STATus") && depth >= 2)
  {
    if (!queryOnly())
      return {};
    const bool cond = depth == 2 || (depth == 3 && (matches(cmd.path[2], "CONDition") || matches(cmd.path[2], "EVENt")));
    if (!cond)
      return undefined();
    if (matches(cmd.path[1], "QUEStionable"))
    {
      // bit 0 overload, bit 1 hold, bit 2 meter not connected, bit 3 no
      // current reading; an old reading's overload or hold is gone
      int bits = 0;
      const Reading &r = m_readings[0];
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      const bool current = r.valid && now - r.msecs <= kStaleMs;
      if (current && r.overload) bits |= 1;
      if (current && r.hold) bits |= 2;
      if (!m_connected) bits |= 4;
      if (!current) bits |= 8;
      return QString::number(bits);
    }
    if (matches(cmd.path[1], "OPERation"))
    {
      // SCPI bit 4 = measuring: the recorder runs; bit 9 (QtDMM's own) an
      // alarm is raised, acknowledged or not
      int bits = m_recording ? 16 : 0;
      if (m_sources.alarms)
        for (const AlarmState &a : m_sources.alarms())
          if (a.level >= AlarmState::Raised)
            bits |= 512;
      return QString::number(bits);
    }
    return undefined();
  }

  // SYSTem
  if (matches(head, "SYSTem") && depth >= 2)
  {
    if (matches(cmd.path[1], "ERRor"))
    {
      if (!queryOnly())
        return {};
      if (depth == 3 && matches(cmd.path[2], "COUNt"))
        return QString::number(m_errors.size());
      if (depth == 3 && !matches(cmd.path[2], "NEXT"))
        return undefined();
      if (m_errors.isEmpty())
        return "0,\"No error\"";
      const auto e = m_errors.takeFirst();
      return QString("%1,\"%2\"").arg(e.first).arg(e.second);
    }
    if (matches(cmd.path[1], "VERSion") && depth == 2)
      return queryOnly() ? QString("1999.0") : QString();
    return undefined();
  }

  return undefined();
}
