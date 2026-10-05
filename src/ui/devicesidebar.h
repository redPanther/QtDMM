// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QTreeWidget>
#include <QVariant>

class DeviceLibrary;

/// The sidebar "Devices": the node "My devices" with the entries of the
/// DeviceLibrary in their order. A click switches to an entry, the one in
/// use is bold with the state of its readings; where a device is connected
/// shows in its tooltip. The context menu has Settings, Rename (in place),
/// Open in a new window and Remove; dragging an entry changes the order.
class DeviceSidebar : public QTreeWidget
{
  Q_OBJECT
public:
  explicit DeviceSidebar(DeviceLibrary *library, QWidget *parent = nullptr);

  /// The entry this window uses (empty: none) and whether its readings come in.
  void        setCurrent(const QString &id, bool active);
  /// The node "My devices".
  QTreeWidgetItem *devicesNode() const { return m_devices; }
  /// The item of the entry @p id, nullptr when there is none.
  QTreeWidgetItem *deviceItem(const QString &id) const;
  /// The ids of the entries, in the order shown.
  QStringList deviceIds() const;

  /// Shown at the start: as the user left it (@p stored, invalid when never
  /// set), else with two devices or more; a window without a meter shows it
  /// whenever there are devices to choose from.
  static bool shownAtStart(const QVariant &stored, int devices, bool configured);
  /// The assistant added an entry: the sidebar opens with the second device.
  static bool opensAfterAdd(int devicesBefore, int devicesAfter);

Q_SIGNALS:
  void        switchRequested(const QString &id);
  void        settingsRequested(const QString &id);
  void        newWindowRequested(const QString &id);

public Q_SLOTS:
  /// The entries anew from the library.
  void        fill();

protected:
  void        dropEvent(QDropEvent *event) override;
  void        contextMenuEvent(QContextMenuEvent *event) override;

private:
  void        updateMarks();
  QString     idOf(const QTreeWidgetItem *item) const;

  DeviceLibrary   *m_library = nullptr;
  QTreeWidgetItem *m_devices = nullptr;
  QString          m_current;
  bool             m_active = false;
  bool             m_filling = false;
};
