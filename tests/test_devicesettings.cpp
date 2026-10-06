// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The meter's settings widget outside the settings dialog: keys in, keys out,
// "Advanced" closed for a known model and open for manual settings, and the
// group of each kind of meter instead of the port.

#include <QtWidgets>

#include "ui/devicesettings.h"
#include "device/dmmdecoder.h"

// src/device/transports/calc.cpp registers these with its CalcDevice
static const bool registered = [] {
  DmmDecoder::addConfig({"QtDMM", "Calculated value", "", 0, FrameFormat::Sigrok, 8, 1, 1, 0, 400000, 0, 0, 0});
  DmmDecoder::addConfig({"QtDMM", "Simulated meter", "", 0, FrameFormat::Sigrok, 8, 1, 1, 0, 40000, 0, 0, 0});
  return true;
}();

static int failed = 0;

static void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAIL:" << what;
    ++failed;
  }
}

static bool shown(DeviceSettings &w, const char *name)
{
  QWidget *child = w.findChild<QWidget *>(name);
  return child && child->isVisibleTo(&w);
}

int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);

  DeviceSettings w;
  w.setPorts({"/dev/ttyUSB0", "/dev/ttyUSB1"});

  // 1. a known cable meter: keys round trip, only the port row, Advanced closed
  const QVariantMap ut61e {
    {"DMM/model", "Uni-Trend UT61E"}, {"Port settings/device", "/dev/serial/by-id/usb-Prolific-if00-port0"},
    {"Port settings/baud", "19200"}, {"Port settings/bits", "7"}, {"Port settings/stop-bits", "1"},
    {"Port settings/parity", 2}, {"DMM/data-format", "CyrustekES51922"}, {"DMM/display", "22000"},
    {"DMM/rts", false}, {"DMM/dtr", true}, {"DMM/external-setup", false}, {"DMM/number-of-values", 1}};
  w.load(ut61e);
  QVariantMap out = w.keys();
  for (auto it = ut61e.cbegin(); it != ut61e.cend(); ++it)
    check(out.value(it.key()).toString() == it.value().toString(),
          QString("UT61E %1: %2 != %3").arg(it.key(), out.value(it.key()).toString(), it.value().toString()));
  check(w.device() == "/dev/serial/by-id/usb-Prolific-if00-port0", "UT61E keeps a port not in the list: " + w.device());
  check(!w.portParametersShown(), "UT61E: port parameters not shown");
  check(shown(w, "ButtonGroup11") && !w.findChild<QWidget *>("ui_advanced"), "UT61E: port row, no Advanced button");
  check(!shown(w, "ui_advancedBox") && !shown(w, "ui_protocol_title"), "UT61E: port parameters and protocol hidden");

  // 2. manual settings: Advanced open, the model "Manual"
  w.load({{"DMM/model", "Manual"}, {"Port settings/device", "/dev/ttyUSB1"}, {"Port settings/baud", "2400"}});
  check(w.portParametersShown(), "manual: port parameters shown");
  check(shown(w, "ui_serialBox") && shown(w, "ui_protocol") && shown(w, "ui_protocol_title"),
        "manual: port parameters and protocol shown, with its title");
  check(w.keys().value("DMM/model") == "Manual", "manual: model " + w.keys().value("DMM/model").toString());
  check(w.speed() == 2400, "manual: baud");

  // 3. no frames: the groups are sections with a title above
  check(w.findChild<QGroupBox *>("ui_protocol")->title().isEmpty() && w.findChild<QLabel *>("ui_protocol_title"),
        "sections: the title above the group");

  // 4. calculated value: the formula group, no port, no Advanced
  w.load({{"DMM/model", "QtDMM Calculated value"}, {"DMM/calc-unit", "W"}, {"DMM/calc-expression", "u * i"}});
  check(w.isCalculated(), "calc: model");
  check(w.device() == "calc W u * i", "calc: device " + w.device());
  check(shown(w, "ui_calcGroup") && !shown(w, "ButtonGroup11") && !shown(w, "ui_advancedBox")
          && shown(w, "ui_calcGroup_title"), "calc: formula group only");

  // 5. virtual meter: its own group, the formula from the waveform
  w.load({{"DMM/model", "QtDMM Virtual meter"}, {"DMM/virtual-waveform", 0}, {"DMM/virtual-min", "5"},
          {"DMM/virtual-unit", "V"}, {"DMM/virtual-coupling", "DC"}});
  check(w.isVirtual() && shown(w, "ui_virtualGroup") && !shown(w, "ui_advancedBox"), "virtual: group");
  check(w.device().startsWith("calc V/DC "), "virtual: device " + w.device());
  check(w.keys().value("DMM/virtual-min") == "5", "virtual: min");
  check(w.keys().value("Port settings/device").toString().startsWith("calc "),
        "virtual: the formula as its device, not a port: " + w.keys().value("Port settings/device").toString());

  // 6. Victron: the Bluetooth group instead of the port, the key only here
  w.load({{"DMM/model", "Victron SmartShunt"}, {"Port settings/device", "ble AA:BB:CC:DD:EE:FF"},
          {"Port settings/ble-address", "AA:BB:CC:DD:EE:FF"}, {"Port settings/ble-key", "00112233445566778899aabbccddeeff"}});
  check(w.isBluetooth() && shown(w, "ui_bleGroup") && !shown(w, "ButtonGroup11"), "Victron: Bluetooth group");
  check(w.keys().value("Port settings/device") == "ble AA:BB:CC:DD:EE:FF", "Victron: place " + w.keys().value("Port settings/device").toString());
  check(w.keys().value("Port settings/ble-key") == "00112233445566778899aabbccddeeff", "Victron: key kept");

  // 7. a missing key gets its default (a known model brings its own instead)
  w.load({{"DMM/model", "Manual"}});
  check(w.keys().value("DMM/rts").toBool() && !w.keys().value("DMM/dtr").toBool(), "default: RTS on, DTR off");
  w.load({{"DMM/model", "Uni-Trend UT61E"}, {"DMM/rts", true}});
  check(!w.rts() && w.dtr(), "UT61E: RTS/DTR of the model");

  if (failed)
    qWarning() << failed << "check(s) failed";
  else
    qInfo() << "all checks passed";
  return failed ? 1 : 0;
}
