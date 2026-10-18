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
#include "service/hislipserver.h"
#include "service/mdnsresponder.h"
#include "recording/recordingstore.h"
#include "service/scpiserver.h"
#include "service/sharedstatemanager.h"
#include "core/siprefix.h"

namespace
{
// --vxi11-rpc: replaces the setting Scpi/vxi11-rpc for this run
std::optional<Vxi11Server::RpcbindMode> s_vxi11RpcOverride;
// --vxi11-portmapper-port: an own portmapper on this port for this run (tests)
std::optional<quint16> s_vxi11PortmapperPort;
}


MeterController::MeterController(QObject *parent)
  : QObject(parent)
  , m_dmm(new MeterConnection(this))
  , m_alarms(new AlarmManager(this))
  , m_recorder(new RecordingStore(this))
  , m_scpi(new ScpiServer(this))
  , m_mdns(new MdnsResponder(this))
  , m_hislip(new HislipServer(m_scpi->endpoint(), this))
  , m_mdnsHislip(new MdnsResponder(this))
  , m_vxi11(new Vxi11Server(m_scpi->endpoint(), this))
  , m_mdnsVxi11(new MdnsResponder(this))
  , m_external(new QProcess(this))
{
  qRegisterMetaType<Reading>();
  connect(m_dmm, &MeterConnection::response, this, &MeterController::responseSLOT);
  connect(m_dmm, &MeterConnection::error, this, &MeterController::error);
  // SCPI's "connected" (INPut?, STAT:QUES? bit 2) is the link itself: lost
  // and reconnecting (a Bluetooth meter switched off) is not connected
  connect(m_dmm, &MeterConnection::linkStateChanged, this, [this](MeterConnection::LinkState state)
  {
    m_scpi->setConnected(state == MeterConnection::LinkState::Connected);
  });

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

  // What a SCPI command starts in the UI runs after the command: a dialog it
  // opens (the length of a recording, a port without access) must not spin an
  // event loop inside the server, where the client may go away meanwhile. The
  // checks stay at once, so the error queue answers the next command; they
  // count what is checked but not run yet, and the run checks once more.
  connect(m_scpi, &ScpiServer::startRecording, this, [this]
  {
    QString why;
    if (m_startPending)
      why = tr("already starting, the recording goes on");
    if (!m_startPending && mayStartRecording(&why))
    {
      m_startPending = true;
      QMetaObject::invokeMethod(this, [this]
      {
        QString why;
        if (mayStartRecording(&why))
          Q_EMIT recordingRequested(true);   // may ask the length: pending until it returns
        else
          Q_EMIT info(tr("SCPI: %1").arg(why));
        m_startPending = false;
      }, Qt::QueuedConnection);
    }
    else
    {
      m_scpi->pushError(-213, "Init ignored");
      Q_EMIT info(tr("SCPI: %1").arg(why));
    }
  });
  connect(m_scpi, &ScpiServer::stopRecording, this, [this]
  {
    QMetaObject::invokeMethod(this, [this] { Q_EMIT recordingRequested(false); }, Qt::QueuedConnection);
  });
  connect(m_scpi, &ScpiServer::connectRequested, this, [this](bool on)
  {
    // INPut OFF also ends a connection that is lost and being reopened
    if (on == m_connectPending.value_or(m_dmm->isWanted()))
      return;
    m_connectPending = on;
    QMetaObject::invokeMethod(this, [this, on]
    {
      if (on != m_dmm->isWanted())
        Q_EMIT connectRequested(on);   // may show a dialog: pending until it returns
      if (m_connectPending == on)
        m_connectPending.reset();
    }, Qt::QueuedConnection);
  });
  connect(m_scpi, &ScpiServer::resetMinMaxRequested, this, &MeterController::resetMinMax);
  connect(m_scpi, &ScpiServer::acknowledgeAlarmsRequested, this, &MeterController::acknowledgeAlarms);
  connect(m_scpi, &ScpiServer::markRequested, this, [this](const QString &text)
  {
    Q_EMIT markRequested(QColor(0x1e, 0x90, 0xff), text, true, true);
  });
  installScpiSources();
  connect(m_scpi, &ScpiServer::clientsChanged, this, [this](int) { updateScpiStatus(); });
  connect(m_hislip, &HislipServer::sessionsChanged, this, [this](int) { updateScpiStatus(); });
  connect(m_vxi11, &Vxi11Server::linksChanged, this, [this](int) { updateScpiStatus(); });

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
  // a "no readings" alarm watches a connected meter, so its clock starts here
  m_alarms->setConnected(m_dmm->isOpen(), QDateTime::currentMSecsSinceEpoch());
  return ok;
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

  // min/max follow their value (setMinMaxValue()); a held display is the
  // old reading again, so it does not count
  bool newMin = false;
  bool newMax = false;
  if (id == 0 && !hold)
  {
    if (m_lastUnit != unit)
      Q_EMIT unitChanged(unit);
    m_lastUnit = unit;
  }
  if (id == m_minMaxId && !hold)
  {
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
    if (m_baseUnit != baseUnit)
    {
      m_baseUnit = baseUnit;
      Q_EMIT baseUnitChanged(baseUnit);
    }

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

  // the alarms on this value
  if (!baseUnit.isEmpty())
    m_valueUnits[id] = baseUnit;
  m_valueOverload[id] = overload;
  m_alarms->feed(dval, overload, now, id);

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

bool MeterController::mayStartRecording(QString *why) const
{
  QString reason;
  if (m_recorder->state() == RecordingStore::Record)
    reason = tr("already recording, the recording goes on");
  else if (m_recorder->state() == RecordingStore::View && m_recorder->dirty())
    reason = tr("start ignored, the recording shown is not saved yet");
  if (why)
    *why = reason;
  return reason.isEmpty();
}

void MeterController::setMinMaxValue(int id)
{
  if (id == m_minMaxId)
    return;
  m_minMaxId = id;
  resetMinMax();
}

void MeterController::resetMinMax()
{
  m_minMax.clear();
  Q_EMIT minMaxReset();
}

void MeterController::forgetUnit()
{
  m_lastUnit.clear();
  m_baseUnit.clear();
  m_valueUnits.clear();
  Q_EMIT unitChanged(QString());
  Q_EMIT baseUnitChanged(QString());
}

// ---------------------------------------------------------------- alarms

QString MeterController::valueUnit(int id) const
{
  return id == 0 ? m_baseUnit : m_valueUnits.value(id);
}

void MeterController::onAlarmRaised(int, const Alarm &alarm, double value)
{
  const QString unit = valueUnit(alarm.value);
  const bool overload = m_valueOverload.value(alarm.value);
  const QString shown = overload ? QStringLiteral("OL") : EngNumberValidator::engValue(value) + unit;
  const QString text = alarm.message.isEmpty() ? alarm.describe(unit) : alarm.message;
  Q_EMIT error(tr("%1: %2 (%3)").arg(Alarm::title(alarm.name), text, shown));

  QString why;
  if (alarm.recorder == Alarm::RecorderStart && mayStartRecording(&why))
    Q_EMIT recordingRequested(true);
  else if (alarm.recorder == Alarm::RecorderStart)
    Q_EMIT info(tr("%1: %2").arg(Alarm::title(alarm.name), why));
  else if (alarm.recorder == Alarm::RecorderStop)
    Q_EMIT recordingRequested(false);
  if (alarm.markGraph || alarm.markTable)
    Q_EMIT markRequested(alarm.color, Alarm::title(alarm.name), alarm.markGraph, alarm.markTable);
  if (!alarm.command.isEmpty())
  {
    QString cmd = alarm.command;
    cmd.replace("%v", EngNumberValidator::engText(value)).replace("%u", unit).replace("%n", alarm.name);
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
    lines << QString("%1: %2").arg(al.name, al.message.isEmpty() ? al.describe(valueUnit(al.value)) : al.message);
    if (!color.isValid())
      color = al.color;
  }
  Q_EMIT alarmBannerChanged(lines.join('\n'), color);

  // the display: the newest raised alarm that colours it, acknowledged or not
  const QColor alert = m_alarms->displayColor();
  if (alert != m_alertColor)
  {
    m_alertColor = alert;
    Q_EMIT alertColorChanged(alert);
  }
}

// ---------------------------------------------------------------- SCPI

// What the QTDMM: and CALCulate queries read: the min/max memory, the
// recording, the alarms, the meter's keys and the other instances
void MeterController::installScpiSources()
{
  ScpiServer::Sources s;
  s.stats = [this]
  {
    ScpiServer::Stats st;
    if (!m_minMax.isEmpty())
    {
      st.count = m_minMax.count();
      st.min = m_minMax.minimum();
      st.max = m_minMax.maximum();
      st.mean = m_minMax.mean();
    }
    return st;
  };
  s.recording = [this]
  {
    ScpiServer::RecordingState r;
    r.count = m_recorder->count();
    switch (m_recorder->state())
    {
      case RecordingStore::Live:   r.mode = ScpiServer::RecordingState::Live; break;
      case RecordingStore::Record: r.mode = ScpiServer::RecordingState::Record; break;
      default: r.mode = r.count ? ScpiServer::RecordingState::View : ScpiServer::RecordingState::Idle;
    }
    r.seconds = m_recorder->duration() / 1000.0;
    r.unit = m_recorder->unit();
    return r;
  };
  s.recordingData = [this](int count)
  {
    QList<ScpiServer::Point> points;
    const RecordingSeries &series = m_recorder->series();
    for (int i = std::max(0, series.count() - count); i < series.count(); ++i)
    {
      const RawPoint &p = series.at(i);
      points.append({ p.t / 1000.0, p.value, p.quality == Quality::Overload });
    }
    return points;
  };
  s.alarms = [this]
  {
    QList<ScpiServer::AlarmState> alarms;
    const QList<Alarm> &list = m_alarms->alarms();
    for (int i = 0; i < list.size(); ++i)
      alarms.append({ list[i].name, ScpiServer::AlarmState::Level(m_alarms->state(i)) });
    return alarms;
  };
  s.keys = [this] { return m_dmm->keys(); };
  s.pressKey = [this](const QString &key) { return pressKey(key); };
  s.instances = [this]
  {
    QList<ScpiServer::Instance> list;
    if (!m_stateMgr)
      return list;
    const auto readings = m_stateMgr->readings();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const QString &id : m_stateMgr->instances())
    {
      ScpiServer::Instance in;
      in.id = id;
      const auto it = readings.constFind(id);
      if (it != readings.constEnd())
      {
        in.value = it->value;
        in.unit = it->unit;
        in.valid = it->valid && now - it->msecs <= ScpiServer::kStaleMs;
      }
      list << in;
    }
    return list;
  };
  m_scpi->setSources(std::move(s));
}

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
    m_scpiError.clear();
  }
  else if (!same)
  {
    m_mdns->stop();
    m_scpiError.clear();
    if (!m_scpi->start(address, config.port))
    {
      m_scpiError = m_scpi->errorString();
      Q_EMIT error(tr("SCPI server: %1").arg(m_scpiError));
    }
  }

  // HiSLIP: the same interpreter for VISA, next to the raw socket; another
  // instance takes the next port, as for the raw socket
  const bool hislip = config.enabled && config.hislip && m_scpi->isListening();
  const bool sameHislip = m_hislip->isListening() && m_hislip->address() == address
                          && m_hislip->port() >= config.hislipPort && m_hislip->port() < config.hislipPort + 10;
  if (!hislip)
  {
    m_mdnsHislip->stop();
    m_hislip->stop();
    m_hislipError = config.enabled && config.hislip ? tr("the SCPI server is not running") : QString();
  }
  else if (!sameHislip)
  {
    m_mdnsHislip->stop();
    m_hislipError.clear();
    if (!m_hislip->start(address, config.hislipPort, 10))
    {
      m_hislipError = m_hislip->error();
      Q_EMIT error(tr("HiSLIP server: %1").arg(m_hislipError));
    }
  }

  // VXI-11: one per computer (one portmapper entry), with the system rpcbind
  // or an own portmapper on 111; switched on but not possible, it stays off
  // with a reason, the other two run on
  const bool vxi11 = config.enabled && config.vxi11 && m_scpi->isListening();
  const Vxi11Server::RpcbindMode rpc =
    s_vxi11PortmapperPort ? Vxi11Server::RpcbindMode::Own : s_vxi11RpcOverride.value_or(config.vxi11Rpc);
  const bool sameVxi11 = m_vxi11->isRunning() && m_vxi11->address() == address && m_vxi11->mode() == rpc;
  if (!vxi11)
  {
    m_mdnsVxi11->stop();
    m_vxi11->stop();
    m_vxi11Error = config.enabled && config.vxi11 ? tr("the SCPI server is not running") : QString();
  }
  else if (!sameVxi11)
  {
    m_mdnsVxi11->stop();
    m_vxi11Error.clear();
    if (!m_vxi11->start(address, rpc, 0, 0, s_vxi11PortmapperPort.value_or(Vxi11Server::kPortmapperPort)))
    {
      m_vxi11Error = m_vxi11->error();
      Q_EMIT error(tr("VXI-11 server: %1").arg(m_vxi11Error));
    }
  }

  QMap<QString, QString> txt;
  txt["txtvers"] = "1";
  txt["model"] = m_model;
  txt["version"] = APP_VERSION;
  txt["instance"] = m_instanceId;
  const QString instance = QString("QtDMM %1").arg(m_instanceId == "default"
                                                   ? QHostInfo::localHostName() : m_instanceId);
  if (m_scpi->isListening() && config.mdns && !m_mdns->isActive())
    m_mdns->start("_scpi-raw._tcp", instance, m_scpi->port(), txt);
  else if (!config.mdns)
    m_mdns->stop();
  if (m_hislip->isListening() && config.mdns && !m_mdnsHislip->isActive())
    m_mdnsHislip->start("_hislip._tcp", instance, m_hislip->port(), txt);
  else if (!config.mdns)
    m_mdnsHislip->stop();
  // VXI-11 clients ask the portmapper: the announced port is 111
  if (m_vxi11->isRunning() && config.mdns && !m_mdnsVxi11->isActive())
    m_mdnsVxi11->start("_vxi-11._tcp", instance, Vxi11Server::kPortmapperPort, txt);
  else if (!config.mdns)
    m_mdnsVxi11->stop();
  updateScpiStatus();
}

