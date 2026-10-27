// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/recentfiles.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace
{
QString clean(const QString &path)
{
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
}

RecentFiles::RecentFiles(const QString &file, const QString &key, int max) :
  m_file(file),
  m_key(key),
  m_max(max)
{
}

QStringList RecentFiles::list() const
{
  if (m_file.isEmpty())
    return m_memory;
  return QSettings(m_file, QSettings::IniFormat).value(m_key).toStringList();
}

void RecentFiles::add(const QString &path)
{
  store(added(list(), clean(path), m_max));
}

void RecentFiles::remove(const QString &path)
{
  QStringList l = list();
  l.removeAll(clean(path));
  store(l);
}

void RecentFiles::clear()
{
  store(QStringList());
}

QStringList RecentFiles::added(QStringList list, const QString &path, int max)
{
  list.removeAll(path);
  list.prepend(path);
  while (list.size() > qMax(0, max))
    list.removeLast();
  return list;
}

void RecentFiles::store(const QStringList &list)
{
  if (m_file.isEmpty())
  {
    m_memory = list;
    return;
  }
  QSettings s(m_file, QSettings::IniFormat);
  if (list.isEmpty())
    s.remove(m_key);
  else
    s.setValue(m_key, list);
  s.sync();
}
