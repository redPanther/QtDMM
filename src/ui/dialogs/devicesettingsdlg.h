// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QVariantMap>

class DeviceSettings;
class QPushButton;

/// "Settings..." of an entry of My devices: the meter's settings as on page
/// 3 of the assistant, for the entry's keys. OK only when they are complete.
/// The caller gives settings() its ports first, then load()s the keys.
class DeviceSettingsDlg : public QDialog
{
  Q_OBJECT
public:
  explicit DeviceSettingsDlg(const QString &name, QWidget *parent = nullptr);

  DeviceSettings *settings() const { return m_settings; }
  void        load(const QVariantMap &keys);
  /// The meter keys as DeviceSettings::keys() gives them.
  QVariantMap keys() const;

private:
  DeviceSettings *m_settings = nullptr;
  QPushButton    *m_ok = nullptr;
};
