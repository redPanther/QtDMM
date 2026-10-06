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

int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir dir;
  DeviceLibrary library(dir.path());
  library.add("Simulated meter", { { "DMM/model", "QtDMM Virtual meter" } });   // the old model name

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
    dlg.setDiscoverers([](AddDeviceDlg::Connection) { return QList<Discoverer *>(); });
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
    check(dlg.name() == "Simulated meter (2)", "name: unique in My devices: " + dlg.name());
    check(dlg.canGoNext(), "virtual meter: Next");
    dlg.next();
    check(dlg.page() == AddDeviceDlg::TargetPage, "page 4");
    dlg.back();
    check(dlg.page() == AddDeviceDlg::DevicePage, "back to page 3");
    dlg.next();
    dlg.findChild<QToolButton *>("ui_newWindow")->click();
    check(dlg.result() == QDialog::Accepted && dlg.target() == AddDeviceDlg::NewWindow, "new window finishes");
    check(dlg.keys().value("DMM/model") == "QtDMM Simulated meter", "keys: the model");
  }

  // the searches of the real dialog look at this machine; here the finds are given
  auto noSearch = [](AddDeviceDlg::Connection) { return QList<Discoverer *>(); };

  // 2b. the empty start: no page 4, page 3 finishes with Add into this window
  {
    AddDeviceDlg dlg(&library);
    dlg.setDiscoverers(noSearch);
    dlg.setTargetChoice(false);
    dlg.chooseConnection(AddDeviceDlg::Simulated);
    QPushButton *add = nullptr;
    for (QPushButton *b : dlg.findChildren<QPushButton *>())
      if (b->text() == "A&dd" && !b->isHidden())
        add = b;
    check(add && add->isEnabled(), "empty start: Add on page 3");
    dlg.next();
    check(dlg.page() != AddDeviceDlg::TargetPage, "empty start: no page 4");
    check(dlg.result() == QDialog::Accepted && dlg.target() == AddDeviceDlg::ThisWindow,
          "empty start: into this window");
  }

  // 3. the name follows the model until it is typed in
  {
    AddDeviceDlg dlg(&library);
    dlg.setDiscoverers(noSearch);
    dlg.chooseConnection(AddDeviceDlg::Bluetooth);
    check(dlg.page() == AddDeviceDlg::PortPage, "Bluetooth: page 2");
    check(!dlg.canGoNext(), "page 2 without a port: no Next");
    dlg.setPort("AA:BB:CC:DD:EE:FF");
    check(dlg.canGoNext(), "page 2 with an address typed in: Next");
    dlg.next();
    check(dlg.page() == AddDeviceDlg::DevicePage, "Bluetooth: page 3");
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
    dlg.back();
    check(dlg.page() == AddDeviceDlg::PortPage, "back from page 3 to page 2");
    dlg.back();
    check(dlg.page() == AddDeviceDlg::ConnectionPage, "back to page 1");
    dlg.reject();
    check(dlg.target() == AddDeviceDlg::NoTarget, "cancel: no target");
  }

  // 4. cable: the finds as cards, recognised ones first; a port typed in; this window
  {
    AddDeviceDlg dlg(&library);
    dlg.setDiscoverers(noSearch);
    dlg.setCurrentDevice("UT803");
    const QString hidPlace = DeviceLibrary::place({ { "Port settings/device", "HID 0x1a86:0xe008 /dev/hidraw3" } });
    dlg.setPlacesInUse({ { hidPlace, "In use by the instance u" } });
    dlg.chooseConnection(AddDeviceDlg::Cable);
    check(dlg.page() == AddDeviceDlg::PortPage, "cable: page 2");
    Candidate serial;
    serial.kind = Candidate::Serial;
    serial.key = "/dev/ttyUSB0";
    serial.title = "USB-serial adapter (CH340)";
    serial.keys.insert("Port settings/device", "SERIAL /dev/ttyUSB0");
    Candidate hid;
    hid.kind = Candidate::UsbCable;
    hid.key = "/dev/hidraw3";
    hid.title = "UNI-T, serial meters";
    hid.keys.insert("Port settings/device", "HID 0x1a86:0xe008 /dev/hidraw3");
    hid.models = QStringList { "Uni-Trend UT61E", "Uni-Trend UT803" };
    Candidate locked = serial;
    locked.key = "/dev/ttyUSB1";
    locked.keys.insert("Port settings/device", "SERIAL /dev/ttyUSB1");
    locked.problem = "/dev/ttyUSB1 belongs to the group dialout";
    dlg.addCandidate(serial);
    dlg.addCandidate(hid);
    dlg.addCandidate(locked);
    dlg.addCandidate(serial);   // the same find twice: one card
    check(dlg.candidates() == QStringList({ "/dev/hidraw3", "/dev/ttyUSB0", "/dev/ttyUSB1" }),
          "recognised first, each once: " + dlg.candidates().join(", "));
    auto *cards = dlg.findChild<QListWidget *>("ui_cards");
    check(cards->item(0)->text().contains("In use by the instance u"), "card: in use: " + cards->item(0)->text());
    dlg.chooseCandidate("/dev/ttyUSB1");
    check(!dlg.canGoNext(), "a port QtDMM may not open: no Next");
    dlg.chooseCandidate("/dev/hidraw3");
    check(dlg.findChild<QLineEdit *>("ui_port")->text() == "HID 0x1a86:0xe008 /dev/hidraw3", "card fills the port field");
    dlg.next();
    check(dlg.settings()->keys().value("DMM/model") == "Uni-Trend UT61E", "the likeliest model: "
          + dlg.settings()->keys().value("DMM/model").toString());
    check(dlg.name() == "Uni-Trend UT61E", "name after the model: " + dlg.name());
    check(dlg.knownDevice().isEmpty(), "a new device");
    dlg.back();
    dlg.setPort("/dev/ttyACM0");   // typed in
    dlg.next();
    check(dlg.keys().value("Port settings/device") == "SERIAL /dev/ttyACM0", "typed port: " + dlg.keys().value("Port settings/device").toString());
    check(dlg.name() == "Meter", "nothing known of the port: manual settings: " + dlg.name());
    dlg.next();
    QToolButton *here = dlg.findChild<QToolButton *>("ui_thisWindow");
    check(here->text().contains("as the current device") && here->toolTip().contains("UT803"),
          "this window: as the current device, the meter now in the tooltip: " + here->toolTip());
    here->click();
    check(dlg.target() == AddDeviceDlg::ThisWindow, "this window finishes");
  }

  // 4b. a find that is one of My devices: that entry, no second one
  {
    const QString ut61e = library.add("Bench UT61E", { { "DMM/model", "Uni-Trend UT61E" },
                                                       { "Port settings/device", "HID 0x1a86:0xe008 /dev/hidraw2" } });
    AddDeviceDlg dlg(&library);
    dlg.setDiscoverers(noSearch);
    dlg.chooseConnection(AddDeviceDlg::Cable);
    Candidate hid;
    hid.kind = Candidate::UsbCable;
    hid.key = "/dev/hidraw5";   // plugged in again: another number, the same cable type
    hid.title = "UNI-T, serial meters";
    hid.keys.insert("Port settings/device", "HID 0x1a86:0xe008 /dev/hidraw5");
    hid.models = QStringList { "Uni-Trend UT803", "Uni-Trend UT61E" };
    dlg.addCandidate(hid);
    check(dlg.findChild<QListWidget *>("ui_cards")->item(0)->text().contains("Bench UT61E"), "card: already in My devices");
    dlg.chooseCandidate("/dev/hidraw5");
    dlg.next();
    check(dlg.knownDevice() == ut61e, "known find: the entry");
    check(dlg.name() == "Bench UT61E", "known find: its name: " + dlg.name());
    check(dlg.settings()->keys().value("DMM/model") == "Uni-Trend UT61E", "known find: its model, not the family's first");
    check(dlg.keys().value("Port settings/device").toString().endsWith("/dev/hidraw5"), "known find: the place found now");
    dlg.settings()->load({ { "DMM/model", "Uni-Trend UT803" }, { "Port settings/device", "HID 0x1a86:0xe008 /dev/hidraw5" } });
    check(dlg.knownDevice().isEmpty(), "another model at that place: a new device");
  }

  // 5. network: page 2 with the bridges found, or host:port typed in
  {
    AddDeviceDlg dlg(&library);
    dlg.setDiscoverers(noSearch);
    dlg.chooseConnection(AddDeviceDlg::Network);
    check(dlg.page() == AddDeviceDlg::PortPage, "network: page 2");
    Candidate bridge;
    bridge.kind = Candidate::Network;
    bridge.key = "192.168.1.20:4711";
    bridge.title = "qtdmm-bridge on dory";
    bridge.keys.insert("Port settings/device", "RFC2217 192.168.1.20:4711");
    dlg.addCandidate(bridge);
    dlg.chooseCandidate(bridge.key);
    check(dlg.findChild<QLineEdit *>("ui_port")->text() == "192.168.1.20:4711",
          "network: the bridge port without its type: " + dlg.findChild<QLineEdit *>("ui_port")->text());
    dlg.next();
    check(dlg.page() == AddDeviceDlg::DevicePage, "network: on to page 3");
    check(dlg.keys().value("Port settings/device") == "RFC2217 192.168.1.20:4711",
          "network: the bridge port: " + dlg.keys().value("Port settings/device").toString());
    dlg.back();
    check(dlg.page() == AddDeviceDlg::PortPage, "network: back to page 2");
    dlg.setPort("bench:4000");
    dlg.next();
    check(dlg.keys().value("Port settings/device") == "RFC2217 bench:4000",
          "network typed: " + dlg.keys().value("Port settings/device").toString());
  }

  if (failed)
    qWarning() << failed << "check(s) failed";
  else
    qInfo() << "all checks passed";
  return failed ? 1 : 0;
}
