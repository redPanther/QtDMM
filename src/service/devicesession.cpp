// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later

#include "service/devicesession.h"

#include "core/devicelibrary.h"
#include "device/meterconnection.h"
#include "device/metersetup.h"
#include "service/metercontroller.h"
#include "service/scpiserver.h"
#include "service/servicehost.h"
#include "service/sharedstatemanager.h"

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
  return [this]
  {
    QMap<QString, CalcDevice::Input> out;
    if (m_stateMgr)
    {
      const auto readings = m_stateMgr->readings();
      for (auto it = readings.cbegin(); it != readings.cend(); ++it)
        out.insert(it.key(), { it->value, it->unit, it->msecs, it->valid });
    }
    for (const MeterController *ctl : controllers())
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
  ctl->setRecordingAllowed(false);
  ctl->configure(setup);
  ctl->dmm()->setCalcInputs(calcInputs());
  // no window to ask: INPut ON|OFF goes to the connection
  connect(ctl, &MeterController::connectRequested, ctl, [ctl](bool on) { ctl->connectMeter(on); });
  if (!ctl->connectMeter(true))
  {
    if (error)
      *error = "-240,Hardware error; " + name + ": " + ctl->dmm()->errorString();
    delete ctl;
    return nullptr;
  }
  m_opened << ctl;
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
