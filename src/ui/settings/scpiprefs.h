// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/settings/settingspage.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

/// Settings page "SCPI server": lets other programs read the meter over
/// TCP in SCPI (port 5025, lxi-tools style), with optional mDNS
/// announcement, HiSLIP and VXI-11 for VISA next to it. Keys Scpi/enabled,
/// Scpi/port, Scpi/all-interfaces, Scpi/mdns, Scpi/hislip, Scpi/hislip-port,
/// Scpi/vxi11, Scpi/vxi11-rpc (auto, system, own).
class ScpiPrefs : public SettingsPage
{
  Q_OBJECT
public:
  explicit ScpiPrefs(QWidget *parent = nullptr);

  bool enabled() const;
  int port() const;
  bool allInterfaces() const;
  bool mdns() const;
  bool hislip() const;
  int hislipPort() const;
  bool vxi11() const;
  QString vxi11Rpc() const;   ///< "auto", "system", "own"

  /// Live status from the running server, shown below the settings.
  void setStatus(const QString &text);

public Q_SLOTS:
  void defaultsSLOT() override;
  void factoryDefaultsSLOT() override;
  void applySLOT() override;

private:
  void updateHint();

  QCheckBox *m_enabled, *m_mdns, *m_hislip, *m_vxi11;
  QSpinBox *m_port, *m_hislipPort;
  QComboBox *m_bind, *m_vxi11Rpc;
  QLabel *m_hint, *m_status;
};
