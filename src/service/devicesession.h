// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include "device/transports/calc.h"

class DeviceLibrary;
class MeterController;
class SharedStateManager;
class ServiceHost;

/// The meters of one process: the window's (adopt()) and those opened from
/// "My devices" without a window (open(): --also, SCPI QTDMM:DEVice:OPEN).
/// They share the network services (host()); INSTrument chooses among them.
///
/// A meter opened here has no alarms; its readings carry its source
/// ("device:<id>", Reading::source) and reach SCPI. Its recording follows
/// the window's: it starts at the same moment, on one time base
/// (RecordingStore::startAt()), with the window's sample time, and stops
/// with it, whatever started or stopped the window's (Record, a trigger,
/// the length, SCPI, another instance). INITiate and ABORt on it start and
/// stop the window's; with no window they start and stop those here. A
/// recording of it not saved (QTDMM:RECord:SAVE) is overwritten by the next.
/// Each meter is open once: when the window takes one that is open here,
/// release() closes it first, so that the port is free.
class DeviceSession : public QObject
{
  Q_OBJECT
public:
  explicit DeviceSession(QObject *parent = nullptr);

  ServiceHost *host() const { return m_host; }
  /// "My devices" in @p dir (Settings::configDir()), for open(id).
  void        setConfigDir(const QString &dir);
  /// This instance's id (--config-id), for the meters' mDNS and SCPI names.
  void        setInstanceId(const QString &id) { m_instanceId = id; }
  /// The other instances' readings, which a calculated value reads after
  /// the meters of the process (calcInputs()). The meters opened here
  /// publish nothing there.
  void        setStateManager(SharedStateManager *mgr) { m_stateMgr = mgr; }
  /// sigrok-cli for the meters read through it ("Port settings/sigrok_exe").
  void        setSigrokExe(const QString &exe) { m_sigrokExe = exe; }

  /// The window's meter: counted as open, never closed here.
  void        adopt(MeterController *ctl);
  /// Opens and connects the entry @p id of "My devices". Null and the
  /// reason in @p error (a SCPI error, "<code>,<text>", in English) when it
  /// is unknown, already open or does not connect.
  MeterController *open(const QString &id, QString *error = nullptr);
  /// The same with the entry's keys and name given.
  MeterController *open(const QString &id, const QVariantMap &keys, const QString &name, QString *error = nullptr);
  /// Closes a meter open() opened; false (and why) for the window's or one not open.
  bool        close(const QString &id, QString *error = nullptr);
  /// The window is about to use the entry @p id: a meter open here with it
  /// is closed (the port is the window's), with a message.
  void        release(const QString &id);

  /// The window's meter first, then those of open() in their order.
  QList<MeterController *> controllers() const;
  /// The meter whose readings come from @p source (MeterController::source()).
  MeterController *controller(const QString &source) const;
  /// What a calculated value of this process reads (CalcDevice): the main
  /// value of each meter here by its name in My devices, as a variable
  /// ("UT61E Bench": ut61e_bench), then the other instances by id; a
  /// meter here wins over an instance of the same name.
  CalcDevice::Inputs calcInputs() const;
  /// The variable of a meter named @p name in a formula: lower case,
  /// other characters than letters, digits and '_' as '_', a '_' before a
  /// leading digit.
  static QString variableName(const QString &name);

  /// The entries of "My devices", for QTDMM:DEVice:CATalog?.
  DeviceLibrary *library() const { return m_library; }

Q_SIGNALS:
  void        opened(MeterController *ctl);
  /// After the controller is gone.
  void        closed(const QString &source);
  /// For the status bar: a meter could not be opened, or was closed for the window.
  void        error(const QString &text);

private:
  void        installScpi();
  /// The window's recording started (Record) or ended (another state).
  void        followWindow(int state);
  /// Starts the recording of @p ctl at @p t0 on the core clock, wall clock
  /// @p start, the grid @p sampleTime (tenths, 0: its own).
  void        startFollower(MeterController *ctl, qint64 t0, const QDateTime &start, int sampleTime);
  /// INITiate or ABORt on a meter here.
  void        onRecordingRequested(bool start);

  ServiceHost *m_host;
  DeviceLibrary *m_library = nullptr;
  QPointer<MeterController> m_adopted;
  SharedStateManager *m_stateMgr = nullptr;
  QList<MeterController *> m_opened;
  QString     m_instanceId = "default";
  QString     m_sigrokExe = "sigrok-cli";
};
