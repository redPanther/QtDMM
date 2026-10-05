//======================================================================
// File:		meterprefs.h
// Author:	Matthias Toussaint
// Created:	Sat Oct 19 15:08:57 CEST 2002
//----------------------------------------------------------------------
// This file is part of QtDMM.
//
// QtDMM is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 3
// as published by the Free Software Foundation.
//
// QtDMM is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
//----------------------------------------------------------------------
// Copyright (c) 2002 Matthias Toussaint
//======================================================================


#pragma once

#include "ui/settings/settingspage.h"

class DeviceSettings;
class DeviceLibrary;
class SharedStateManager;
class QComboBox;
class Settings;
class QPushButton;

/// Settings page "Multimeter": the choice from "My devices" above the
/// settings of the meter (DeviceSettings), which it fills from the instance's
/// Settings and writes back.
class MeterPrefs : public SettingsPage
{
  Q_OBJECT
public:
  MeterPrefs(QWidget *parent = Q_NULLPTR);

  /// The fields of the meter; SettingsDialog reads the connection from them.
  DeviceSettings *settings() const { return m_settings; }
  /// The ports to offer: Transport::availablePorts() and the custom ports of @p cfg.
  static QStringList availablePorts(Settings *cfg);
  /// Source of the other instances' readings, shown as a hint below the formula.
  void           setStateManager(SharedStateManager *state);
  /// "My devices": the choice at the top of the page fills the fields.
  void           setDeviceLibrary(DeviceLibrary *library);

Q_SIGNALS:
  /// The hint's link: the user wants the Special ports page (sigrok-cli path).
  void           showPortsPage();

public Q_SLOTS:
  virtual void   defaultsSLOT() Q_DECL_OVERRIDE;
  virtual void   factoryDefaultsSLOT() Q_DECL_OVERRIDE;
  virtual void   applySLOT() Q_DECL_OVERRIDE;

protected Q_SLOTS:
  /// Fills the "My devices" choice, the entry in use selected.
  void           fillDevices();
  /// A device of "My devices" chosen: its keys into the fields.
  void           onDeviceChosen(int index);
  /// The fields into "My devices": the chosen entry, or a new one.
  void           saveDevice();

private:
  /// The ports and the sigrok-cli path into the fields, then @p keys.
  void           loadFields(const QVariantMap &keys);

  DeviceSettings *m_settings = nullptr;
  DeviceLibrary *m_devices = nullptr;
  QComboBox     *ui_myDevice = nullptr;
  QPushButton   *ui_saveDevice = nullptr;
};
