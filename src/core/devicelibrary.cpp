// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/devicelibrary.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
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

QList<MyDevice> DeviceLibrary::list() const
{
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

QString DeviceLibrary::match(const QVariantMap &keys) const
{
  for (const MyDevice &d : list())
  {
    bool same = !d.keys.isEmpty();
    for (auto it = d.keys.cbegin(); same && it != d.keys.cend(); ++it)
      same = keys.value(it.key()).toString() == it.value().toString();
    if (same)
      return d.id;
  }
  return QString();
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
  QSettings s(m_file, QSettings::IniFormat);
  s.beginGroup(kPrefix + id);
  s.setValue("name", name);
  s.setValue("order", before.isEmpty() ? 1 : before.last().order + 1);
  for (auto it = keys.cbegin(); it != keys.cend(); ++it)
    s.setValue(it.key(), it.value());
  s.endGroup();
  s.sync();
  watch();
  Q_EMIT changed();
  return s.status() == QSettings::NoError ? id : QString();
}

bool DeviceLibrary::update(const QString &id, const QVariantMap &keys)
{
  if (!find(id))
    return false;
  QSettings s(m_file, QSettings::IniFormat);
  s.beginGroup(kPrefix + id);
  for (const QString &key : s.allKeys())
    if (!ownKey(key))
      s.remove(key);
  for (auto it = keys.cbegin(); it != keys.cend(); ++it)
    s.setValue(it.key(), it.value());
  s.endGroup();
  s.sync();
  Q_EMIT changed();
  return s.status() == QSettings::NoError;
}

bool DeviceLibrary::rename(const QString &id, const QString &name)
{
  if (!find(id) || name.trimmed().isEmpty())
    return false;
  QSettings s(m_file, QSettings::IniFormat);
  s.setValue(kPrefix + id + "/name", name.trimmed());
  s.sync();
  Q_EMIT changed();
  return s.status() == QSettings::NoError;
}

bool DeviceLibrary::remove(const QString &id)
{
  if (!find(id))
    return false;
  QSettings s(m_file, QSettings::IniFormat);
  s.remove(kPrefix + id);
  s.sync();
  Q_EMIT changed();
  return s.status() == QSettings::NoError;
}

void DeviceLibrary::writeOrder(const QStringList &ids)
{
  QSettings s(m_file, QSettings::IniFormat);
  for (int i = 0; i < ids.size(); ++i)
    s.setValue(kPrefix + ids[i] + "/order", i + 1);
  s.sync();
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
