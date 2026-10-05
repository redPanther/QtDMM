// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QMap>
#include <QVariantMap>
#include <functional>

#include "device/discovery/discovery.h"
#include "device/dmmdecoder.h"

class DeviceLibrary;
class DeviceSettings;
class SharedStateManager;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QToolButton;

/// The assistant "Add device": how the meter is connected (page 1), where
/// (page 2: a passive search for that connection, every port found, the
/// recognised ones first, or typed in), which meter it is with its settings
/// and a name (page 3), and where it goes - in this window or a new one
/// (page 4). sigrok and simulated meters have no page 2.
/// The caller keeps the result in My devices - a find that already is one
/// of them (knownDevice()) changes that entry instead.
class AddDeviceDlg : public QDialog
{
  Q_OBJECT
public:
  enum Connection { Cable, Bluetooth, Network, Sigrok, Simulated };
  enum Page { ConnectionPage, PortPage, DevicePage, TargetPage };
  enum Target { NoTarget, ThisWindow, NewWindow };

  explicit AddDeviceDlg(DeviceLibrary *library, QWidget *parent = nullptr);
  ~AddDeviceDlg() override;

  /// The ports for the port field (Transport::availablePorts() and the custom ports).
  void        setPorts(const QStringList &ports);
  void        setSigrokExe(const QString &exe);
  /// The other instances' readings for the formula of a calculated value.
  void        setStateManager(SharedStateManager *state);
  /// The meter of this window, named on the button "In this window".
  void        setCurrentDevice(const QString &name);
  /// Places (DeviceLibrary::place()) the running instances use, with what a
  /// find at that place says ("In use by the instance u").
  void        setPlacesInUse(const QMap<QString, QString> &places) { m_inUse = places; }
  /// The searches of a connection; by default UsbDiscoverer and
  /// SerialDiscoverer for a cable, BleDiscoverer for Bluetooth,
  /// BridgeDiscoverer for the network. The test
  /// gives none and adds the finds itself.
  void        setDiscoverers(std::function<QList<Discoverer *>(Connection)> make) { m_makeDiscoverers = std::move(make); }

  /// Page 1: the connection; goes on to the next page.
  void        chooseConnection(Connection connection);
  Connection  connection() const { return m_connection; }
  Page        page() const;
  /// Whether "Next" may be pressed now.
  bool        canGoNext() const;
  /// Page 2: a find of the search, shown as a card (recognised meters first).
  void        addCandidate(const Candidate &candidate);
  /// Page 2: the cards in their order, by Candidate::key.
  QStringList candidates() const;
  /// Page 2: chooses the card @p key; its port goes into the port field.
  void        chooseCandidate(const QString &key);
  /// Page 2: the port field (a port, a Bluetooth address, or host:port).
  void        setPort(const QString &port);
  /// The entry of My devices the device is (found at its place, the model
  /// unchanged on page 3); empty for a new device.
  QString     knownDevice() const;

  /// The device's meter keys, as DeviceSettings::keys() gives them.
  QVariantMap keys() const;
  QString     name() const;
  /// Where the device goes; set by the button on page 4.
  Target      target() const { return m_target; }
  DeviceSettings *settings() const { return m_settings; }

  /// The models a connection offers on page 3.
  static bool offers(Connection connection, const DmmDecoder::DMMInfo &info);
  /// Whether the connection has page 2 (cable, Bluetooth, network).
  static bool hasPortPage(Connection connection);

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
  void        startSearch();
  void        stopSearch();
  /// The card for the port field's text, or -1 when it was typed in.
  int         chosenCandidate() const;
  void        updateHint();
  /// From page 2 to page 3: the find (or the typed port) into the fields,
  /// a known entry with its name.
  void        takePort();

  DeviceLibrary  *m_library = nullptr;
  DeviceSettings *m_settings = nullptr;
  QStackedWidget *m_pages = nullptr;
  QLabel         *m_title = nullptr;
  QLineEdit      *m_name = nullptr;
  QToolButton    *m_thisWindow = nullptr;
  QToolButton    *m_newWindow = nullptr;
  QProgressBar   *m_progress = nullptr;
  QListWidget    *m_cards = nullptr;
  QLabel         *m_hint = nullptr;
  QPushButton    *m_fix = nullptr;
  QPushButton    *m_searchAgain = nullptr;
  QLineEdit      *m_port = nullptr;
  QList<Candidate> m_found;   ///< in the order of the cards
  QMap<QString, QString> m_inUse;
  QList<Discoverer *> m_running;
  std::function<QList<Discoverer *>(Connection)> m_makeDiscoverers;
  QString         m_known;    ///< the entry of My devices at the chosen place
  QPushButton    *m_back = nullptr;
  QPushButton    *m_next = nullptr;
  Connection      m_connection = Cable;
  Target          m_target = NoTarget;
  QString         m_currentDevice;
  QString         m_model;   ///< the model the name was suggested for
};
