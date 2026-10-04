// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "device/discovery/discovery.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSerialPortInfo>
#include <algorithm>

#include "device/dmmdecoder.h"
#include "device/transports/hidreader.h"   // hidapi
#include "service/mdnsbrowser.h"

#ifdef QTDMM_WITH_BLE
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothLocalDevice>
#include "device/transports/blegatt.h"
#include "device/victronble.h"
#endif

namespace
{
QString ids(quint16 vid, quint16 pid)
{
  return QString("%1:%2").arg(vid, 4, 16, QLatin1Char('0')).arg(pid, 4, 16, QLatin1Char('0'));
}

// the likeliest first: the model most people have with that cable
QStringList likeliestFirst(QStringList models, const QString &first)
{
  std::sort(models.begin(), models.end(), [](const QString &a, const QString &b)
  {
    return a.localeAwareCompare(b) < 0;
  });
  const int i = int(models.indexOf(first));
  if (i > 0)
    models.move(i, 0);
  return models;
}
}

// ---------------------------------------------------------------- families

QStringList Families::models(FrameFormat::DataFormat format, bool serial)
{
  QStringList out;
  for (const DmmDecoder::DMMInfo &m : DmmDecoder::getDeviceConfigurations())
    if ((format == FrameFormat::Invalid || m.protocol == format) && (m.baud > 0) == serial)
      out << m.name;
  return likeliestFirst(out, QString());
}

QStringList Families::vendorModels(const QString &vendor)
{
  QStringList out;
  for (const DmmDecoder::DMMInfo &m : DmmDecoder::getDeviceConfigurations())
    if (m.vendor == vendor && m.baud > 0)
      out << m.name;
  return likeliestFirst(out, QString());
}

QStringList Families::preferByName(QStringList models, const QString &deviceName)
{
  const QString word = deviceName.section(' ', 0, 0);
  if (word.isEmpty())
    return models;
  for (int i = 0; i < models.size(); ++i)
    if (models[i].section(' ', 1, 1).startsWith(word, Qt::CaseInsensitive))
    {
      models.move(i, 0);
      break;
    }
  return models;
}

std::optional<Families::Family> Families::forUsb(quint16 vid, quint16 pid)
{
  const QString id = ids(vid, pid);
  if (id == "10c4:ea80" || id == "1a86:e429")
  {
    // the UT-D09 cable: the protocol of the UT61+ series
    QStringList models;
    for (const QString &m : Families::models(FrameFormat::UniTUT61Plus, true))
      models << m;
    return Family { QCoreApplication::translate("Discovery", "UNI-T UT61B+, UT61D+, UT61E+ or UT161"), "UT-D09",
                    QCoreApplication::translate("Discovery", "Switch the meter on; it sends as soon as the cable is plugged in."), "HID",
                    likeliestFirst(models, "Uni-Trend UT61E+ *") };
  }
  if (id == "1a86:e008" || id == "04fa:2490")
  {
    // the UT-D04 cable: an optical serial link, many UNI-T meters
    QStringList models;
    for (const DmmDecoder::DMMInfo &m : DmmDecoder::getDeviceConfigurations())
      if (m.vendor == "Uni-Trend" && m.baud > 0 && m.protocol != FrameFormat::UniTUT61Plus)
        models << m.name;
    return Family { QCoreApplication::translate("Discovery", "UNI-T meter with the UT-D04 cable"), "UT-D04",
                    QCoreApplication::translate("Discovery", "Press RS232 (or PC) on the meter, else it sends nothing."), "HID",
                    likeliestFirst(models, "Uni-Trend UT61E") };
  }
  if (id == "0820:0001")
  {
    QStringList models;
    for (const DmmDecoder::DMMInfo &m : DmmDecoder::getDeviceConfigurations())
      if (m.vendor == "Brymen" && m.baud == 0)
        models << m.name;
    return Family { QCoreApplication::translate("Discovery", "Brymen BM52x, BM82x or BM86x"), "BU-86X",
                    QCoreApplication::translate("Discovery", "Put the IR adapter on the meter and switch its PC link on."), "HID", likeliestFirst(models, QString()) };
  }
  if (id == "04d9:a052")
    return Family { QCoreApplication::translate("Discovery", "TFA AIRCO2NTROL CO2 monitor"), QString(), QString(), "HIDHOLTEK",
                    likeliestFirst(Families::models(FrameFormat::TfaAirControl, false), "TFA Dostmann AIRCO2NTROL Mini *") };
  return std::nullopt;
}

