// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>

class QAction;
class QFileSystemWatcher;
class QWidget;

/// @file
/// The keys of the main window's actions, set on the settings page
/// "Keyboard".

/// Every action that may have a key, with the key it comes with and the
/// one the user gave it. Only the keys that differ from the default are
/// stored, in shortcuts.conf next to devices.conf: one set for all
/// instances, as keys are a habit and not a property of the meter. Another
/// instance changing them is picked up through the file.
class ShortcutRegistry : public QObject
{
  Q_OBJECT
public:
  /// What the settings page lists of an action.
  struct Entry
  {
    QString id;                    ///< the action's objectName, the key in the file
    QString group;                 ///< heading on the page (translated)
    QString name;                  ///< the action's name on the page (translated)
    QPointer<QAction> action;
    QList<QKeySequence> defaults;  ///< the keys it comes with
  };
  /// The user's keys: id -> keys; an empty list takes the key away.
  using Overrides = QMap<QString, QList<QKeySequence>>;

  /// @p file is shortcuts.conf; empty keeps the keys in memory only (tests).
  explicit ShortcutRegistry(QWidget *window, const QString &file, QObject *parent = nullptr);

  /// Takes @p action into the list: its keys now are its defaults, its
  /// tooltip without key the text to add the key to. The action is added
  /// to the window, so its key works also when it sits in a hidden toolbar
  /// or a menu only. @p name is how the page lists it when the action's
  /// text alone is not clear ("Configure" twice); empty: the text. Call
  /// before load().
  void add(QAction *action, const QString &group, const QString &name = QString());
  const QList<Entry> &entries() const { return m_entries; }
  const Entry *find(const QString &id) const;

  /// The keys stored, applied to the actions.
  void load();
  const Overrides &overrides() const { return m_overrides; }
  /// Sets, stores and applies @p overrides (entries equal to the default
  /// are dropped).
  void setOverrides(const Overrides &overrides);
  /// The keys @p id has with @p overrides.
  QList<QKeySequence> keys(const QString &id, const Overrides &overrides) const;
  /// The action other than @p id that has @p key with @p overrides, or an
  /// empty string.
  QString conflict(const QString &id, const QKeySequence &key, const Overrides &overrides) const;
  /// Keys the window does not give to an action: Esc, Tab, Return, and
  /// Alt with a letter or digit (the underlined letters of buttons and menus).
  static bool isReserved(const QKeySequence &key);

  /// "Text (key)" for a tooltip, the key as the platform writes it.
  static QString withKey(const QString &text, const QKeySequence &key);

Q_SIGNALS:
  /// The keys changed (here or in another instance): texts naming a key
  /// are to be redone.
  void changed();

private:
  void apply();
  void read();
  void write() const;

  QWidget *m_window;
  QString m_file;
  QList<Entry> m_entries;
  Overrides m_overrides;
  QFileSystemWatcher *m_watcher = nullptr;
};
