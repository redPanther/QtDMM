// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later

#include "service/devicesession.h"

#include <QDateTime>

#include "core/devicelibrary.h"
#include "core/sampletypes.h"
#include "device/meterconnection.h"
#include "device/metersetup.h"
#include "recording/recordingstore.h"
#include "service/metercontroller.h"
#include "service/scpiserver.h"
#include "service/servicehost.h"

DeviceSession::DeviceSession(QObject *parent)
  : QObject(parent)
  , m_host(new ServiceHost(this))
{
  installScpi();
}

void DeviceSession::setConfigDir(const QString &dir)
{
  delete m_library;
  m_library = new DeviceLibrary(dir, this);
}

void DeviceSession::adopt(MeterController *ctl)
{
  m_adopted = ctl;
  ctl->dmm()->setCalcInputs(calcInputs());
  connect(ctl->recorder(), &RecordingStore::stateChanged, this, &DeviceSession::followWindow);
  // cleared while it records: a new start, the others too
  connect(ctl->recorder(), &RecordingStore::cleared, this, [this]
  {
    if (m_adopted && m_adopted->recorder()->isRunning())
      for (MeterController *other : std::as_const(m_opened))
        if (other->recorder()->isRunning())
          other->recorder()->clear();
  });
}

void DeviceSession::followWindow(int state)
{
  if (!m_adopted)
    return;
  RecordingStore *lead = m_adopted->recorder();
  if (state == RecordingStore::Record)
  {
    // its start stays where it is, also when its first reading comes later:
    // the others record on it. Alone it begins with its first reading
    if (!m_opened.isEmpty())
      lead->holdOrigin();
    for (MeterController *ctl : std::as_const(m_opened))
      startFollower(ctl, lead->startClock(), lead->startDateTime(), lead->sampleTime());
  }
  else if (state == RecordingStore::Live)
    // live with it: a start with the pre-trigger finds their readings too
    for (MeterController *ctl : std::as_const(m_opened))
      liveFollower(ctl);
  else
    for (MeterController *ctl : std::as_const(m_opened))
      ctl->recorder()->stop();
}

void DeviceSession::liveFollower(MeterController *ctl)
{
  warnUnsaved(ctl);
  ctl->recorder()->live();
}

void DeviceSession::warnUnsaved(MeterController *ctl)
{
  const RecordingStore *store = ctl->recorder();
  if (store->state() == RecordingStore::View && store->dirty())
    Q_EMIT error(tr("%1: the recording was not saved, the new one replaces it").arg(ctl->deviceName()));
}

void DeviceSession::startFollower(MeterController *ctl, qint64 t0, const QDateTime &start, int sampleTime)
{
  RecordingStore *store = ctl->recorder();
  warnUnsaved(ctl);
  if (sampleTime > 0)
    store->setSampleTime(sampleTime);
  store->startAt(t0, start);
}

void DeviceSession::onRecordingRequested(bool start)
{
  if (m_adopted)
  {
    // the window's starts all, as its Record button does
    QString why;
    if (start && !m_adopted->mayStartRecording(&why))
    {
      m_host->scpi()->pushError(-213, "Init ignored");
      Q_EMIT error(tr("%1: %2").arg(m_adopted->deviceName().isEmpty() ? tr("Window") : m_adopted->deviceName(), why));
      return;
    }
    m_adopted->requestRecording(start);
    return;
  }
  // no window: those here, from now on
  const qint64 t0 = Sample::now();
  const QDateTime wall = QDateTime::currentDateTime();
  for (MeterController *other : std::as_const(m_opened))
    if (!start)
      other->recorder()->stop();
    else if (!other->recorder()->isRunning())
      startFollower(other, t0, wall, 0);
}

QString DeviceSession::variableName(const QString &name)
{
  QString out = name.toLower();
  for (QChar &c : out)
    if (!c.isLetterOrNumber() && c != '_')
      c = '_';
  // a name begins with a letter: "34465A" is _34465a
  return !out.isEmpty() && out[0].isDigit() ? '_' + out : out;
}

CalcDevice::Inputs DeviceSession::calcInputs() const
{
  // a calculated meter may outlive the session by a deleteLater()
  return [self = QPointer<const DeviceSession>(this)]
  {
    QMap<QString, CalcDevice::Input> out;
    if (!self)
      return out;
    for (const MeterController *ctl : self->controllers())
    {
      const Reading &r = ctl->lastReading();
      if (ctl->deviceName().isEmpty() || r.msecs == 0)
        continue;
      out.insert(variableName(ctl->deviceName()), { r.value, r.baseUnit, r.msecs, !r.hold && !r.overload && !r.noNumber() });
    }
    return out;
  };
}

