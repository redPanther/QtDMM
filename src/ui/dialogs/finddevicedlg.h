// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QList>
#include <QMap>
#include <QVariantMap>

#include "device/discovery/discovery.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class Settings;
class DeviceLibrary;

/// "Find device": where to look (USB cable, Bluetooth, serial, network -
/// nothing ticked at first, the choice remembered), a search over the ticked
/// ways at once, the finds as cards as they come, then the model to confirm
/// and connect. A card QtDMM may not open says why, with "How to fix". A
/// find that is one of My devices says so and connects that entry; one that
/// a running instance uses says that too.
class FindDeviceDlg : public QDialog
{
  Q_OBJECT
public:
  FindDeviceDlg(Settings *settings, DeviceLibrary *library, QWidget *parent = nullptr);
  ~FindDeviceDlg() override;

  /// After accept(): the meter keys of the choice (where it is, the model,
  /// a Victron key).
  QVariantMap keys() const;
  /// Whether to keep it in My devices, and under which name.
  bool        addToLibrary() const;
  QString     name() const;
  /// The entry of My devices at the place of the choice (DeviceLibrary::
  /// findByPlace()): connecting uses it instead of a new one. Empty for a
  /// meter not saved yet.
  QString     knownDevice() const;
  /// Places (DeviceLibrary::place()) the running instances use, with what
  /// a card at that place says ("In use here"). Before the search.
  void        setPlacesInUse(const QMap<QString, QString> &places) { m_inUse = places; }

private:
  void        search();
  void        stopSearch();
  void        add(const Candidate &c);
  void        select();
  void        showAllModels(bool all);
  void        updateButtons();
  QList<QCheckBox *> places() const;

  Settings      *m_settings;
  DeviceLibrary *m_library;
  QCheckBox     *m_usb;
  QCheckBox     *m_ble;
  QCheckBox     *m_serial;
  QCheckBox     *m_network;
  QPushButton   *m_search;
  QProgressBar  *m_progress;
  QListWidget   *m_cards;
  QLabel        *m_hint;
  QPushButton   *m_fix;
  QComboBox     *m_model;
  QCheckBox     *m_allModels;
  QLabel        *m_keyLabel;
  QLineEdit     *m_key;
  QCheckBox     *m_keep;
  QLineEdit     *m_name;
  QPushButton   *m_connect;
  bool           m_needsKey = false;   ///< a Victron device: its key is asked
  QList<Candidate> m_found;
  QStringList    m_known;     ///< per find: the entry of My devices at its place, or empty
  QMap<QString, QString> m_inUse;
  QList<Discoverer *> m_running;
};
