// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

#include <QDialog>
#include <QTimer>
#include <QVariantMap>

class DeviceSettings;
class DeviceSummary;
class QLabel;
class QPushButton;
class QTabWidget;

/// "Info and settings..." of an entry of My devices (also its double
/// click): the tab Info - the device at a glance, its state now, its
/// maker's website and its help page - and the tab Settings, the meter's
/// settings as on page 3 of the assistant. OK takes the settings, only
/// when they are complete. The caller gives settings() its ports first,
/// then load()s the keys.
class DeviceSettingsDlg : public QDialog
{
  Q_OBJECT
public:
  enum Tab { InfoTab, SettingsTab };

  explicit DeviceSettingsDlg(const QString &name, QWidget *parent = nullptr);

  DeviceSettings *settings() const { return m_settings; }
  void        load(const QVariantMap &keys);
  /// The meter keys as DeviceSettings::keys() gives them.
  QVariantMap keys() const;
  void        showTab(Tab tab);
  Tab         currentTab() const;
  /// What the Info tab says of the device's state, rich text; asked at
  /// once and every second while the dialog is open.
  void        setStatus(const std::function<QString()> &status);
  /// The Info tab's text of the state and the links (for the tests).
  QString     statusText() const;
  QString     linksText() const;

Q_SIGNALS:
  /// The link to the help: a page of QtDMM's help ("supported-devices.md").
  void        helpRequested(const QString &page);

private:
  void        updateInfo();
  void        updateStatus();

  QString         m_name;
  QTabWidget     *m_tabs = nullptr;
  DeviceSummary  *m_summary = nullptr;
  QLabel         *m_status = nullptr;
  QLabel         *m_links = nullptr;
  DeviceSettings *m_settings = nullptr;
  QPushButton    *m_ok = nullptr;
  std::function<QString()> m_statusText;
  QTimer          m_tick;
};
