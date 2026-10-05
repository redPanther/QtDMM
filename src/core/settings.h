/*
	Copyright (C) 2015 Frank Büttner frank@familie-büttner.de

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>
*/

#pragma once

#include <QtCore>
#include <QColor>

/// Typed access to the QSettings file, with writes staged until save().
///
/// Keys are "Group/name" strings as used throughout the preference pages.
/// set*() writes go to a staging hash first so the settings dialog's Cancel
/// can drop them (clear()); save() commits them. Reads always come from the
/// stored file, so a staged value is not visible to get*() until saved.
///
/// The file is QSettings' native one (~/.config/QtDMM/QtDMM.conf on Unix,
/// %APPDATA%\\QtDMM\\QtDMM.ini on Windows). With an instance id or config
/// directory (--config-id, --config-dir) a separate ini file
/// "QtDMM_<id>.<suffix>" in that directory is used instead.
class Settings : public QObject
{
  Q_OBJECT
public:
  explicit       Settings(QObject *parent = Q_NULLPTR);
  /// Settings of one instance; see the class description for the file used.
  explicit       Settings(const QString &instance_id, const QString &config_path, QObject *parent = Q_NULLPTR);
  ~Settings();

  /// False on the very first start (no settings file yet).
  const bool    &fileExists() const { return m_fileExists; }
  /// Set when an old-format file was migrated; pages then reset a few keys.
  const bool    &fileConverted() const { return m_fileConverted; }
  const QString &fileName() const { return m_filename; }

  /// Removes the settings file of another instance.
  void           deleteConfig(QString instance_id);
  /// Renames the settings file of another (stopped) instance; false when
  /// there is none or @p to already has one. "default" is not renamed.
  bool           renameConfig(const QString &from, const QString &to);
  /// Writes this instance's settings as the file of a new instance, without
  /// the meter and its connection (DMM/, Port settings/), the window position
  /// and the SCPI server switch. Returns the file written.
  QString        copyConfig(const QString &instance_id) const;

  /// The ports set up by hand on the former page "Special ports"
  /// (custom_device0..9, "<type> <address>"): still offered, no new ones.
  QStringList    customPorts() const;
  /// Ids of all instances that have a settings file next to this one.
  QStringList    getConfigInstances();
  /// The directory of the settings files (the instances', devices.conf).
  QString        configDir() const;
  /// Whether @p key belongs to the meter and its connection - DMM/*, Port
  /// settings/* except the list of own ports and the path of sigrok-cli
  /// (DMM/my-device, which of "My devices" it is, among them): what a new
  /// instance does not copy. "My devices" keep a part (DeviceLibrary::entryKeys()).
  static bool    isMeterKey(const QString &key);
  /// The stored keys and values of the meter (isMeterKey()); with
  /// @p staged the writes not saved yet on top.
  QVariantMap    meterKeys(bool staged = false) const;
  /// Stages @p keys as they are (meter keys from "My devices").
  void           setValues(const QVariantMap &keys);
  /// Commits staged writes to the file.
  void           save();
  /// Drops staged writes.
  void           clear();

  int            getInt(const QString &name, const int &def = 0) const;
  void           setInt(const QString &name, const int &value);

  QColor         getColor(const QString &name, const QColor &def = Qt::white)const;
  void           setColor(const QString &name, const QColor &value);

  QString       getString(const QString &name, const QString &def = "") const;
  void          setString(const QString &name, const QString &value);

  bool          getBool(const QString &name, const bool &def = false)const;
  void          setBool(const QString &name, const bool &value);

private:
  bool          m_fileExists;
  bool          m_fileConverted;
  QString       m_filename;
  QSettings    *m_qsettings;
  QString       m_configPath;
  QString       m_configBaseFileName;
  QString       m_configFileNameSuffix;
  QString       m_instanceId;
  QHash<QString, QVariant> *m_tmpConfig;
};
