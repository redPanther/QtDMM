// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A window switching between two of My devices (InstanceWidget::switchDevice(),
// a click in the sidebar): it takes over exactly the entry's keys - a key of
// the meter before stays neither in the window nor goes into the new entry.
#include <QApplication>
#include <QDebug>
#include <QTemporaryDir>

#include "core/devicelibrary.h"
#include "core/settings.h"
#include "service/devicesession.h"
#include "ui/frontpanel.h"
#include "ui/instancewidget.h"
#include "testutil.h"

int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir dir;

  // two simulated meters: B with its own unit and period, A without them
  const QVariantMap keysA { {"DMM/model", "QtDMM Simulated meter"}, {"Port settings/device", "calc V/DC 1.5"} };
  const QVariantMap keysB { {"DMM/model", "QtDMM Simulated meter"}, {"Port settings/device", "calc A/DC 2"},
                            {"DMM/virtual-unit", "A"}, {"DMM/virtual-period", "6"} };
  DeviceLibrary lib(dir.path());
  const QString a = lib.add("Sim A", keysA);
  const QString b = lib.add("Sim B", keysB);
  const QVariantMap entryA = lib.find(a)->keys;
  const QVariantMap entryB = lib.find(b)->keys;
  check(!entryA.contains("DMM/virtual-unit") && entryB.value("DMM/virtual-unit") == "A", "two entries, different keys");

  check(DeviceLibrary::isEntryKey("DMM/virtual-unit") && DeviceLibrary::isEntryKey("Port settings/baud")
          && DeviceLibrary::isEntryKey("Port settings/ble-key") && !DeviceLibrary::isEntryKey("DMM/my-device")
          && !DeviceLibrary::isEntryKey("DMM/configured"),
        "isEntryKey(): the keys an entry can keep, not the window's own");

  {
    Settings old("win", dir.path());
    old.setValues(keysB);
    old.setString("DMM/my-device", b);
    old.setBool("DMM/configured", true);
    old.save();
    old.sync();
  }
  DeviceSession session;
  session.setConfigDir(dir.path());
  FrontPanel panel(&session);   // MainWindow gives the window its display
  InstanceWidget wid("win", dir.path(), &session);
  wid.setPanel(&panel);
  check(wid.currentDevice() == b, "the window starts with Sim B");

  check(wid.switchDevice(a, false), "switch to Sim A");
  const QVariantMap now = wid.settings()->meterKeys(true);
  check(wid.currentDevice() == a, "the window uses Sim A");
  check(!now.contains("DMM/virtual-unit") && !now.contains("DMM/virtual-period"),
        "Sim B's keys left the window: " + QStringList(now.keys()).join(", "));
  check(now.value("Port settings/device") == "calc V/DC 1.5", "Sim A's place");
  check(lib.find(a)->keys == entryA, "nothing of Sim B went into Sim A's entry");

  check(wid.switchDevice(b, false), "switch back to Sim B");
  const QVariantMap back = wid.settings()->meterKeys(true);
  check(back.value("DMM/virtual-unit") == "A" && back.value("DMM/virtual-period") == "6", "Sim B's keys again");
  check(lib.find(b)->keys == entryB && lib.find(a)->keys == entryA, "both entries unchanged");

  if (failed)
  {
    qWarning() << failed << "switch device check(s) failed";
    return 1;
  }
  qInfo() << "All switch device tests passed.";
  return 0;
}