void MeterController::updateScpiStatus()
{
  if (!m_scpi->isListening())
  {
    Q_EMIT scpiStatusChanged(m_scpiError.isEmpty() ? tr("The server is not running.")
                                                   : tr("The server could not start: %1.").arg(m_scpiError));
    return;
  }
  const QString where = m_scpi->address() == QHostAddress::LocalHost ? QString("localhost") : QHostInfo::localHostName();
  const int n = m_scpi->clientCount() + m_hislip->sessionCount() + m_vxi11->linkCount();
  const QString clients = n == 1 ? tr("1 client") : tr("%1 clients").arg(n);
  QString text = tr("Listening on %1, port %2, %3 connected.").arg(where).arg(m_scpi->port()).arg(clients);
  if (m_hislip->isListening())
    text += ' ' + tr("HiSLIP on port %1: %2.").arg(m_hislip->port()).arg(hislipResource());
  else if (!m_hislipError.isEmpty())
    text += ' ' + tr("HiSLIP could not start: %1.").arg(m_hislipError);
  if (m_vxi11->isRunning())
    text += ' ' + (m_vxi11->usesSystemRpcbind() ? tr("VXI-11 through the system rpcbind: %1.")
                                                : tr("VXI-11 with an own portmapper on port %2: %1."))
                    .arg(vxi11Resource()).arg(m_vxi11->portmapperPort());
  else if (!m_vxi11Error.isEmpty())
    text += ' ' + tr("VXI-11 is off: %1.").arg(m_vxi11Error);
  if (m_mdns->isActive())
    text += ' ' + tr("Announced as \"%1\".").arg(m_mdns->instanceName());
  Q_EMIT scpiStatusChanged(text);
}

