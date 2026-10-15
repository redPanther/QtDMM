// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/shortcuts.h"

#include <QAction>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSettings>
#include <QWidget>

namespace
{
constexpr const char *kGroup = "Shortcuts";
constexpr const char *kBaseTip = "qtdmmBaseToolTip";   ///< the action's tooltip without the key
}

ShortcutRegistry::ShortcutRegistry(QWidget *window, const QString &file, QObject *parent)
  : QObject(parent)
  , m_window(window)
  , m_file(file)
{
  if (m_file.isEmpty())
    return;
  // QSettings writes a new file and renames it over the old one, which a
  // watch on the file itself does not survive: watch the directory
  m_watcher = new QFileSystemWatcher(this);
  m_watcher->addPath(QFileInfo(m_file).absolutePath());
  connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]
  {
    const Overrides before = m_overrides;
    read();
    if (m_overrides != before)
    {
      apply();
      Q_EMIT changed();
    }
  });
}

void ShortcutRegistry::add(QAction *action, const QString &group, const QString &name)
{
  Q_ASSERT(!action->objectName().isEmpty());
  QString tip = action->toolTip();
  if (tip.isEmpty() || tip == action->text())
    tip = action->text().remove('&');
  action->setProperty(kBaseTip, tip);
  m_entries << Entry{ action->objectName(), group, name.isEmpty() ? action->text().remove('&') : name, action,
                     action->shortcuts() };
  if (m_window)
    m_window->addAction(action);
}

const ShortcutRegistry::Entry *ShortcutRegistry::find(const QString &id) const
{
  for (const Entry &e : m_entries)
    if (e.id == id)
      return &e;
  return nullptr;
}

void ShortcutRegistry::load()
{
  read();
  apply();
}

void ShortcutRegistry::setOverrides(const Overrides &overrides)
{
  Overrides clean;
  for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
  {
    const Entry *e = find(it.key());
    if (e && it.value() != e->defaults)
      clean.insert(it.key(), it.value());
  }
  if (clean == m_overrides)
    return;
  m_overrides = clean;
  write();
  apply();
  Q_EMIT changed();
}

QList<QKeySequence> ShortcutRegistry::keys(const QString &id, const Overrides &overrides) const
{
  if (overrides.contains(id))
    return overrides.value(id);
  const Entry *e = find(id);
  return e ? e->defaults : QList<QKeySequence>();
}

QString ShortcutRegistry::conflict(const QString &id, const QKeySequence &key, const Overrides &overrides) const
{
  if (key.isEmpty())
    return QString();
  for (const Entry &e : m_entries)
    if (e.id != id && keys(e.id, overrides).contains(key))
      return e.id;
  return QString();
}

bool ShortcutRegistry::isReserved(const QKeySequence &key)
{
  if (key.count() != 1)
    return false;
  const QKeyCombination c = key[0];
  const Qt::Key k = c.key();
  const Qt::KeyboardModifiers m = c.keyboardModifiers() & ~Qt::KeypadModifier;
  if (m == Qt::NoModifier || m == Qt::ShiftModifier)
    if (k == Qt::Key_Escape || k == Qt::Key_Tab || k == Qt::Key_Backtab || k == Qt::Key_Return || k == Qt::Key_Enter)
      return true;
  const bool letterOrDigit = (k >= Qt::Key_A && k <= Qt::Key_Z) || (k >= Qt::Key_0 && k <= Qt::Key_9);
  return letterOrDigit && (m == Qt::AltModifier || m == (Qt::AltModifier | Qt::ShiftModifier));
}

QString ShortcutRegistry::withKey(const QString &text, const QKeySequence &key)
{
  if (key.isEmpty())
    return text;
  return QString("%1 (%2)").arg(text, key.toString(QKeySequence::NativeText));
}

void ShortcutRegistry::apply()
{
  for (const Entry &e : std::as_const(m_entries))
  {
    if (!e.action)
      continue;
    const QList<QKeySequence> k = keys(e.id, m_overrides);
    e.action->setShortcuts(k);
    e.action->setToolTip(withKey(e.action->property(kBaseTip).toString(), k.value(0)));
  }
}

void ShortcutRegistry::read()
{
  m_overrides.clear();
  if (m_file.isEmpty())
    return;
  QSettings s(m_file, QSettings::IniFormat);
  s.beginGroup(kGroup);
  for (const QString &id : s.childKeys())
    if (find(id))
      m_overrides.insert(id, QKeySequence::listFromString(s.value(id).toString(), QKeySequence::PortableText));
}

void ShortcutRegistry::write() const
{
  if (m_file.isEmpty())
    return;
  QSettings s(m_file, QSettings::IniFormat);
  s.remove(kGroup);
  s.beginGroup(kGroup);
  for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it)
    s.setValue(it.key(), QKeySequence::listToString(it.value(), QKeySequence::PortableText));
  s.endGroup();
  s.sync();
}
