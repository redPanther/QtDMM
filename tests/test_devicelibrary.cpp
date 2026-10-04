// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// "My devices": the meter keys (Settings::isMeterKey(), the same that a new
// instance does not copy), DeviceLibrary in devices.conf and the stable
// name of a serial port.
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/devicelibrary.h"
#include "core/settings.h"
#include "device/transports/serial.h"

static int failed = 0;

static void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAILED:" << what;
    failed++;
  }
}

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  QTemporaryDir dir;

  // --- 1. the meter keys: exactly what copyConfig() leaves out of a new
  //        instance (apart from the window position and the SCPI switch) ---
  Settings ut61e("ut61e", dir.path());
  ut61e.setString("DMM/model", "UNI-T UT61E");
  ut61e.setString("DMM/data-format", "Cyrustek ES51922");
  ut61e.setBool("DMM/rts", false);
  ut61e.setString("Port settings/device", "SERIAL /dev/ttyUSB0");
  ut61e.setInt("Port settings/baud", 19200);
  ut61e.setString("Port settings/ble-key", "");
  ut61e.setString("Port settings/custom_device0", "RFC2217 bench:4000");
  ut61e.setString("Port settings/sigrok_exe", "/usr/bin/sigrok-cli");
  ut61e.setInt("Graph/sample-time", 5);
  ut61e.setInt("Position/x", 10);
  ut61e.save();
  const QVariantMap keys = ut61e.meterKeys();
  check(keys.size() == 6 && keys.value("DMM/model") == "UNI-T UT61E" && keys.contains("Port settings/baud")
          && !keys.contains("Port settings/custom_device0") && !keys.contains("Port settings/sigrok_exe")
          && !keys.contains("Graph/sample-time"),
        "meter keys: " + QStringList(keys.keys()).join(", "));
  {
    ut61e.copyConfig("copied");
    Settings copied("copied", dir.path());
    check(copied.meterKeys().isEmpty() && copied.getInt("Graph/sample-time") == 5,
          "copyConfig: no meter key, the rest copied");
  }

  // --- 2. the library: add, list, apply into another instance and back ---
  DeviceLibrary lib(dir.path());
  QSignalSpy changed(&lib, &DeviceLibrary::changed);
  check(lib.list().isEmpty(), "library: empty at first");
  const QString name = QString::fromUtf8("UT61E (Werkbank) Ü/ä = ; # [x]");
  const QString id = lib.add(name, keys);
  check(!id.isEmpty() && changed.size() >= 1, "library: add");
  QList<MyDevice> all = lib.list();
  check(all.size() == 1 && all.first().name == name && all.first().keys == keys,
        "library: the entry as added, special characters in the name: " + (all.isEmpty() ? QString() : all.first().name));
  check(all.first().where() == "/dev/ttyUSB0" && all.first().model() == "UNI-T UT61E", "library: where and model");
  check(QFile::exists(dir.path() + "/devices.conf"), "library: devices.conf next to the instances");
  check(!Settings("x", dir.path()).getConfigInstances().contains("devices"), "library: devices.conf is no instance");

  Settings other("other", dir.path());
  other.setString("DMM/model", "Brymen BM869s");
  other.setInt("Graph/sample-time", 1);
  other.save();
  other.setValues(lib.find(id)->keys);
  other.save();
  check(other.meterKeys() == keys && other.getInt("Graph/sample-time") == 1, "apply: the meter keys, nothing else");
  check(lib.match(other.meterKeys()) == id, "match: the device in use");
  other.setString("Port settings/baud", "2400");
  other.save();
  check(lib.match(other.meterKeys()).isEmpty(), "match: changed is not the same");

  // --- 3. names, order, duplicate, rename, remove ---
  check(lib.uniqueName(name) == name + " (2)" && lib.uniqueName("BM869s") == "BM869s", "uniqueName");
  QVariantMap ble = { { "DMM/model", "Victron SmartShunt" }, { "Port settings/device", "BLE" },
                      { "Port settings/ble-address", "AA:BB:CC:DD:EE:FF" } };
  const QString id2 = lib.add("SmartShunt", ble);
  const QString id3 = lib.add("Virtual", { { "DMM/model", "QtDMM Virtual meter" },
                                           { "Port settings/device", "calc V/DC 1 + t" } });
  check(lib.find(id2)->where() == "AA:BB:CC:DD:EE:FF" && lib.find(id3)->where() == "V/DC 1 + t", "where: BLE, calc");
  auto order = [&]
  {
    QStringList n;
    for (const MyDevice &d : lib.list())
      n << d.name.left(5);
    return n.join(' ');
  };
  check(order() == "UT61E Smart Virtu", "order: as added, got " + order());
  lib.move(id3, 0);
  check(order() == "Virtu UT61E Smart", "move: got " + order());
  const QString dup = lib.duplicate(id);
  check(order() == "Virtu UT61E UT61E Smart" && lib.find(dup)->name == name + " (2)", "duplicate: right after it, got " + order());
  check(lib.rename(dup, "Bench 2") && lib.find(dup)->name == "Bench 2" && !lib.rename(dup, "  "), "rename");
  check(lib.update(id2, { { "DMM/model", "Victron SmartShunt" } }) && lib.find(id2)->keys.size() == 1, "update replaces the keys");
  check(lib.remove(dup) && !lib.find(dup) && lib.list().size() == 3, "remove");

  // --- 4. two instances: one sees what the other wrote, nothing is lost ---
  {
    DeviceLibrary a(dir.path()), b(dir.path());
    const QString ia = a.add("A", { { "DMM/model", "A" } });
    const QString ib = b.add("B", { { "DMM/model", "B" } });
    check(a.find(ib) && b.find(ia) && a.list().size() == 5, "two instances: both entries kept");
    QSignalSpy seen(&a, &DeviceLibrary::changed);
    b.rename(ia, "A2");
    QDeadlineTimer wait(2000);
    while (seen.isEmpty() && !wait.hasExpired())
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    check(!seen.isEmpty() && a.find(ia)->name == "A2", "two instances: a change in one is changed() in the other");
  }

#ifdef Q_OS_LINUX
  // --- 5. the stable port name: the link in by-id that points to the port ---
  {
    QDir(dir.path()).mkpath("dev/serial/by-id");
    const QString tty = dir.path() + "/dev/ttyUSB0";
    QFile f(tty);
    f.open(QIODevice::WriteOnly);
    f.close();
    QFile(dir.path() + "/dev/ttyUSB1").open(QIODevice::WriteOnly);
    const QString byId = dir.path() + "/dev/serial/by-id";
    QFile::link("../../ttyUSB1", byId + "/usb-Other_cable-if00-port0");
    QFile::link("../../ttyUSB0", byId + "/usb-WCH.CN_USB_Quad_Serial_0123-if00-port0");
    check(SerialDevice::stablePortName(tty, byId) == byId + "/usb-WCH.CN_USB_Quad_Serial_0123-if00-port0",
          "stable: the by-id link, got " + SerialDevice::stablePortName(tty, byId));
    check(SerialDevice::stablePortName(dir.path() + "/dev/ttyS0", byId) == dir.path() + "/dev/ttyS0",
          "stable: no link, the port as it is");
    const QString link = byId + "/usb-Other_cable-if00-port0";
    check(SerialDevice::stablePortName(link, byId) == link, "stable: a by-id name stays");
  }
#endif

  if (failed)
  {
    qWarning() << failed << "device library check(s) failed";
    return 1;
  }
  qInfo() << "All device library tests passed.";
  return 0;
}
