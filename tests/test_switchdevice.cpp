// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window's meter before 26.4 (InstanceWidget::takeOldDevice()): its entry
// of My devices, once, or a new one for a meter set up in the window alone; the window keeps no meter of its own, the entry stays
// as it is. And which keys an entry keeps (DeviceLibrary::isEntryKey()).
#include <QApplication>
#include <QDebug>
#include <QTemporaryDir>

#include "core/devicelibrary.h"
#include "core/settings.h"
#include "service/devicesession.h"
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
  InstanceWidget wid("win", dir.path(), &session);
  check(wid.takeOldDevice() == b, "the window's meter was Sim B");
  check(wid.settings()->getString("DMM/my-device").isEmpty() && wid.takeOldDevice().isEmpty(), "once");
  check(session.controllers().isEmpty(), "the window opens no meter of its own");
  check(lib.find(b)->keys == entryB && lib.find(a)->keys == entryA, "both entries unchanged");
  {
    Settings gone("gone", dir.path());
    gone.setString("DMM/my-device", "nosuch");
    gone.save();
    gone.sync();
  }
  InstanceWidget none("gone", dir.path(), &session);
  check(none.takeOldDevice().isEmpty(), "an entry gone, no meter set up: none");

  // a meter set up in the window alone: an entry of its own, once; one at
  // the place of an entry with its model takes that entry; "Manual" alone
  // (written at every exit before 26.4) is no meter
  auto oldWindow = [&](const QString &id, const QVariantMap &keys, bool configured)
  {
    Settings old(id, dir.path());
    old.setValues(keys);
    if (configured)
      old.setBool("DMM/configured", true);
    old.save();
    old.sync();
  };
  oldWindow("manual", { {"DMM/model", "Manual"}, {"Port settings/device", "calc V/DC 2"} }, true);
  InstanceWidget manual("manual", dir.path(), &session);
  const QString made = manual.takeOldDevice();
  check(!made.isEmpty() && lib.find(made) && lib.find(made)->name == "Meter"
          && lib.find(made)->keys.value("Port settings/device") == "calc V/DC 2",
        "Manual: an entry of its own, named Meter");
  check(manual.takeOldDevice().isEmpty(), "Manual: once");
  oldWindow("sima", keysA, false);
  InstanceWidget sima("sima", dir.path(), &session);
  check(sima.takeOldDevice() == a, "a model at an entry's place: that entry");
  oldWindow("plain", { {"DMM/model", "Manual"} }, false);
  InstanceWidget plain("plain", dir.path(), &session);
  const int entries = int(lib.list().size());
  check(plain.takeOldDevice().isEmpty() && lib.list().size() == entries, "Manual alone: no meter");

  if (failed)
  {
    qWarning() << failed << "old window device check(s) failed";
    return 1;
  }
  qInfo() << "All old window device tests passed.";
  return 0;
}
