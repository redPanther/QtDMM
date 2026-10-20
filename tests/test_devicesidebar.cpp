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
#include <QTest>

#include "core/devicelibrary.h"
#include "ui/devicesettings.h"
#include "ui/devicesidebar.h"
#include "ui/dialogs/devicesettingsdlg.h"
#include "testutil.h"

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
  {
    // the dot blinks with the readings: the other green each time
    const QImage on = sidebar.deviceItem(tfa)->icon(0).pixmap(16).toImage();
    sidebar.setCurrent(tfa, true, true);
    const QImage blink = sidebar.deviceItem(tfa)->icon(0).pixmap(16).toImage();
    sidebar.setCurrent(tfa, false);
    const QImage off = sidebar.deviceItem(tfa)->icon(0).pixmap(16).toImage();
    check(on != blink && on != off && blink != off, "the dot: green, the other green, grey");
    sidebar.setCurrent(tfa, true);
  }

  // 2. a click switches, but not to the one in use
  QStringList switched;
  QObject::connect(&sidebar, &DeviceSidebar::switchRequested, [&](const QString &id) { switched << id; });
  // after the double-click time: a double click would be Info instead
  const int wait = QApplication::doubleClickInterval() + 100;
  // (waiting, the sidebar may fill anew from the library's file: the items
  // are looked up again after it)
  Q_EMIT sidebar.itemClicked(bench, 0);
  check(switched.isEmpty(), "a click switches not at once");
  QTest::qWait(wait);
  Q_EMIT sidebar.itemClicked(sidebar.deviceItem(tfa), 0);
  Q_EMIT sidebar.itemClicked(sidebar.devicesNode(), 0);
  QTest::qWait(wait);
  bench = sidebar.deviceItem(ut61e);
  check(switched == QStringList({ ut61e }), "click switches: " + switched.join(','));
  {
    // a double click: info and settings, no switch
    QStringList info;
    const auto conn = QObject::connect(&sidebar, &DeviceSidebar::settingsRequested, [&](const QString &id) { info << id; });
    Q_EMIT sidebar.itemClicked(bench, 0);
    Q_EMIT sidebar.itemDoubleClicked(bench, 0);
    Q_EMIT sidebar.itemClicked(bench, 0);   // the release of the second click
    QTest::qWait(wait);
    bench = sidebar.deviceItem(ut61e);
    check(info == QStringList({ ut61e }) && switched.size() == 1,
          QString("double click: info, no switch (%1 / %2)").arg(info.join(',')).arg(switched.size()));
    QObject::disconnect(conn);
    check(sidebar.deviceItem(tfa)->toolTip(0).contains("In use in this window")
            && sidebar.deviceItem(tfa)->toolTip(0).contains("Double-click")
            && !bench->toolTip(0).contains("In use"),
          "the tooltip: in use here, how to read more: " + sidebar.deviceItem(tfa)->toolTip(0));
  }

  // the last row adds a device; it is no device itself
  QTreeWidgetItem *add = sidebar.devicesNode()->child(sidebar.devicesNode()->childCount() - 1);
  int adds = 0;
  QObject::connect(&sidebar, &DeviceSidebar::addDeviceRequested, [&] { adds++; });
  Q_EMIT sidebar.itemClicked(add, 0);
  check(add->text(0).startsWith('+') && adds == 1 && switched.size() == 1, "the last row asks for Add device");
  check(!(add->flags() & (Qt::ItemIsDragEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable)), "the add row stays put");

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
  {
    // Info first: the device at a glance, its state, the maker's website, the help
    DeviceSettingsDlg dlg("Bench");
    dlg.load({ { "DMM/model", "Uni-Trend UT61E" }, { "Port settings/device", "SERIAL /dev/ttyUSB0" } });
    check(dlg.currentTab() == DeviceSettingsDlg::InfoTab, "the dialog opens on Info");
    const QString summary = dlg.findChild<QLabel *>("ui_deviceInfo")->text();
    check(summary.contains("Bench") && summary.contains("UT61E") && summary.contains("/dev/ttyUSB0"),
          "Info: name, model, connection: " + summary);
    int asked = 0;
    dlg.setStatus([&] { ++asked; return QString("<p>In use in this window.</p>"); });
    check(asked == 1 && dlg.statusText().contains("In use"), "Info: the state at once");
    QTest::qWait(1100);
    check(asked >= 2, QString("Info: the state again each second, asked %1 times").arg(asked));
    check(dlg.linksText().contains("https://www.uni-trend.com/") && dlg.linksText().contains("help:supported-devices.md"),
          "Info: the maker's website and the help: " + dlg.linksText());
    QString page;
    QObject::connect(&dlg, &DeviceSettingsDlg::helpRequested, [&](const QString &p) { page = p; });
    Q_EMIT dlg.findChild<QLabel *>("ui_deviceLinks")->linkActivated("help:supported-devices.md");
    check(page == "supported-devices.md", "Info: the help link asks for its page");
    dlg.showTab(DeviceSettingsDlg::SettingsTab);
    check(dlg.currentTab() == DeviceSettingsDlg::SettingsTab && dlg.settings()->isVisibleTo(&dlg), "the Settings tab");
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
    check(own && own->isExpanded() && !p->isExpanded() && !u->isExpanded(), "at first only this window's is open");
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

  // 7b. this window's values below its instance: a row each with the reading,
  //     the curve's colour while in the graph, the menu per value
  {
    QList<DeviceSidebar::Instance> rows;
    rows << DeviceSidebar::Instance { "default", "Victron SmartShunt", "13.29 V DC", true, true, {} }
         << DeviceSidebar::Instance { "u", "Bench", "OL", true, false, {} };
    const QList<DeviceSidebar::Value> values { { 0, "Voltage DC", "13.29 V", QColor("#3b82f6") },
                                               { 1, "Current DC", "-14.9 A", QColor() },
                                               { 2, "Value 3", "", QColor() } };
    rows[0].values = values;
    rows[1].values = values;   // another window's: not shown here
    sidebar.setInstances(rows, "default");
    QTreeWidgetItem *own = sidebar.instanceItem("default");
    check(own && own->childCount() == 4 && own->child(0)->text(0) == "Victron SmartShunt",
          QString("the device, then three values, got %1 rows").arg(own ? own->childCount() : -1));
    check(sidebar.instanceItem("u")->childCount() == 1, "another window's values are not here");
    QTreeWidgetItem *current = sidebar.valueItem(1);
    check(current && current->text(0) == "2  Current DC" && current->text(1) == "-14.9 A", "the row of value 2");
    check(sidebar.valueItem(0)->toolTip(0).contains("graph") && !current->toolTip(0).contains("graph"),
          "the graph's values say so");
    rows[0].values[1].text = "-15.0 A";
    sidebar.setInstances(rows, "default");
    check(sidebar.valueItem(1) == current && current->text(1) == "-15.0 A", "the reading in place");
    own->setExpanded(false);
    sidebar.setInstances(rows, "default");
    check(!own->isExpanded(), "folded away it stays so");
    own->setExpanded(true);
    int menuFor = -1;
    QObject::connect(&sidebar, &DeviceSidebar::valueMenuRequested, [&](int id, const QPoint &) { menuFor = id; });
    const QRect r = sidebar.visualItemRect(current);
    QContextMenuEvent menu(QContextMenuEvent::Mouse, r.center(), sidebar.viewport()->mapToGlobal(r.center()));
    QCoreApplication::sendEvent(sidebar.viewport(), &menu);
    check(menuFor == 1, QString("the menu of value 2, got %1").arg(menuFor));
    rows[0].values = values.mid(0, 1);
    sidebar.setInstances(rows, "default");
    check(own->childCount() == 1 && !sidebar.valueItem(0), "one value: no rows");
    // where the row is short: unit and coupling, the full name in the tooltip
    rows[0].values = values;
    rows[0].values[1].shortName = "A DC";
    sidebar.setInstances(rows, "default");
    check(sidebar.valueItem(1)->text(0) == "2  A DC" && sidebar.valueItem(1)->toolTip(0) == "Current DC",
          "the short name in the row, the full one in the tooltip");
  }

  // 8. the services: port or off, the address to copy, settings by a double click
  {
    DeviceSidebar::Service scpi { "scpi", "SCPI", "5025", "localhost:5025", "tip", true, true };
    DeviceSidebar::Service hislip { "hislip", "HiSLIP", "off", "", "off tip", false, false };
    sidebar.setServices({ scpi, hislip });
    QTreeWidgetItem *node = sidebar.servicesNode();
    check(node->childCount() == 2, "two services");
    QTreeWidgetItem *s = node->child(0);
    check(s->text(0) == "SCPI" && s->text(1) == "5025" && s->data(1, Qt::UserRole).toString() == "localhost:5025",
          "SCPI row: " + s->text(1));
    check(node->child(1)->text(1) == "off", "HiSLIP off");
    check(!(s->flags() & Qt::ItemIsEditable) && !(node->flags() & Qt::ItemIsDropEnabled), "not renamed, no drops");
    int settings = 0;
    QObject::connect(&sidebar, &DeviceSidebar::serviceSettingsRequested, [&] { ++settings; });
    Q_EMIT sidebar.itemDoubleClicked(node->child(1), 0);
    check(settings == 1, "double click: settings");
    hislip.running = true;
    hislip.value = "4880";
    sidebar.setServices({ scpi, hislip });
    check(node->child(0) == s && node->child(1)->text(1) == "4880", "updated in place");
    sidebar.setServices({ scpi });
    check(node->childCount() == 1, "one less");
  }

  if (failed)
    qWarning() << failed << "check(s) failed";
  else
    qInfo() << "all checks passed";
  return failed ? 1 : 0;
}
