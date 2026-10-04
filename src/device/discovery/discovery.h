// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include "device/frameformat.h"

class MdnsBrowser;

/// One thing a search found: a meter, a cable of a meter family, a serial
/// port, a bridge - with what is known about which meters it can be.
struct Candidate
{
  enum Kind
  {
    UsbCable,    ///< a USB-HID cable or sensor of a known family (VID:PID)
    Serial,      ///< a serial port, a USB-serial adapter
    Bluetooth,   ///< a Bluetooth LE meter or Victron device
    Network      ///< a qtdmm-bridge port in the network
  };

  Kind        kind = Serial;
  QString     key;       ///< identifies it across a search (path, address)
  QString     title;     ///< "UNI-T UT61B+/D+/E+ or UT161"
  QString     detail;    ///< "UT-D09 cable · /dev/hidraw3"
  QString     hint;      ///< what to do at the meter ("press RS232")
  /// The meter keys that say where it is: Port settings/device, or
  /// Port settings/ble-address for Bluetooth.
  QVariantMap keys;
  /// The models (DMMInfo::name) it can be, the likeliest first; empty: any
  /// model with a serial protocol.
  QStringList models;
  /// Why it cannot be used yet ("/dev/hidraw3 belongs to root"); empty: fine.
  QString     problem;
  /// How to fix the problem, with the commands.
  QString     fix;
};
Q_DECLARE_METATYPE(Candidate)

/// One way of searching. start() reports each find as found() as soon as it
/// is known and ends with finished(); stop() ends it early.
class Discoverer : public QObject
{
  Q_OBJECT
public:
  using QObject::QObject;
  virtual void start() = 0;
  virtual void stop() {}

Q_SIGNALS:
  void        found(const Candidate &candidate);
  void        finished();
};

/// The meter families a cable or sensor stands for, and the hints.
namespace Families
{
/// A USB-HID cable or sensor by VID:PID; nullopt for an unknown one.
struct Family
{
  QString     title;
  QString     cable;       ///< "UT-D04", empty for a sensor
  QString     hint;
  QString     portType;    ///< "HID" or "HIDHOLTEK"
  QStringList models;
};
std::optional<Family> forUsb(quint16 vid, quint16 pid);
/// The chip of a USB-serial adapter ("FTDI", "CH340", "PL2303", "CP210x"),
/// empty when unknown.
QString     adapter(quint16 vid, quint16 pid);
/// The models (DMMInfo::name) with @p format, the serial ones (baud > 0)
/// or the others; all serial models for FrameFormat::Invalid.
QStringList models(FrameFormat::DataFormat format, bool serial);
/// The models of @p vendor with a serial protocol.
QStringList vendorModels(const QString &vendor);
/// @p models with the one that fits the device's own name first: a
/// Victron "SmartShunt HQ2203" is the "Victron SmartShunt".
QStringList preferByName(QStringList models, const QString &deviceName);
}

/// Why QtDMM cannot open @p path, and how to fix it; empty when it can.
/// @p vid and @p pid go into the udev rule of a HID device.
struct AccessProblem
{
  QString problem;
  QString fix;
};
AccessProblem accessProblem(const QString &path, bool hid, quint16 vid = 0, quint16 pid = 0);
/// The udev rule that lets the logged-in user open the HID cables and
/// sensors QtDMM knows (what the packages install).
QString udevRules();

/// USB-HID cables and sensors of the known families (hidapi).
class UsbDiscoverer : public Discoverer
{
  Q_OBJECT
public:
  using Discoverer::Discoverer;
  void        start() override;
};

/// Serial ports, with the chip of a USB-serial adapter.
class SerialDiscoverer : public Discoverer
{
  Q_OBJECT
public:
  using Discoverer::Discoverer;
  void        start() override;
};

/// qtdmm-bridge ports in the network (mDNS).
class BridgeDiscoverer : public Discoverer
{
  Q_OBJECT
public:
  using Discoverer::Discoverer;
  void        start() override;
  void        stop() override;

private:
  MdnsBrowser *m_browser = nullptr;
};

/// Bluetooth LE: one scan for the GATT meters and the Victron devices.
/// Without Bluetooth support it finds nothing; available() says why.
class BleDiscoverer : public Discoverer
{
  Q_OBJECT
public:
  explicit BleDiscoverer(QObject *parent = nullptr);
  ~BleDiscoverer() override;
  void        start() override;
  void        stop() override;
  /// Empty when a scan is possible, else why not ("no Bluetooth adapter").
  static QString unavailable();

private:
  struct Private;
  Private    *d;
};
