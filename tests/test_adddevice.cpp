// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The assistant "Add device" without a main window: the order of its pages
// per connection, the models each connection offers, the name that follows
// the model until it is typed in, Next only when the meter is complete, and
// the two buttons that finish it.

#include <QtWidgets>
#include <QTemporaryDir>

#include "ui/dialogs/adddevicedlg.h"
#include "ui/devicesettings.h"
#include "core/devicelibrary.h"

// src/device/transports/calc.cpp registers these with its CalcDevice
static const bool registered = [] {
  DmmDecoder::addConfig({"QtDMM", "Calculated value", "", 0, FrameFormat::Sigrok, 8, 1, 1, 0, 400000, 0, 0, 0});
  DmmDecoder::addConfig({"QtDMM", "Virtual meter", "", 0, FrameFormat::Sigrok, 8, 1, 1, 0, 40000, 0, 0, 0});
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

int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir dir;
  DeviceLibrary library(dir.path());
  library.add("QtDMM Virtual meter", { { "DMM/model", "QtDMM Virtual meter" } });

  // 1. the models per connection
  for (const DmmDecoder::DMMInfo &info : DmmDecoder::getDeviceConfigurations())
  {
    int kinds = 0;
    for (AddDeviceDlg::Connection c : { AddDeviceDlg::Cable, AddDeviceDlg::Bluetooth, AddDeviceDlg::Sigrok, AddDeviceDlg::Simulated })
      kinds += AddDeviceDlg::offers(c, info) ? 1 : 0;
    check(kinds == 1, "one connection for " + info.name);
    check(AddDeviceDlg::offers(AddDeviceDlg::Network, info) == AddDeviceDlg::offers(AddDeviceDlg::Cable, info),
          "network offers the cable meters: " + info.name);
  }
  auto offered = [](AddDeviceDlg::Connection c, const QString &name)
  {
    for (const DmmDecoder::DMMInfo &info : DmmDecoder::getDeviceConfigurations())
      if (info.name == name)
        return AddDeviceDlg::offers(c, info);
    return false;
  };
  check(offered(AddDeviceDlg::Cable, "Uni-Trend UT61E"), "UT61E is a cable meter");
  check(offered(AddDeviceDlg::Bluetooth, "Victron SmartShunt"), "SmartShunt is Bluetooth");
  check(offered(AddDeviceDlg::Bluetooth, "Uni-Trend UT61E+ (UT-D07B Bluetooth) *"), "UT61E+ with UT-D07B is Bluetooth");
  check(offered(AddDeviceDlg::Cable, "Uni-Trend UT61E+ *"), "UT61E+ with its cable is a cable meter");
  check(offered(AddDeviceDlg::Sigrok, "Keysight 34465A (sigrok) *"), "34465A is sigrok");
  check(offered(AddDeviceDlg::Simulated, "QtDMM Calculated value"), "calculated value is simulated");

  // 2. page order: sigrok and simulated go from page 1 straight to page 3
  {
    AddDeviceDlg dlg(&library);
    check(dlg.page() == AddDeviceDlg::ConnectionPage, "starts on page 1");
    check(!dlg.canGoNext(), "no Next on page 1");
    dlg.chooseConnection(AddDeviceDlg::Sigrok);
    check(dlg.page() == AddDeviceDlg::DevicePage, "sigrok: page 3");
    check(dlg.settings()->isSigrokMeter(), "sigrok: a sigrok model chosen");
    for (const QString &m : dlg.settings()->models())
      check(offered(AddDeviceDlg::Sigrok, m), "sigrok offers only sigrok models: " + m);
    check(!dlg.settings()->models().contains("Uni-Trend UT61E"), "sigrok: no cable meter");
    dlg.back();
    check(dlg.page() == AddDeviceDlg::ConnectionPage, "back to page 1");

    dlg.chooseConnection(AddDeviceDlg::Simulated);
    check(dlg.page() == AddDeviceDlg::DevicePage && dlg.settings()->isVirtual(), "simulated: page 3, the virtual meter");
    check(dlg.name() == "QtDMM Virtual meter (2)", "name: unique in My devices: " + dlg.name());
    check(dlg.canGoNext(), "virtual meter: Next");
    dlg.next();
    check(dlg.page() == AddDeviceDlg::TargetPage, "page 4");
    dlg.back();
    check(dlg.page() == AddDeviceDlg::DevicePage, "back to page 3");
    dlg.next();
    dlg.findChild<QToolButton *>("ui_newWindow")->click();
    check(dlg.result() == QDialog::Accepted && dlg.target() == AddDeviceDlg::NewWindow, "new window finishes");
    check(dlg.keys().value("DMM/model") == "QtDMM Virtual meter", "keys: the model");
  }

  // 3. the name follows the model until it is typed in
  {
    AddDeviceDlg dlg(&library);
    dlg.chooseConnection(AddDeviceDlg::Bluetooth);
    dlg.settings()->load({ { "DMM/model", "Victron SmartShunt" } });
    check(dlg.name() == "Victron SmartShunt", "name follows the model: " + dlg.name());
    check(!dlg.canGoNext(), "Victron without address and key: no Next");
    dlg.settings()->load({ { "DMM/model", "Victron SmartShunt" }, { "Port settings/ble-address", "AA:BB:CC:DD:EE:FF" },
                           { "Port settings/ble-key", "00112233445566778899aabbccddeeff" } });
    check(dlg.canGoNext(), "Victron with address and key: Next");
    auto *name = dlg.findChild<QLineEdit *>("ui_name");
    name->setText("Battery");
    name->setModified(true);
    dlg.settings()->load({ { "DMM/model", "Victron SmartSolar MPPT" } });
    check(dlg.name() == "Battery", "a typed name stays: " + dlg.name());
    name->clear();
    check(!dlg.canGoNext(), "no name: no Next");
    dlg.reject();
    check(dlg.target() == AddDeviceDlg::NoTarget, "cancel: no target");
  }

  // 4. cable: manual settings allowed, the port decides; this window
  {
    AddDeviceDlg dlg(&library);
    dlg.setPorts({ "/dev/ttyUSB0" });
    dlg.setCurrentDevice("UT803");
    dlg.chooseConnection(AddDeviceDlg::Cable);
    check(dlg.name() == "Meter", "manual settings: a plain name: " + dlg.name());
    check(dlg.canGoNext(), "manual with a port: Next");
    dlg.settings()->load({ { "DMM/model", "Uni-Trend UT61E" }, { "Port settings/device", "/dev/ttyUSB0" } });
    check(dlg.name() == "Uni-Trend UT61E", "name follows: " + dlg.name());
    dlg.next();
    QToolButton *here = dlg.findChild<QToolButton *>("ui_thisWindow");
    check(here->text().contains("UT803"), "this window names the meter now: " + here->text());
    here->click();
    check(dlg.target() == AddDeviceDlg::ThisWindow, "this window finishes");
    check(dlg.keys().value("Port settings/device") == "/dev/ttyUSB0", "keys: the port");
  }

  // 5. network: an address typed in gets its port type
  {
    AddDeviceDlg dlg(&library);
    dlg.chooseConnection(AddDeviceDlg::Network);
    dlg.settings()->load({ { "DMM/model", "Uni-Trend UT61E" }, { "Port settings/device", "bench:4000" } });
    check(dlg.keys().value("Port settings/device") == "RFC2217 bench:4000",
          "network: " + dlg.keys().value("Port settings/device").toString());
  }

  if (failed)
    qWarning() << failed << "check(s) failed";
  else
    qInfo() << "all checks passed";
  return failed ? 1 : 0;
}