QList<ServiceState> MeterController::services() const
{
  ServiceState scpi;
  scpi.id = "scpi";
  scpi.name = "SCPI";
  scpi.running = m_scpi->isListening();
  if (scpi.running)
  {
    const QString where = m_scpi->address() == QHostAddress::LocalHost ? QString("localhost") : QHostInfo::localHostName();
    scpi.port = m_scpi->port();
    scpi.address = QString("%1:%2").arg(where).arg(scpi.port);
    scpi.clients = m_scpi->clientCount();
  }
  else
    scpi.error = m_scpiError;
  ServiceState hislip;
  hislip.id = "hislip";
  hislip.name = "HiSLIP";
  hislip.running = m_hislip->isListening();
  if (hislip.running)
  {
    hislip.port = m_hislip->port();
    hislip.address = hislipResource();
    hislip.clients = m_hislip->sessionCount();
  }
  else
    hislip.error = m_hislipError;
  ServiceState vxi11;
  vxi11.id = "vxi11";
  vxi11.name = "VXI-11";
  vxi11.running = m_vxi11->isRunning();
  if (vxi11.running)
  {
    vxi11.port = m_vxi11->portmapperPort();
    vxi11.address = vxi11Resource();
    vxi11.clients = m_vxi11->linkCount();
  }
  else
    vxi11.error = m_vxi11Error;
  return { scpi, hislip, vxi11 };
}

QString MeterController::vxi11Resource() const
{
  if (!m_vxi11->isRunning())
    return QString();
  const QString host = m_vxi11->address() == QHostAddress::LocalHost ? QString("localhost") : QHostInfo::localHostName();
  return QString("TCPIP::%1::INSTR").arg(host);
}

void MeterController::setVxi11RpcOverride(std::optional<Vxi11Server::RpcbindMode> mode)
{
  s_vxi11RpcOverride = mode;
}

void MeterController::setVxi11PortmapperPort(std::optional<quint16> port)
{
  s_vxi11PortmapperPort = port;
}

QString MeterController::hislipResource() const
{
  if (!m_hislip->isListening())
    return QString();
  const QString host = m_hislip->address() == QHostAddress::LocalHost ? QString("localhost") : QHostInfo::localHostName();
  // the port only when it is not the standard one
  if (m_hislip->port() == HislipServer::kDefaultPort)
    return QString("TCPIP::%1::hislip0::INSTR").arg(host);
  return QString("TCPIP::%1::hislip0,%2::INSTR").arg(host).arg(m_hislip->port());
}