QString Families::adapter(quint16 vid, quint16 pid)
{
  if (vid == 0x0403)
    return "FTDI";
  if (vid == 0x1a86 && (pid == 0x7523 || pid == 0x5523 || pid == 0x7522))
    return "CH340";
  if ((vid == 0x067b) || (vid == 0x0557 && pid == 0x2008))
    return "PL2303";
  if (vid == 0x10c4 && (pid == 0xea60 || pid == 0xea70 || pid == 0xea71))
    return "CP210x";
  return QString();
}

// ---------------------------------------------------------------- access

QString udevRules()
{
  QString rules = "# QtDMM: the logged-in user may open the USB-HID cables and sensors of meters\n";
  for (const auto &[vid, pid, what] : { std::tuple { "1a86", "e008", "WCH CH9325 (UNI-T UT-D04)" },
                                         std::tuple { "04fa", "2490", "Hoitek HE2325U (UNI-T UT-D04)" },
                                         std::tuple { "10c4", "ea80", "SiLabs CP2110 (UNI-T UT-D09)" },
                                         std::tuple { "1a86", "e429", "WCH CH9329 (UNI-T UT-D09)" },
                                         std::tuple { "0820", "0001", "Brymen BU-86X" },
                                         std::tuple { "04d9", "a052", "Holtek (TFA AIRCO2NTROL)" } })
    rules += QString("# %3\nKERNEL==\"hidraw*\", ATTRS{idVendor}==\"%1\", ATTRS{idProduct}==\"%2\", TAG+=\"uaccess\"\n")
               .arg(vid, pid, what);
  return rules;
}

AccessProblem accessProblem(const QString &path, bool hid, quint16 vid, quint16 pid)
{
  AccessProblem a;
#ifdef Q_OS_LINUX
  const QFileInfo info(path);
  if (!info.exists() || (info.isReadable() && info.isWritable()))
    return a;
  a.problem = QCoreApplication::translate("Discovery", "No access: %1 belongs to %2:%3").arg(path, info.owner(), info.group());
  if (hid)
  {
    const QString rule = QString("KERNEL==\"hidraw*\", ATTRS{idVendor}==\"%1\", ATTRS{idProduct}==\"%2\", TAG+=\"uaccess\"")
                           .arg(vid, 4, 16, QLatin1Char('0')).arg(pid, 4, 16, QLatin1Char('0'));
    a.fix = QCoreApplication::translate("Discovery", "A udev rule lets you open the cable. The packages of QtDMM install one; for the AppImage or "
               "Flatpak, put this line into /etc/udev/rules.d/70-qtdmm.rules:\n\n%1\n\n"
               "then load it and plug the cable in again:\n\n"
               "sudo udevadm control --reload-rules && sudo udevadm trigger").arg(rule);
  }
  else
  {
    const QString group = info.group().isEmpty() ? QStringLiteral("dialout") : info.group();
    a.fix = QCoreApplication::translate("Discovery", "Serial ports belong to the group %1. Add yourself to it:\n\n"
               "sudo usermod -aG %1 $USER\n\n"
               "then log out and in again.").arg(group);
  }
#else
  Q_UNUSED(path) Q_UNUSED(hid) Q_UNUSED(vid) Q_UNUSED(pid)
#endif
  return a;
}

// ---------------------------------------------------------------- USB-HID

