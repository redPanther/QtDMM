// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/devicelibrary.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QThread>
#include <QUuid>
#include <algorithm>

namespace
{
const QString kPrefix = QStringLiteral("device-");

// the keys of an entry that are its own, not the meter's
bool ownKey(const QString &key)
{
  return key == QLatin1String("name") || key == QLatin1String("order");
}

std::function<QString(const QString &)> g_modelTransport;

// "SERIAL /dev/ttyUSB0" -> "serial"; a port without type is a serial port
QString deviceType(const QString &device)
{
  const QString first = device.section(' ', 0, 0).toLower();
  if (device.trimmed().isEmpty())
    return QString();
  if (!device.contains(' ') && !first.startsWith(QLatin1String("ble")))
    return QStringLiteral("serial");
  return first;
}

// "/dev/serial/by-id/usb-..." and "/dev/ttyUSB0" are one port while the
// cable is plugged in: the file the name stands for
QString portFile(const QString &port)
{
  const QString canonical = QFileInfo(port).canonicalFilePath();
  return canonical.isEmpty() ? port : canonical;
}
}

void DeviceLibrary::setModelTransport(std::function<QString(const QString &)> transport)
{
  g_modelTransport = std::move(transport);
}

QVariantMap DeviceLibrary::entryKeys(const QVariantMap &keys)
{
  QStringList wanted { "DMM/model", "DMM/data-format", "DMM/display", "DMM/number-of-values", "Port settings/device" };
  const QString device = keys.value("Port settings/device").toString();
  const QString model = keys.value("DMM/model").toString();
  const QString byModel = g_modelTransport ? g_modelTransport(model) : QString();
  const QString type = byModel.isEmpty() ? deviceType(device) : byModel;
  if (type == QLatin1String("ble"))
    wanted << "Port settings/ble-address" << "Port settings/ble-key" << "Port settings/ble-main"
           << "Port settings/ble-second";
  else if (type == QLatin1String("blegatt"))
    wanted << "Port settings/ble-address";
  else if (type == QLatin1String("sigrok"))
    wanted << "Port settings/sigrok-conn" << "Port settings/sigrok-options";
  else if (type == QLatin1String("calc") && model.endsWith(QLatin1String("Virtual meter")))
  {
    for (auto it = keys.cbegin(); it != keys.cend(); ++it)
      if (it.key().startsWith(QLatin1String("DMM/virtual-")))
        wanted << it.key();
  }
  else if (type == QLatin1String("calc"))
    wanted << "DMM/calc-unit" << "DMM/calc-expression";
  else
    wanted << "Port settings/baud" << "Port settings/bits" << "Port settings/parity" << "Port settings/stop-bits"
           << "DMM/rts" << "DMM/dtr" << "DMM/external-setup";
  QVariantMap out;
  for (const QString &key : wanted)
    if (keys.contains(key))
      out.insert(key, keys.value(key));
  return out;
}

QString DeviceLibrary::place(const QVariantMap &keys)
{
  const QString device = keys.value("Port settings/device").toString().trimmed();
  const QString type = deviceType(device);
  const QString rest = device.contains(' ') ? device.section(' ', 1).trimmed() : device;
  // the simulated and calculated meters are nowhere; entries from before
  // 26.2 kept the port of the meter before ("serial ..." of the UT61E)
  if (keys.value("DMM/model").toString().startsWith(QLatin1String("QtDMM ")) && type != QLatin1String("calc"))
    return QString();
  if (type == QLatin1String("serial"))
    return "serial " + portFile(rest);
  if (type == QLatin1String("hid") || type == QLatin1String("hidholtek"))
    return "hid " + rest.section(' ', 0, 0).toLower();
  if (type == QLatin1String("ble") || type == QLatin1String("blegatt") || type.isEmpty())
  {
    // the address in the device string, else the one of a find
    QString address = rest.section(' ', 0, 0);
    if (type.isEmpty() || address.isEmpty())
      address = keys.value("Port settings/ble-address").toString().section(' ', 0, 0);
    return address.isEmpty() ? QString() : "ble " + address.toUpper();
  }
  if (type == QLatin1String("sigrok"))
    return "sigrok " + keys.value("Port settings/sigrok-conn", rest).toString();
  return type + ' ' + rest;
}

QString DeviceLibrary::findByPlace(const QVariantMap &keys) const
{
  const QString where = place(keys);
  if (where.isEmpty())
    return QString();
  QStringList same;
  for (const MyDevice &d : list())
    if (place(d.keys) == where)
    {
      // two cables of one type: the path tells them apart
      if (d.keys.value("Port settings/device") == keys.value("Port settings/device"))
        return d.id;
      same << d.id;
    }
  return same.isEmpty() ? QString() : same.first();
}

