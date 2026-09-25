// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <functional>

#include "alarm.h"
#include "reading.h"

class DMM;
class QProcess;
class ScpiServer;
class MdnsResponder;
class SharedStateManager;

/// What the SCPI server should do (from the settings page).
struct ScpiConfig
{
  bool    enabled = false;
  bool    allInterfaces = false;   ///< else localhost only
  quint16 port = 5025;
  bool    mdns = false;            ///< announce as _scpi-raw._tcp
};

/// The meter session without any user interface: the connection (DMM), the
/// min/max memory, the alarms, the SCPI server with its mDNS announcement,
/// the external-program trigger and the publishing of readings to the other
/// instances.
///
/// Views (LCD, analog meter, graph, readings table) and the window hang off
/// its signals; none of them talks to another. Whatever needs a question to
/// the user (a message box, a beep, raising the window) is left to the UI:
/// the controller reports it by a signal.
class MeterController : public QObject
{
  Q_OBJECT
public:
  explicit MeterController(QObject *parent = nullptr);
  ~MeterController() override;

  /// The connection; the settings page configures its port and protocol.
  DMM        *dmm() const { return m_dmm; }
  /// Readings are published to the other instances through @p mgr.
  void        setStateManager(SharedStateManager *mgr);
  /// This instance's id (--config-id), part of the mDNS name.
  void        setInstanceId(const QString &id) { m_instanceId = id; }
  /// The configured meter's name, for SCPI *IDN? and mDNS.
  void        setModel(const QString &model);
  void        setAlarms(const QList<Alarm> &alarms);
  const AlarmManager *alarms() const { return m_alarms; }

  /// Connects (true) or disconnects the meter. False when the port could
  /// not be opened.
  bool        connectMeter(bool on);
  bool        isConnected() const;
  /// The recorder started or stopped (SCPI reports it).
  void        setRecording(bool on);

  /// Starts, restarts or stops the SCPI server; keeps a running server when
  /// nothing about it changed, so its clients stay connected.
  void        applyScpi(const ScpiConfig &config);
  /// Source of the SCPI screen dump (HCOPy:SDUMp:DATA?): format -> image bytes.
  void        setScreenshotSource(std::function<QByteArray(const QByteArray &)> source);

  /// Runs @p command (the threshold trigger); false when it could not be
  /// started. The UI asks first when externalRunning().
  bool        startExternal(const QString &command);
  bool        externalRunning() const;
  /// Ends the running external program (waits up to a second for it).
  void        killExternal();

  /// Min/max memory in SI base units; +-1e20 when empty.
  double      minimum() const { return m_min; }
  double      maximum() const { return m_max; }
  /// The text of the current reading's unit ("mV"), and its base unit ("V").
  QString     unit() const { return m_lastUnit; }
  QString     baseUnit() const { return m_baseUnit; }
  bool        overload() const { return m_overload; }

public Q_SLOTS:
  /// Clears the min/max memory.
  void        resetMinMax();
  /// The user acknowledged the alarm banner.
  void        acknowledgeAlarms();

Q_SIGNALS:
  /// A reading, after min/max; overload and prefix already worked out.
  void        reading(const Reading &reading);
  /// A new minimum (@p value in SI base units, @p text and @p unit as the
  /// meter showed them).
  void        minimumChanged(double value, const QString &text, const QString &unit);
  /// A new maximum, like minimumChanged().
  void        maximumChanged(double value, const QString &text, const QString &unit);
  /// The min/max memory was cleared (reset or a new unit).
  void        minMaxReset();
  /// The main value's unit changed ("mV" -> "V").
  void        unitChanged(const QString &unit);
  /// Ten times a second: the current main value, for the recorder.
  void        sample(double value);
  /// For the status bar's connection field (DMM messages, alarms, SCPI errors).
  void        error(const QString &);
  /// For the status bar's info field.
  void        info(const QString &);

  /// An alarm went off. @p shown is the value as text ("12.3V" or "OL"),
  /// @p text its message. The controller already ran its program; beep,
  /// popup and raising the window are up to the UI.
  void        alarmRaised(const Alarm &alarm, const QString &shown, const QString &text);
  /// The alarm banner: one line per raised, unacknowledged alarm, in the
  /// colour of the first; empty when nothing is raised.
  void        alarmBannerChanged(const QString &lines, const QColor &color);
  /// An alarm wants the recorder started (true) or stopped.
  void        recordingRequested(bool start);
  /// An alarm wants a mark in the graph and/or the readings table.
  void        markRequested(const QColor &color, const QString &name, bool graph, bool table);

  /// A SCPI client asked to connect/disconnect the meter.
  void        connectRequested(bool on);
  /// SCPI server state: a short text for the status bar (empty = off) and
  /// a sentence for the settings page.
  void        scpiStatusChanged(const QString &status, const QString &detail);
  /// The external program exited.
  void        externalFinished(int exitStatus);

private:
  void        valueSLOT(double dval, const QString &val, const QString &unit, const QString &special,
                        const QString &range, bool hold, bool showBar, int id);
  void        onAlarmRaised(int index, const Alarm &alarm, double value);
  void        updateBanner();
  void        updateScpiStatus();
  void        timerEvent(QTimerEvent *) override;

  DMM                *m_dmm;
  AlarmManager       *m_alarms;
  ScpiServer         *m_scpi;
  MdnsResponder      *m_mdns;
  QProcess           *m_external;
  SharedStateManager *m_stateMgr = nullptr;
  QString             m_instanceId = "default";
  QString             m_model;

  double      m_min = 1.0E20;
  double      m_max = -1.0E20;
  QString     m_lastUnit;
  QString     m_baseUnit;
  bool        m_overload = false;
  double      m_dval = 0.0;
};
