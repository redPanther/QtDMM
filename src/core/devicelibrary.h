// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QVariantMap>
#include <optional>

/// One of "My devices": a meter and its connection under a name.
struct MyDevice
{
  QString     id;      ///< fixed, the name may change
  QString     name;
  int         order = 0;
  QVariantMap keys;    ///< the instance's meter keys (Settings::isMeterKey()), as they are stored

  QString model() const { return keys.value("DMM/model").toString(); }
  /// Where it is connected, for a menu: the port, the Bluetooth address, the formula.
  QString where() const;
};

/// "My devices": the meters a user owns, each with its connection, to switch
/// between them with a click instead of filling in the meter page again.
///
/// One file for all instances, devices.conf next to their settings, one
/// section per device ([device-<id>]) with its name, its place in the list
/// and the meter keys exactly as an instance stores them - applying an entry
/// copies them, nothing is converted. Every change goes to the file at
/// once (QSettings merges what another instance wrote meanwhile), and a
/// change by another instance comes back as changed(). Only QtCore.
class DeviceLibrary : public QObject
{
  Q_OBJECT
public:
  /// The library in @p dir (Settings::configDir()).
  explicit DeviceLibrary(const QString &dir, QObject *parent = nullptr);

  QString     fileName() const { return m_file; }
  /// All entries, in their order.
  QList<MyDevice> list() const;
  std::optional<MyDevice> find(const QString &id) const;
  /// The entry whose keys @p keys has the same values of - the one in use;
  /// empty when none is.
  QString     match(const QVariantMap &keys) const;
  /// @p base, or with " (2)", " (3)", ... when an entry has that name.
  QString     uniqueName(const QString &base) const;

  /// A new entry at the end; returns its id.
  QString     add(const QString &name, const QVariantMap &keys);
  bool        update(const QString &id, const QVariantMap &keys);
  bool        rename(const QString &id, const QString &name);
  bool        remove(const QString &id);
  /// Moves the entry to position @p index of list().
  bool        move(const QString &id, int index);
  /// A copy right after it, named "<name> (2)"; returns the new id.
  QString     duplicate(const QString &id);

Q_SIGNALS:
  /// The list changed, here or in another instance.
  void        changed();

private:
  void        watch();
  void        writeOrder(const QStringList &ids);

  QString     m_file;
  QFileSystemWatcher m_watcher;
};
