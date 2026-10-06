// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "service/metercontroller.h"

#include <QDateTime>
#include <QHostInfo>
#include <QProcess>
#include <QTimer>
#include <QRegularExpression>

#include "device/meterconnection.h"
#include "ui/engnumbervalidator.h"
#include "service/mdnsresponder.h"
#include "recording/recordingstore.h"
#include "service/scpiserver.h"
#include "service/sharedstatemanager.h"
#include "core/siprefix.h"


MeterController::MeterController(QObject *parent)
  : QObject(parent)
  , m_dmm(new MeterConnection(this))
  , m_alarms(new AlarmManager(this))
  , m_recorder(new RecordingStore(this))
  , m_scpi(new ScpiServer(this))
  , m_mdns(new MdnsResponder(this))
  , m_external(new QProcess(this))
{
  qRegisterMetaType<Reading>();
  connect(m_dmm, &MeterConnection::response, this, &MeterController::responseSLOT);
  connect(m_dmm, &MeterConnection::error, this, &MeterController::error);

  // the recorder keeps every reading of the main value, and a gap where it
  // went stale
  connect(this, &MeterController::reading, m_recorder, &RecordingStore::setReading);
  connect(this, &MeterController::staleChanged, m_recorder, &RecordingStore::setStale);
  connect(m_recorder, &RecordingStore::runningChanged, this, &MeterController::setRecording);
  // another function at the meter ended the recording: say so, and mark
  // where in the graph
  connect(m_recorder, &RecordingStore::functionChanged, this, [this](const QString &from, const QString &to)
  {
    Q_EMIT error(tr("Recording stopped: the meter measures %2 now, not %1").arg(from, to));
    Q_EMIT markRequested(QColor(0xff, 0x8c, 0x00), tr("Function changed"), true, false);
  });

  connect(m_alarms, &AlarmManager::raised, this, &MeterController::onAlarmRaised);
  connect(m_alarms, &AlarmManager::cleared, this, [this](int, const Alarm &alarm)
  {
    Q_EMIT info(tr("%1 cleared").arg(Alarm::title(alarm.name)));
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

  // an alarm's program that had the port to itself has ended: the meter again
  connect(m_external, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus)
  {
    Q_EMIT info(tr("%1 ended with exit code %2").arg(m_external->program()).arg(exitCode));
    if (m_reopenAfter)
    {
      m_reopenAfter = false;
      Q_EMIT portReleased(false);
    }
  });

  m_clock.start();
  startTimer(100);   // the stale check and the alarms' time base
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
  m_stale.reset();   // a new connection has its own rhythm
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

bool MeterController::pressKey(const QString &key)
{
  return m_dmm->sendKey(key);
}

void MeterController::setRecording(bool on)
{
  m_scpi->setRecording(on);
}

void MeterController::timerEvent(QTimerEvent *)
{
  // a value older than the meter's rhythm allows is no value any more
  // (kern_spezifikation §4.3): the recorder gets a gap, not the old value
  const qint64 t = m_clock.elapsed();
  m_recorder->setStaleAfter(int(m_stale.maxAgeMs()));
  const bool stale = m_stale.stale(t);
  if (stale != m_staleShown)
  {
    m_staleShown = stale;
    Q_EMIT staleChanged(stale);
  }
  m_alarms->tick(QDateTime::currentMSecsSinceEpoch());
}

void MeterController::responseSLOT(const DmmDecoder::DmmResponse &response)
{
  // the one place the decoders' strings are read is ReadingAdapter
  m_adapter.setFormat(m_dmm->format());
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  for (const PortSample &ps : m_adapter.adapt(response, now))
    publish(ReadingAdapter::reading(ps));
}

void MeterController::publish(const Reading &rd)
{
  const double dval = rd.value;
  const QString &val = rd.text;
  const QString &unit = rd.unit;
  const bool hold = rd.hold;
  const int id = rd.id;
  const qint64 now = rd.msecs;
  const bool overload = rd.overload;
  const QString &baseUnit = rd.baseUnit;

  // the meter's secondary display is off (an empty second value): the views
  // clear it, but it is no reading - not logged, not published
  if (id > 0 && val.isEmpty())
  {
    Q_EMIT reading(rd);
    return;
  }

  // min/max follow the main value; a held display is the old reading
  // again, so it does not count
  bool newMin = false;
  bool newMax = false;
  if (id == 0 && !hold)
  {
    if (m_lastUnit != unit)
      Q_EMIT unitChanged(unit);
    m_lastUnit = unit;
    // kept across a range change (mV -> V), afresh on another port
    const MinMaxMemory::Result mm = m_minMax.feed(rd);
    if (mm.reset)
      Q_EMIT minMaxReset();
    newMin = mm.newMin;
    newMax = mm.newMax;
  }

  Q_EMIT reading(rd);
  if (newMax)
    Q_EMIT maximumChanged(m_minMax.maximum(), val, unit);
  if (newMin)
    Q_EMIT minimumChanged(m_minMax.minimum(), val, unit);

  if (id == 0)
  {
    m_stale.arrived(m_clock.elapsed());
    m_overload = overload;
    m_baseUnit = baseUnit;
    m_alarms->feed(dval, overload, now);

    // let the other instances see this value (calculated values, P = U * I)
    if (m_stateMgr)
    {
      SharedStateManager::Reading r;
      r.value = dval;
      r.unit = baseUnit;
      r.port = rd.port.toString();
      r.msecs = now;
      r.valid = !hold && !overload;
      m_stateMgr->publishReading(r);
    }
  }

  ScpiServer::Reading r;
  r.value = dval;
  r.unit = baseUnit;
  r.port = rd.port;
  r.range = rd.range;
  r.hold = hold;
  r.overload = overload;
  r.valid = true;
  r.msecs = now;
  m_scpi->setReading(id, r);
}

void MeterController::resetMinMax()
{
  m_minMax.clear();
  Q_EMIT minMaxReset();
}

// ---------------------------------------------------------------- alarms

void MeterController::onAlarmRaised(int, const Alarm &alarm, double value)
{
  const QString shown = m_overload ? QStringLiteral("OL") : EngNumberValidator::engValue(value) + m_baseUnit;
  const QString text = alarm.message.isEmpty() ? alarm.describe(m_baseUnit) : alarm.message;
  Q_EMIT error(tr("%1: %2 (%3)").arg(Alarm::title(alarm.name), text, shown));

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
      if (!alarm.disconnect)
      {
        if (!QProcess::startDetached(program, args))
          Q_EMIT error(tr("%1: could not run %2").arg(Alarm::title(alarm.name), program));
      }
      else if (m_externalPending || m_external->state() != QProcess::NotRunning)
        Q_EMIT error(tr("%1: %2 is still running").arg(Alarm::title(alarm.name), m_external->program()));
      else
      {
        // "Disconnect first": the program has the port to itself, the meter
        // comes back when it has ended. Not here: the alarm raised while the
        // reader is still on the frame of the port that would close
        const QString title = Alarm::title(alarm.name);
        m_externalPending = true;   // a second alarm in the meantime waits for it
        m_external->setProgram(program);
        QTimer::singleShot(0, this, [this, title, program, args]
        {
          m_externalPending = false;
          m_reopenAfter = m_dmm->isOpen();
          if (m_reopenAfter)
          {
            // the recording goes on: no readings while the program runs
            m_recorder->setStale(true);
            Q_EMIT portReleased(true);
          }
          m_external->start(program, args);
          if (!m_external->waitForStarted(3000))
          {
            Q_EMIT error(tr("%1: could not run %2").arg(title, program));
            if (m_reopenAfter)
            {
              m_reopenAfter = false;
              Q_EMIT portReleased(false);
            }
          }
        });
      }
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