MeterController *DeviceSession::open(const QString &id, QString *error)
{
  const std::optional<MyDevice> entry = m_library ? m_library->find(id) : std::nullopt;
  if (!entry)
  {
    if (error)
      *error = "-224,Illegal parameter value; no such device: " + id;
    return nullptr;
  }
  return open(entry->id, entry->keys, entry->name, error);
}

MeterController *DeviceSession::open(const QString &id, const QVariantMap &keys, const QString &name, QString *error)
{
  if (controller("device:" + id))
  {
    if (error)
      *error = "-221,Settings conflict; open already: " + name;
    return nullptr;
  }
  const MeterSetup setup = MeterSetup::fromKeys(keys, m_sigrokExe);
  auto *ctl = new MeterController(this, m_host);
  ctl->setInstanceId(m_instanceId);
  ctl->setDevice(id, name);
  ctl->setModel(setup.knownModel ? setup.info.name : QStringLiteral("Manual"));
  ctl->setValueCount(setup.valueCount);
  ctl->setFollowsWindow(true);
  ctl->configure(setup);
  ctl->dmm()->setCalcInputs(calcInputs());
  // its messages to the status bar: a lost connection, a recording stopped
  connect(ctl, &MeterController::error, this, [this, ctl](const QString &text)
  {
    Q_EMIT this->error(tr("%1: %2").arg(ctl->deviceName(), text));
  });
  connect(ctl, &MeterController::info, this, [this, ctl](const QString &text)
  {
    Q_EMIT info(tr("%1: %2").arg(ctl->deviceName(), text));
  });
  // no window to ask: INPut ON|OFF goes to the connection
  connect(ctl, &MeterController::connectRequested, ctl, [ctl](bool on) { ctl->connectMeter(on); });
  connect(ctl, &MeterController::recordingRequested, this, &DeviceSession::onRecordingRequested);
  if (!ctl->connectMeter(true))
  {
    // SCPI in English, the reason in the status bar in the user's language
    if (error)
      *error = "-240,Hardware error; " + name + ": does not connect";
    Q_EMIT this->error(tr("%1: %2").arg(name, ctl->dmm()->errorString()));
    delete ctl;
    return nullptr;
  }
  m_opened << ctl;
  // opened while the window records: it joins, on the same time base
  if (m_adopted && m_adopted->recorder()->isRunning())
  {
    RecordingStore *lead = m_adopted->recorder();
    lead->holdOrigin();
    startFollower(ctl, lead->startClock(), lead->startDateTime(), lead->sampleTime());
  }
  else if (!m_adopted || m_adopted->recorder()->state() == RecordingStore::Live)
    ctl->recorder()->live();
  Q_EMIT opened(ctl);
  return ctl;
}

bool DeviceSession::close(const QString &id, QString *error)
{
  const QString source = "device:" + id;
  for (MeterController *ctl : std::as_const(m_opened))
    if (ctl->source() == source)
    {
      m_opened.removeOne(ctl);
      ctl->connectMeter(false);
      delete ctl;
      Q_EMIT closed(source);
      return true;
    }
  if (error)
    *error = m_adopted && m_adopted->source() == source ? QString("-221,Settings conflict; the window's device")
                                                        : "-224,Illegal parameter value; not open: " + id;
  return false;
}

void DeviceSession::release(const QString &id)
{
  if (id.isEmpty())
    return;
  if (MeterController *ctl = controller("device:" + id); ctl && ctl != m_adopted)
  {
    const QString name = ctl->deviceName();
    close(id);
    Q_EMIT error(tr("%1 closed in the background: the window uses it now").arg(name));
  }
}

QList<MeterController *> DeviceSession::controllers() const
{
  QList<MeterController *> all;
  if (m_adopted)
    all << m_adopted.data();
  return all + m_opened;
}

MeterController *DeviceSession::controller(const QString &source) const
{
  for (MeterController *ctl : controllers())
    if (ctl->source() == source)
      return ctl;
  return nullptr;
}

MeterController *DeviceSession::controller(const ValueRef &value) const
{
  return value.source.isEmpty() ? m_adopted.data() : controller(value.source);
}

void DeviceSession::installScpi()
{
  ScpiServer::DeviceControl control;
  control.catalog = [this]
  {
    QList<ScpiServer::DeviceEntry> out;
    if (m_library)
      for (const MyDevice &d : m_library->list())
        out << ScpiServer::DeviceEntry { d.id, d.name, controller("device:" + d.id) != nullptr };
    return out;
  };
  control.open = [this](const QString &id)
  {
    QString error;
    open(id, &error);
    return error;
  };
  control.close = [this](const QString &id)
  {
    QString error;
    close(id, &error);
    return error;
  };
  m_host->scpi()->setDeviceControl(control);
}