void UsbDiscoverer::start()
{
  // hidapi only enumerates (no device is opened): quick, but after start()
  // returned, so the caller has its connections in place
  QTimer::singleShot(0, this, [this]
  {
    struct hid_device_info *devs = hid_enumerate(0, 0);
    for (struct hid_device_info *d = devs; d; d = d->next)
    {
      const std::optional<Families::Family> family = Families::forUsb(d->vendor_id, d->product_id);
      if (!family)
        continue;
      const QString path = QString::fromLatin1(d->path);
      Candidate c;
      c.kind = Candidate::UsbCable;
      c.key = path;
      c.title = family->title;
      c.detail = family->cable.isEmpty() ? path : QCoreApplication::translate("Discovery", "%1 cable · %2").arg(family->cable, path);
      c.hint = family->hint;
      c.models = family->models;
      c.keys.insert("Port settings/device", QString("%1 0x%2:0x%3 %4").arg(family->portType)
                                              .arg(d->vendor_id, 4, 16, QLatin1Char('0'))
                                              .arg(d->product_id, 4, 16, QLatin1Char('0')).arg(path));
      const AccessProblem a = accessProblem(path, true, d->vendor_id, d->product_id);
      c.problem = a.problem;
      c.fix = a.fix;
      Q_EMIT found(c);
    }
    hid_free_enumeration(devs);
    Q_EMIT finished();
  });
}

// ---------------------------------------------------------------- serial

void SerialDiscoverer::start()
{
  QTimer::singleShot(0, this, [this]
  {
    for (const QSerialPortInfo &p : QSerialPortInfo::availablePorts())
    {
      // the kernel lists ttyS0..31 whether or not there is a UART: only USB
      // adapters (they say who made them) and named ports count here
      if (!p.hasVendorIdentifier() && p.description().isEmpty())
        continue;
#ifdef Q_OS_WIN
      const QString port = p.portName();
#else
      const QString port = p.systemLocation();
#endif
      Candidate c;
      c.kind = Candidate::Serial;
      c.key = port;
      const QString chip = p.hasVendorIdentifier() ? Families::adapter(p.vendorIdentifier(), p.productIdentifier())
                                                   : QString();
      c.title = chip.isEmpty() ? QCoreApplication::translate("Discovery", "Serial port") : QCoreApplication::translate("Discovery", "USB-serial adapter (%1)").arg(chip);
      c.detail = p.manufacturer().isEmpty() ? port : QString("%1 · %2").arg(p.manufacturer(), port);
      c.hint = QCoreApplication::translate("Discovery", "Which meter is on it, QtDMM cannot tell yet: choose the model.");
      c.keys.insert("Port settings/device", "SERIAL " + port);
      const AccessProblem a = accessProblem(port, false);
      c.problem = a.problem;
      c.fix = a.fix;
      Q_EMIT found(c);
    }
    Q_EMIT finished();
  });
}

// ---------------------------------------------------------------- network

void BridgeDiscoverer::start()
{
  m_browser = new MdnsBrowser(this);
  connect(m_browser, &MdnsBrowser::found, this, [this](const MdnsBrowser::Service &s)
  {
    const QString where = s.address.isNull() ? s.host : s.address.toString();
    QString host = s.host;
    if (host.endsWith(".local"))
      host.chop(6);
    const QString device = s.txt.value("name", s.txt.value("device"));
    Candidate c;
    c.kind = Candidate::Network;
    c.key = QString("%1:%2").arg(where).arg(s.port);
    c.title = QCoreApplication::translate("Discovery", "qtdmm-bridge on %1").arg(host);
    c.detail = device.isEmpty() ? c.key : QString("%1 · %2").arg(device, c.key);
    c.hint = QCoreApplication::translate("Discovery", "Which meter is on it, QtDMM cannot tell yet: choose the model.");
    c.keys.insert("Port settings/device", "RFC2217 " + c.key);
    Q_EMIT found(c);
  });
  connect(m_browser, &MdnsBrowser::finished, this, &Discoverer::finished);
  m_browser->browse("_qtdmm-bridge._tcp.local", 3000);
}