void DeviceLibrary::tidy()
{
  if (!QFileInfo::exists(m_file))
    return;
  for (const MyDevice &d : list())
  {
    QVariantMap keys = entryKeys(d.keys);
    // a Bluetooth meter whose place is still the port of the meter before
    const QString byModel = g_modelTransport ? g_modelTransport(d.model()) : QString();
    const QString address = keys.value("Port settings/ble-address").toString().section(' ', 0, 0);
    if (!byModel.isEmpty() && !address.isEmpty()
        && deviceType(keys.value("Port settings/device").toString()) != byModel)
      keys.insert("Port settings/device", byModel + ' ' + address);
    if (keys == d.keys)
      continue;
    write([&](QSettings &s)
    {
      s.beginGroup(kPrefix + d.id);
      for (auto it = d.keys.cbegin(); it != d.keys.cend(); ++it)
        if (!keys.contains(it.key()))
          s.remove(it.key());
      for (auto it = keys.cbegin(); it != keys.cend(); ++it)
        s.setValue(it.key(), it.value());
      s.endGroup();
    });
  }
}

QString MyDevice::where() const
{
  const QString device = keys.value("Port settings/device").toString();
  const QString type = device.section(' ', 0, 0).toUpper();
  if (type == QLatin1String("BLE"))
    return keys.value("Port settings/ble-address").toString();
  if (type == QLatin1String("CALC"))
    return device.section(' ', 1);
  if (type == QLatin1String("SIGROK"))
    return keys.value("Port settings/sigrok-conn").toString();
  return device.section(' ', 1);
}

DeviceLibrary::DeviceLibrary(const QString &dir, QObject *parent) :
  QObject(parent),
  m_file(QDir(dir).filePath("devices.conf"))
{
  connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this]
  {
    watch();   // a file written anew is a new file to the watcher
    Q_EMIT changed();
  });
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]
  {
    // the file came into being
    if (!m_watcher.files().contains(m_file) && QFileInfo::exists(m_file))
    {
      watch();
      Q_EMIT changed();
    }
  });
  tidy();
  watch();
}

void DeviceLibrary::watch()
{
  const QString dir = QFileInfo(m_file).absolutePath();
  if (QFileInfo::exists(dir) && !m_watcher.directories().contains(dir))
    m_watcher.addPath(dir);
  if (QFileInfo::exists(m_file) && !m_watcher.files().contains(m_file))
    m_watcher.addPath(m_file);
}

namespace
{
constexpr int kAttempts = 6;   ///< reading/writing devices.conf, 20 ... 320 ms apart
}

bool DeviceLibrary::write(const std::function<void(QSettings &)> &edit,
                          const std::function<bool()> &landed) const
{
  for (int attempt = 0; attempt < kAttempts; ++attempt)
  {
    if (attempt > 0)
      QThread::msleep(20u << (attempt - 1));
    {
      QSettings s(m_file, QSettings::IniFormat);
      edit(s);
      s.sync();
      if (s.status() == QSettings::NoError)
        return true;
    }
    // an error, and still in the file (Windows CI: the entry of a second
    // instance was there, add() said it failed)
    if (landed && landed())
      return true;
  }
  return false;
}

QList<MyDevice> DeviceLibrary::list() const
{
  // the file is replaced on every write; an attempt that cannot read it is
  // tried again (see write())
  for (int attempt = 1; attempt < kAttempts; ++attempt)
  {
    QSettings probe(m_file, QSettings::IniFormat);
    if (probe.status() == QSettings::NoError)
      break;
    QThread::msleep(20u << (attempt - 1));
  }
  QSettings s(m_file, QSettings::IniFormat);
  QList<MyDevice> out;
  for (const QString &group : s.childGroups())
  {
    if (!group.startsWith(kPrefix))
      continue;
    s.beginGroup(group);
    MyDevice d;
    d.id = group.mid(kPrefix.size());
    d.name = s.value("name").toString();
    d.order = s.value("order", 0).toInt();
    for (const QString &key : s.allKeys())
      if (!ownKey(key))
        d.keys.insert(key, s.value(key));
    s.endGroup();
    out << d;
  }
  std::stable_sort(out.begin(), out.end(), [](const MyDevice &a, const MyDevice &b)
  {
    return a.order != b.order ? a.order < b.order : a.name.localeAwareCompare(b.name) < 0;
  });
  return out;
}

std::optional<MyDevice> DeviceLibrary::find(const QString &id) const
{
  for (const MyDevice &d : list())
    if (d.id == id)
      return d;
  return std::nullopt;
}

