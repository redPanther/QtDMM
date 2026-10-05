// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QVariantMap>

#include "device/dmmdecoder.h"

class DeviceLibrary;
class DeviceSettings;
class SharedStateManager;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QToolButton;

/// The assistant "Add device": how the meter is connected (page 1), which
/// meter it is with its settings and a name (page 3), and where it goes - in
/// this window or a new one (page 4). Page 2, the search for the port, comes
/// for cable, Bluetooth and network; until then they go to page 3 with the
/// port field. The caller keeps the result in My devices.
class AddDeviceDlg : public QDialog
{
  Q_OBJECT
public:
  enum Connection { Cable, Bluetooth, Network, Sigrok, Simulated };
  enum Page { ConnectionPage, DevicePage, TargetPage };
  enum Target { NoTarget, ThisWindow, NewWindow };

  explicit AddDeviceDlg(DeviceLibrary *library, QWidget *parent = nullptr);

  /// The ports for the port field (Transport::availablePorts() and the custom ports).
  void        setPorts(const QStringList &ports);
  void        setSigrokExe(const QString &exe);
  /// The other instances' readings for the formula of a calculated value.
  void        setStateManager(SharedStateManager *state);
  /// The meter of this window, named on the button "In this window".
  void        setCurrentDevice(const QString &name);

  /// Page 1: the connection; goes on to the next page.
  void        chooseConnection(Connection connection);
  Connection  connection() const { return m_connection; }
  Page        page() const;
  /// Whether "Next" may be pressed now.
  bool        canGoNext() const;

  /// The device's meter keys, as DeviceSettings::keys() gives them.
  QVariantMap keys() const;
  QString     name() const;
  /// Where the device goes; set by the button on page 4.
  Target      target() const { return m_target; }
  DeviceSettings *settings() const { return m_settings; }

  /// The models a connection offers on page 3.
  static bool offers(Connection connection, const DmmDecoder::DMMInfo &info);

public Q_SLOTS:
  void        back();
  void        next();

private:
  QToolButton *tile(const QString &icon, const QString &text, const QString &toolTip);
  void        showPage(Page page);
  void        updateButtons();
  /// The name follows the model until it is typed in.
  void        suggestName();
  void        finish(Target target);

  DeviceLibrary  *m_library = nullptr;
  DeviceSettings *m_settings = nullptr;
  QStackedWidget *m_pages = nullptr;
  QLabel         *m_title = nullptr;
  QLineEdit      *m_name = nullptr;
  QToolButton    *m_thisWindow = nullptr;
  QToolButton    *m_newWindow = nullptr;
  QPushButton    *m_back = nullptr;
  QPushButton    *m_next = nullptr;
  Connection      m_connection = Cable;
  Target          m_target = NoTarget;
  QString         m_currentDevice;
  QString         m_model;   ///< the model the name was suggested for
};
