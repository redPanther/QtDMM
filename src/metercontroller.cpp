// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "metercontroller.h"

#include <QDateTime>
#include <QHostInfo>
#include <QProcess>
#include <QRegularExpression>

#include "dmm.h"
#include "engnumbervalidator.h"
#include "mdnsresponder.h"
#include "scpiserver.h"
#include "sharedstatemanager.h"
#include "siprefix.h"


MeterController::MeterController(QObject *parent)
  : QObject(parent)
  , m_dmm(new DMM(this))
  , m_alarms(new AlarmManager(this))
  , m_scpi(new ScpiServer(this))
  , m_mdns(new MdnsResponder(this))
  , m_external(new QProcess(this))
{
  qRegisterMetaType<Reading>();
  connect(m_dmm, &DMM::value, this, &MeterController::valueSLOT);
  connect(m_dmm, &DMM::error, this, &MeterController::error);

  connect(m_alarms, &AlarmManager::raised, this, &MeterController::onAlarmRaised);
  connect(m_alarms, &AlarmManager::cleared, this, [this](int, const Alarm &alarm)
  {
    Q_EMIT info(tr("Alarm %1 cleared").arg(alarm.name));
    updateBanner();
  });

  connect(m_scpi, &ScpiServer::startRecording, this, [this] { Q_EMIT recordingRequested(true); });
  connect(m_scpi, &ScpiServer::stopRecording, this, [this] { Q_EMIT recordingRequested(false); });
  connect(m_scpi, &ScpiServer::connectRequested, this, [this](bool on)
  {
    if (on != m_dmm->isOpen())
      Q_EMIT connectRequested(on);
  });
  connect(m_scpi, &ScpiServer::clientsChanged, this, [this](int) { updateScpiStatus(); });

  connect(m_external, &QProcess::finished, this, [this](int, QProcess::ExitStatus status)
  {
    Q_EMIT externalFinished(int(status));
  });

  startTimer(100);   // recorder sample clock and alarm time base
}

MeterController::~MeterController() = default;

void MeterController::setStateManager(SharedStateManager *mgr)
{
  m_stateMgr = mgr;
  m_dmm->setStateManager(mgr);
}

void MeterController::setModel(const QString &model)
{
  m_model = model;
  m_dmm->setName(model);
  m_scpi->setModel(model);
}

void MeterController::setAlarms(const QList<Alarm> &alarms)
{
  m_alarms->setAlarms(alarms);
  updateBanner();
}

bool MeterController::connectMeter(bool on)
{
  bool ok = true;
  if (on)
    ok = m_dmm->open();
  else
    m_dmm->close();
  m_scpi->setConnected(m_dmm->isOpen());
  // a "no readings" alarm watches a connected meter, so its clock starts here
  m_alarms->setConnected(m_dmm->isOpen(), QDateTime::currentMSecsSinceEpoch());
  return ok;
}

bool MeterController::isConnected() const
{
  return m_dmm->isOpen();
}

void MeterController::setRecording(bool on)
{
  m_scpi->setRecording(on);
}

void MeterController::timerEvent(QTimerEvent *)
{
  Q_EMIT sample(m_dval);
  m_alarms->tick(QDateTime::currentMSecsSinceEpoch());
}

void MeterController::valueSLOT(double dval, const QString &val, const QString &unit, const QString &special,
                                const QString &range, bool hold, bool showBar, int id)
{
  // the one place the display strings are interpreted: decoders mark
  // overload and similar states with letters in the value text ("OL",
  // "-OL", "EFLO"), a number never has one
  static const QRegularExpression letters("[A-Za-z]");
  Reading rd;
  rd.value = dval;
  rd.text = val;
  rd.unit = unit;
  const SiPrefix::Split split = SiPrefix::split(unit);
  rd.prefix = split.prefix;
  rd.baseUnit = split.baseUnit;
  rd.special = special;
  rd.range = range;
  rd.hold = hold;
  rd.showBar = showBar;
  rd.overload = val.contains(letters);
  rd.id = id;
  rd.msecs = QDateTime::currentMSecsSinceEpoch();
  const qint64 now = rd.msecs;
  const bool overload = rd.overload;
  const QString &baseUnit = rd.baseUnit;

  // min/max and the sampled value follow the main value; a held display is
  // the old reading again, so it does not count
  bool newMin = false;
  bool newMax = false;
  if (id == 0 && !hold)
  {
    if (m_lastUnit != unit)
    {
      resetMinMax();
      Q_EMIT unitChanged(unit);
    }
    m_lastUnit = unit;
    if (dval > m_max)
    {
      m_max = dval;
      newMax = true;
    }
    if (dval < m_min)
    {
      m_min = dval;
      newMin = true;
    }
    m_dval = dval;
  }

  Q_EMIT reading(rd);
  if (newMax)
    Q_EMIT maximumChanged(m_max, val, unit);
  if (newMin)
    Q_EMIT minimumChanged(m_min, val, unit);

  if (id == 0)
  {
    m_overload = overload;
    m_baseUnit = baseUnit;
    m_alarms->feed(dval, overload, now);

    // let the other instances see this value (calculated values, P = U * I)
    if (m_stateMgr)
    {
      SharedStateManager::Reading r;
      r.value = dval;
      r.unit = baseUnit;
      r.special = special;
      r.msecs = now;
      r.valid = !hold && !overload;
      m_stateMgr->publishReading(r);
    }
  }

  ScpiServer::Reading r;
  r.value = dval;
  r.unit = baseUnit;
  r.special = special;
  r.range = range;
  r.hold = hold;
  r.overload = overload;
  r.valid = true;
  r.msecs = now;
  m_scpi->setReading(id, r);
}