QString DeviceLibrary::instanceId(const QString &name, const QStringList &taken)
{
  QString id;
  for (const QChar c : name.trimmed())
  {
    const bool ok = c.isLetterOrNumber() && c.unicode() < 128;
    if (ok || c == '_')
      id += c;
    else if (!id.endsWith('_'))
      id += '_';
  }
  while (id.endsWith('_'))
    id.chop(1);
  while (id.startsWith('_'))
    id.remove(0, 1);
  if (id.isEmpty())
    id = QStringLiteral("meter");
  else if (id.at(0).isDigit())
    id.prepend(QStringLiteral("m_"));
  // instance names are file names: no two that differ in case only
  auto isTaken = [&taken](const QString &candidate)
  {
    return candidate.compare(QLatin1String("default"), Qt::CaseInsensitive) == 0
           || taken.contains(candidate, Qt::CaseInsensitive);
  };
  const QString stem = id;
  for (int n = 2; isTaken(id); ++n)
    id = QString("%1_%2").arg(stem).arg(n);
  return id;
}

QString DeviceLibrary::uniqueName(const QString &base) const
{
  QStringList names;
  for (const MyDevice &d : list())
    names << d.name;
  QString name = base.trimmed().isEmpty() ? tr("Meter") : base.trimmed();
  const QString stem = name;
  for (int n = 2; names.contains(name); ++n)
    name = QString("%1 (%2)").arg(stem).arg(n);
  return name;
}

QString DeviceLibrary::add(const QString &name, const QVariantMap &keys)
{
  const QList<MyDevice> before = list();
  const QString id = QUuid::createUuid().toString(QUuid::Id128).left(8);
  const QVariantMap own = entryKeys(keys);
  const bool ok = write([&](QSettings &s)
  {
    s.beginGroup(kPrefix + id);
    s.setValue("name", name);
    s.setValue("order", before.isEmpty() ? 1 : before.last().order + 1);
    for (auto it = own.cbegin(); it != own.cend(); ++it)
      s.setValue(it.key(), it.value());
    s.endGroup();
  }, [&] { const std::optional<MyDevice> d = find(id); return d && d->name == name; });
  watch();
  Q_EMIT changed();
  return ok ? id : QString();
}

bool DeviceLibrary::update(const QString &id, const QVariantMap &keys)
{
  if (!find(id))
    return false;
  const QVariantMap own = entryKeys(keys);
  const bool ok = write([&](QSettings &s)
  {
    s.beginGroup(kPrefix + id);
    for (const QString &key : s.allKeys())
      if (!ownKey(key))
        s.remove(key);
    for (auto it = own.cbegin(); it != own.cend(); ++it)
      s.setValue(it.key(), it.value());
    s.endGroup();
  }, [&]
  {
    const std::optional<MyDevice> d = find(id);
    if (!d || d->keys.size() != own.size())
      return false;
    for (auto it = own.cbegin(); it != own.cend(); ++it)
      if (d->keys.value(it.key()).toString() != it.value().toString())
        return false;
    return true;
  });
  Q_EMIT changed();
  return ok;
}

bool DeviceLibrary::rename(const QString &id, const QString &name)
{
  if (!find(id) || name.trimmed().isEmpty())
    return false;
  const bool ok = write([&](QSettings &s) { s.setValue(kPrefix + id + "/name", name.trimmed()); },
                        [&] { const std::optional<MyDevice> d = find(id); return d && d->name == name.trimmed(); });
  Q_EMIT changed();
  return ok;
}

bool DeviceLibrary::remove(const QString &id)
{
  if (!find(id))
    return false;
  const bool ok = write([&](QSettings &s) { s.remove(kPrefix + id); }, [&] { return !find(id); });
  Q_EMIT changed();
  return ok;
}

void DeviceLibrary::writeOrder(const QStringList &ids)
{
  write([&](QSettings &s)
  {
    for (int i = 0; i < ids.size(); ++i)
      s.setValue(kPrefix + ids[i] + "/order", i + 1);
  }, [&]
  {
    QStringList now;
    for (const MyDevice &d : list())
      now << d.id;
    return now == ids;
  });
}

bool DeviceLibrary::move(const QString &id, int index)
{
  QStringList ids;
  for (const MyDevice &d : list())
    ids << d.id;
  const int from = int(ids.indexOf(id));
  if (from < 0)
    return false;
  ids.move(from, qBound(0, index, int(ids.size()) - 1));
  writeOrder(ids);
  Q_EMIT changed();
  return true;
}

QString DeviceLibrary::duplicate(const QString &id)
{
  const std::optional<MyDevice> d = find(id);
  if (!d)
    return QString();
  const QString copy = add(uniqueName(d->name), d->keys);
  // right after the original
  QStringList ids;
  for (const MyDevice &e : list())
    if (e.id != copy)
      ids << e.id;
  ids.insert(ids.indexOf(id) + 1, copy);
  writeOrder(ids);
  Q_EMIT changed();
  return copy;
}