void BridgeDiscoverer::stop()
{
  if (m_browser && m_browser->isActive())
    m_browser->stop();
}

// ---------------------------------------------------------------- Bluetooth

struct BleDiscoverer::Private
{
#ifdef QTDMM_WITH_BLE
  QBluetoothDeviceDiscoveryAgent *agent = nullptr;
  QSet<QString> seen;
#endif
};

BleDiscoverer::BleDiscoverer(QObject *parent) :
  Discoverer(parent),
  d(new Private)
{
}

BleDiscoverer::~BleDiscoverer()
{
  delete d;
}

QString BleDiscoverer::unavailable()
{
#ifdef QTDMM_WITH_BLE
  if (QBluetoothLocalDevice::allDevices().isEmpty())
    return QCoreApplication::translate("Discovery", "No Bluetooth adapter found.");
  return QString();
#else
  return QCoreApplication::translate("Discovery", "This build of QtDMM has no Bluetooth.");
#endif
}

void BleDiscoverer::start()
{
#ifdef QTDMM_WITH_BLE
  // one scan for everything: two agents at once get in each other's way
  d->agent = new QBluetoothDeviceDiscoveryAgent(this);
  d->agent->setLowEnergyDiscoveryTimeout(7000);
  auto take = [this](const QBluetoothDeviceInfo &info)
  {
    const QString address = info.address().toString().toUpper();
    if (d->seen.contains(address))
      return;
    Candidate c;
    c.kind = Candidate::Bluetooth;
    c.key = address;
    if (info.manufacturerIds().contains(VictronBle::CompanyId))
    {
      c.title = info.name().isEmpty() ? QCoreApplication::translate("Discovery", "Victron device") : QCoreApplication::translate("Discovery", "Victron %1").arg(info.name());
      c.hint = QCoreApplication::translate("Discovery", "The device key is in the VictronConnect app: Product info, Instant readout.");
      c.models = Families::preferByName(Families::models(FrameFormat::VictronBLE, false), info.name());
    }
    else
    {
      // the GATT meters, by service or name
      for (int f = 0; f < FrameFormat::EndOfList; ++f)
      {
        const auto profile = BleGattDevice::profile(FrameFormat::DataFormat(f));
        if (!profile)
          continue;
        bool match = info.serviceUuids().contains(profile->service);
        for (const QString &prefix : profile->namePrefixes)
          match = match || info.name().startsWith(prefix, Qt::CaseInsensitive);
        if (match)
          c.models += Families::models(FrameFormat::DataFormat(f), false);
      }
      if (c.models.isEmpty())
        return;
      c.title = info.name().isEmpty() ? QCoreApplication::translate("Discovery", "Bluetooth meter") : info.name();
      c.hint = QCoreApplication::translate("Discovery", "Switch Bluetooth on at the meter.");
    }
    d->seen.insert(address);
    c.detail = info.rssi() != 0 ? QString("%1 · %2 dBm").arg(address).arg(info.rssi()) : address;
    c.keys.insert("Port settings/ble-address", QString("%1 %2").arg(address, info.name()).trimmed());
    Q_EMIT found(c);
  };
  connect(d->agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, take);
  connect(d->agent, &QBluetoothDeviceDiscoveryAgent::deviceUpdated, this,
          [take](const QBluetoothDeviceInfo &info, QBluetoothDeviceInfo::Fields) { take(info); });
  connect(d->agent, &QBluetoothDeviceDiscoveryAgent::finished, this, &Discoverer::finished);
  connect(d->agent, &QBluetoothDeviceDiscoveryAgent::canceled, this, &Discoverer::finished);
  connect(d->agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this, &Discoverer::finished);
  d->agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
#else
  QTimer::singleShot(0, this, &Discoverer::finished);
#endif
}

void BleDiscoverer::stop()
{
#ifdef QTDMM_WITH_BLE
  if (d->agent && d->agent->isActive())
    d->agent->stop();
#endif
}