void MeterController::resetMinMax()
{
  m_min = 1.0E20;
  m_max = -1.0E20;
  Q_EMIT minMaxReset();
}

// ---------------------------------------------------------------- alarms

void MeterController::onAlarmRaised(int, const Alarm &alarm, double value)
{
  const QString shown = m_overload ? QStringLiteral("OL") : EngNumberValidator::engValue(value) + m_baseUnit;
  const QString text = alarm.message.isEmpty() ? alarm.describe(m_baseUnit) : alarm.message;
  Q_EMIT error(tr("Alarm %1: %2 (%3)").arg(alarm.name, text, shown));

  if (alarm.recorder == Alarm::RecorderStart)
    Q_EMIT recordingRequested(true);
  else if (alarm.recorder == Alarm::RecorderStop)
    Q_EMIT recordingRequested(false);
  if (alarm.markGraph || alarm.markTable)
    Q_EMIT markRequested(alarm.color, alarm.name, alarm.markGraph, alarm.markTable);
  if (!alarm.command.isEmpty())
  {
    QString cmd = alarm.command;
    cmd.replace("%v", EngNumberValidator::engText(value)).replace("%u", m_baseUnit).replace("%n", alarm.name);
    QStringList args = QProcess::splitCommand(cmd);
    if (!args.isEmpty())
    {
      const QString program = args.takeFirst();
      if (!QProcess::startDetached(program, args))
        Q_EMIT error(tr("Alarm %1: could not run %2").arg(alarm.name, program));
    }
  }
  Q_EMIT alarmRaised(alarm, shown, text);
  updateBanner();
}

void MeterController::acknowledgeAlarms()
{
  m_alarms->acknowledgeAll();
  updateBanner();
}

// One line per raised alarm (acknowledged ones are silent), on the colour
// of the first one.
void MeterController::updateBanner()
{
  QStringList lines;
  QColor color;
  const QList<Alarm> &list = m_alarms->alarms();
  for (int i = 0; i < list.size(); ++i)
  {
    if (m_alarms->state(i) != AlarmManager::Raised || !list[i].banner)
      continue;
    const Alarm &al = list[i];
    lines << QString("%1: %2").arg(al.name, al.message.isEmpty() ? al.describe(m_baseUnit) : al.message);
    if (!color.isValid())
      color = al.color;
  }
  Q_EMIT alarmBannerChanged(lines.join('\n'), color);
}

// ---------------------------------------------------------------- SCPI

void MeterController::setScreenshotSource(std::function<QByteArray(const QByteArray &)> source)
{
  m_scpi->setScreenshotSource(std::move(source));
}

void MeterController::applyScpi(const ScpiConfig &config)
{
  const QHostAddress address = config.allInterfaces ? QHostAddress::Any : QHostAddress::LocalHost;
  // keep a running server when nothing about it changed: clients stay
  const bool same = m_scpi->isListening() && m_scpi->address() == address
                    && m_scpi->port() >= config.port && m_scpi->port() < config.port + 10;
  if (!config.enabled)
  {
    m_mdns->stop();
    m_scpi->stop();
  }
  else if (!same)
  {
    m_mdns->stop();
    if (!m_scpi->start(address, config.port))
      Q_EMIT error(tr("SCPI server: %1").arg(m_scpi->errorString()));
  }
  if (m_scpi->isListening() && config.mdns && !m_mdns->isActive())
  {
    QMap<QString, QString> txt;
    txt["txtvers"] = "1";
    txt["model"] = m_model;
    txt["version"] = APP_VERSION;
    txt["instance"] = m_instanceId;
    const QString instance = QString("QtDMM %1").arg(m_instanceId == "default"
                                                     ? QHostInfo::localHostName() : m_instanceId);
    m_mdns->start("_scpi-raw._tcp", instance, m_scpi->port(), txt);
  }
  else if (!config.mdns)
    m_mdns->stop();
  updateScpiStatus();
}

void MeterController::updateScpiStatus()
{
  if (!m_scpi->isListening())
  {
    Q_EMIT scpiStatusChanged(QString(), tr("The server is not running."));
    return;
  }
  const QString where = m_scpi->address() == QHostAddress::LocalHost ? QString("localhost") : QHostInfo::localHostName();
  const int n = m_scpi->clientCount();
  const QString clients = n == 1 ? tr("1 client") : tr("%1 clients").arg(n);
  QString text = tr("Listening on %1, port %2, %3 connected.").arg(where).arg(m_scpi->port()).arg(clients);
  if (m_mdns->isActive())
    text += ' ' + tr("Announced as \"%1\".").arg(m_mdns->instanceName());
  Q_EMIT scpiStatusChanged(tr("SCPI %1:%2 (%3)").arg(where).arg(m_scpi->port()).arg(clients), text);
}

// ---------------------------------------------------------------- external program

bool MeterController::startExternal(const QString &command)
{
  // the command is passed as the only argument of an empty program, as it
  // always was: QProcess then runs it through the platform's rules
  m_external->setArguments({command});
  m_external->start();
  return m_external->state() == QProcess::Starting;
}

bool MeterController::externalRunning() const
{
  return m_external->state() == QProcess::Running;
}

void MeterController::killExternal()
{
  m_external->kill();
  m_external->waitForFinished(1000);
}
