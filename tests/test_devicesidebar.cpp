// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sidebar "Devices" without a main window: My devices in their order,
// the one in use marked, a click to switch, rename in place, a change of
// the library shown; when the sidebar is shown at the start and after the
// assistant; the dialog "Settings..." of an entry; the node "Instances"
// with its readings, clicks, rename and delete only for stopped ones.

#include <QtWidgets>
#include <QTemporaryDir>

#include "core/devicelibrary.h"
#include "ui/devicesettings.h"
#include "ui/devicesidebar.h"
#include "ui/dialogs/devicesettingsdlg.h"

static int failed = 0;

static void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAIL:" << what;
    ++failed;
  }
}

int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir dir;
  DeviceLibrary library(dir.path());
  const QString ut61e = library.add("Bench UT61E", { { "DMM/model", "Uni-Trend UT61E" },
                                                     { "Port settings/device", "SERIAL /dev/ttyUSB0" } });
  const QString tfa = library.add("TFA", { { "DMM/model", "TFA AIRCO2NTROL MINI" } });
  const QString shunt = library.add("Battery", { { "DMM/model", "Victron SmartShunt" } });

  // 1. the entries in their order, the one in use marked
  DeviceSidebar sidebar(&library);
  check(sidebar.deviceIds() == QStringList({ ut61e, tfa, shunt }), "entries in their order");
  check(sidebar.devicesNode()->text(0) == "My devices", "the node");
  QTreeWidgetItem *bench = sidebar.deviceItem(ut61e);
  check(bench && bench->text(0) == "Bench UT61E", "the name");
  check(bench && bench->toolTip(0).contains("/dev/ttyUSB0"), "the place in the tooltip: " + bench->toolTip(0));
  check(bench && !(bench->flags() & Qt::ItemIsDropEnabled), "an entry takes no other");
  sidebar.setCurrent(tfa, true);
  check(sidebar.deviceItem(tfa)->font(0).bold() && !bench->font(0).bold(), "the one in use is bold");
  check(!sidebar.deviceItem(tfa)->icon(0).isNull(), "the one in use has its state");

  // 2. a click switches, but not to the one in use
  QStringList switched;
  QObject::connect(&sidebar, &DeviceSidebar::switchRequested, [&](const QString &id) { switched << id; });
  Q_EMIT sidebar.itemClicked(bench, 0);
  Q_EMIT sidebar.itemClicked(sidebar.deviceItem(tfa), 0);
  Q_EMIT sidebar.itemClicked(sidebar.devicesNode(), 0);
  check(switched == QStringList({ ut61e }), "click switches: " + switched.join(','));

  // 3. rename in place; an empty name is not taken
  sidebar.deviceItem(shunt)->setText(0, "House battery");
  check(library.find(shunt)->name == "House battery", "renamed in place");
  sidebar.deviceItem(shunt)->setText(0, "  ");
  check(library.find(shunt)->name == "House battery", "an empty name is not taken");

  // 4. a change of the library (here or another instance) is shown
  library.move(shunt, 0);
  QCoreApplication::processEvents();
  check(sidebar.deviceIds() == QStringList({ shunt, ut61e, tfa }), "new order shown");
  library.remove(tfa);
  QCoreApplication::processEvents();
  check(sidebar.deviceIds() == QStringList({ shunt, ut61e }), "removed entry gone");
  check(sidebar.deviceItem(shunt)->text(0) == "House battery", "new name shown");

  // 5. when it is shown
  check(!DeviceSidebar::shownAtStart(QVariant(), 1, true), "one device: closed");
  check(DeviceSidebar::shownAtStart(QVariant(), 2, true), "two devices, never set: open");
  check(!DeviceSidebar::shownAtStart(QVariant(false), 3, true), "closed by the user: stays closed");
  check(DeviceSidebar::shownAtStart(QVariant(true), 1, true), "opened by the user: stays open");
  check(DeviceSidebar::shownAtStart(QVariant(false), 1, false), "a window without a meter, devices there: open");
  check(!DeviceSidebar::shownAtStart(QVariant(), 0, false), "a window without a meter, no devices: closed");
  check(DeviceSidebar::opensAfterAdd(1, 2), "the second device opens it");
  check(!DeviceSidebar::opensAfterAdd(0, 1), "the first does not");
  check(!DeviceSidebar::opensAfterAdd(2, 3), "the third does not");
  check(!DeviceSidebar::opensAfterAdd(2, 2), "a known one changed does not");

  // 6. "Settings..." of an entry: its keys, OK only when complete
  {
    DeviceSettingsDlg dlg("Battery");
    QPushButton *ok = dlg.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    dlg.load({ { "DMM/model", "Victron SmartShunt" }, { "Port settings/ble-address", "AA:BB:CC:DD:EE:FF" } });
    check(!ok->isEnabled(), "Victron without key: no OK");
    dlg.load({ { "DMM/model", "Victron SmartShunt" }, { "Port settings/ble-address", "AA:BB:CC:DD:EE:FF" },
               { "Port settings/ble-key", "00112233445566778899aabbccddeeff" } });
    check(ok->isEnabled(), "complete: OK");
    check(dlg.keys().value("DMM/model") == "Victron SmartShunt", "keys: the model");
  }

  // 7. the instances: this window's bold, a click raises another, a double click starts it
  {
    QList<DeviceSidebar::Instance> rows;
    rows << DeviceSidebar::Instance { "default", "Bench UT61E", "12.01 V DC", true, true }
         << DeviceSidebar::Instance { "p", "= u * i", "stopped", false, false }
         << DeviceSidebar::Instance { "u", "", "OL", true, false };
    sidebar.setInstances(rows, "default");
    check(sidebar.instancesNode()->childCount() == 3, "three instances");
    QTreeWidgetItem *own = sidebar.instanceItem("default");
    QTreeWidgetItem *p = sidebar.instanceItem("p");
    QTreeWidgetItem *u = sidebar.instanceItem("u");
    check(own && own->font(0).bold() && !p->font(0).bold(), "this window's is bold");
    check(own && own->text(1) == "12.01 V DC", "the reading");
    check(own && own->child(0)->text(0) == "Bench UT61E", "the device below");
    check(u && u->child(0)->isHidden(), "no device, no line");
    check(p && (p->flags() & Qt::ItemIsEditable), "a stopped one can be renamed");
    check(u && !(u->flags() & Qt::ItemIsEditable), "a running one cannot");
    check(own && !(own->flags() & Qt::ItemIsEditable), "nor this one");
    check(!(sidebar.instancesNode()->flags() & Qt::ItemIsDropEnabled), "no device dropped into the instances");

    QStringList opened;
    QObject::connect(&sidebar, &DeviceSidebar::instanceRequested, [&](const QString &id) { opened << id; });
    Q_EMIT sidebar.itemClicked(own, 0);
    Q_EMIT sidebar.itemClicked(p->child(0), 0);
    Q_EMIT sidebar.itemClicked(u, 0);
    check(opened == QStringList({ "u" }), "a click brings a running one to the front, starts none: "
                                           + opened.join(','));
    opened.clear();
    Q_EMIT sidebar.itemDoubleClicked(own, 0);
    Q_EMIT sidebar.itemDoubleClicked(p->child(0), 0);
    Q_EMIT sidebar.itemDoubleClicked(u, 0);
    check(opened == QStringList({ "p" }), "a double click starts a stopped one: " + opened.join(','));

    // the new name goes to MainWindow; the item keeps the old one till then
    QStringList renamed;
    QObject::connect(&sidebar, &DeviceSidebar::renameInstanceRequested,
                     [&](const QString &from, const QString &to) { renamed << from + ">" + to; });
    p->setText(0, " power ");
    check(renamed == QStringList({ "p>power" }), "rename asked: " + renamed.join(','));
    check(p->text(0) == "p", "old name until renamed");

    // the values change in place, the items stay
    rows[0].value = "12.02 V DC";
    sidebar.setInstances(rows, "default");
    check(sidebar.instanceItem("p") == p && own->text(1) == "12.02 V DC", "updated in place");
    rows.removeAt(1);
    sidebar.setInstances(rows, "default");
    check(!sidebar.instanceItem("p") && sidebar.instancesNode()->childCount() == 2, "deleted one gone");
  }

  if (failed)
    qWarning() << failed << "check(s) failed";
  else
    qInfo() << "all checks passed";
  return failed ? 1 : 0;
}
