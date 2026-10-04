// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QVariantMap>
#include <functional>
#include <optional>

/// One of "My devices": a meter and its connection under a name.
struct MyDevice
{
  QString     id;      ///< fixed, the name may change
  QString     name;
  int         order = 0;
  QVariantMap keys;    ///< the meter keys that belong to it (DeviceLibrary::entryKeys())

  QString model() const { return keys.value("DMM/model").toString(); }
  /// Where it is connected, for a menu: the port, the Bluetooth address, the formula.
  QString where() const;
};

/// "My devices": the meters a user owns, each with its connection, to switch
/// between them with a click instead of filling in the meter page again.
///
/// One file for all instances, devices.conf next to their settings, one
/// section per device ([device-<id>]) with its name, its place in the list
/// and the meter keys that belong to the device (entryKeys(): the model, the
/// place and what its way of connecting needs - no values of another
/// transport), named as an instance stores them: applying an entry copies
/// them. An older file with whole snapshots is tidied up when the library
/// is made. Every change goes to the file at once (QSettings merges what
/// another instance wrote meanwhile), and a change by another instance
/// comes back as changed(). Only QtCore.
///
/// Which entry an instance uses is its key DMM/my-device (the id), not a
/// comparison of keys.
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
  /// The entry at the place of @p keys (place()), whatever else differs;
  /// empty when none is. Of two entries at one USB cable type (the same
  /// VID:PID) the one with the same path.
  QString     findByPlace(const QVariantMap &keys) const;
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

  /// Of the meter keys @p keys those that belong to a device: model, data
  /// format, display and the place always, then what the way of connecting
  /// needs - the line settings for a serial port, an HID cable or the
  /// network, key and values for a Victron device, conn and options for
  /// sigrok, the signal of the virtual meter, unit and formula of a
  /// calculated value. A Bluetooth meter that QtDMM connects to (GATT) needs
  /// its address only.
  static QVariantMap entryKeys(const QVariantMap &keys);
  /// Where a device is, as one string to compare: "serial <port>" (a
  /// /dev/serial/by-id name and its ttyUSB are the same place while the
  /// cable is plugged in), "hid <vid>:<pid>" (the hidraw number changes when
  /// it is plugged in again), "ble <address>", "rfc2217 <host:port>",
  /// "sigrok <conn>", "calc <formula>". Empty when there is no place.
  static QString place(const QVariantMap &keys);
  /// How a model connects when its name alone tells: "ble" (Victron),
  /// "blegatt", or empty for a port of its own. The model table is not in
  /// QtCore: the application sets this, before it makes a library.
  /// entryKeys() and tidy() go by it before the device string - an entry
  /// from before kept the port of the meter before as its place.
  static void setModelTransport(std::function<QString(const QString &model)> transport);

Q_SIGNALS:
  /// The list changed, here or in another instance.
  void        changed();

private:
  void        watch();
  /// entryKeys() for every entry: snapshots of all meter keys from before
  /// are cut down (name, order and id stay).
  void        tidy();
  void        writeOrder(const QStringList &ids);

  QString     m_file;
  QFileSystemWatcher m_watcher;
};
