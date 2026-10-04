// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class DeviceLibrary;
class QListWidget;
class QPushButton;

/// "Manage my devices": the list of DeviceLibrary, to rename, duplicate,
/// delete and reorder (drag) its entries, and to use or edit one.
class MyDevicesDlg : public QDialog
{
  Q_OBJECT
public:
  MyDevicesDlg(DeviceLibrary *library, QWidget *parent = nullptr);

Q_SIGNALS:
  /// Switch to the device @p id.
  void        useRequested(const QString &id);
  /// Switch to it and open the meter page.
  void        editRequested(const QString &id);

private:
  void        fill();
  QString     currentId() const;
  void        updateButtons();

  DeviceLibrary *m_library;
  QListWidget   *m_list;
  QPushButton   *m_use;
  QPushButton   *m_edit;
  QPushButton   *m_rename;
  QPushButton   *m_duplicate;
  QPushButton   *m_delete;
  bool           m_filling = false;
};
