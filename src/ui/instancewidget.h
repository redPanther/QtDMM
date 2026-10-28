//======================================================================
// File:		mainwid.h
// Author:	Matthias Toussaint
// Created:	Tue Apr 10 17:25:07 CEST 2001
//----------------------------------------------------------------------
// This file is part of QtDMM.
//
// QtDMM is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// QtDMM is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with QtDMM.  If not, see <http://www.gnu.org/licenses/>.
//----------------------------------------------------------------------
// Copyright (c) 2001 Matthias Toussaint
//======================================================================

#pragma once

#include <optional>


#include <QtGui>
#include <QtPrintSupport>

#include "ui_uiinstancewidget.h"

#include "core/limittest.h"
#include "core/reading.h"
#include "core/valueref.h"
#include "ui/dialogs/printdlg.h"

class MeterController;
class DeviceSession;
class ValueFeed;
class SettingsDialog;
class LcdWidget;
class Settings;
class AnalogMeter;
class PoincarePlot;
class ReadingsModel;
struct Alarm;
class DeviceLibrary;

/// The recorder graph (shown in its own MDI window), plus the settings and
/// the other dialogs.
///
/// The meter session itself - connection, min/max memory, alarms, SCPI
/// server, the alarms' programs - is a MeterController, which InstanceWidget creates.
/// The views (display, analog meter, readings table, graph) are connected to
/// its signals here and know nothing of each other. MainWindow provides the
/// frame (menus, toolbars, docks, status bar) and hooks its actions up to
/// the *SLOT members here.
class InstanceWidget : public QFrame, private Ui::UIInstanceWidget
{
  Q_OBJECT
public:
  /// @param instanceId  name of this instance for multi-instance setups (--config-id)
  /// @param configPath  directory of the settings file (--config-dir), or empty
  /// @param session     the meters of the process: the window's meter takes part
  ///                    in its network services and is one of them
  /// @param parent      the MainWindow
  InstanceWidget(QString instanceId, QString configPath, DeviceSession *session, QWidget *parent = nullptr);
  /// Disconnects, saves the settings and asks about unsaved data. Returns
  /// false when the user cancels; MainWindow then ignores the close event.
  bool        closeWin();
  /// The settings dialog: its pages hold what MainWindow reads (the window
  /// at exit, the analog meter's style, the shown display, the keys).
  SettingsDialog *settingsDialog() const { return m_configDlg; }
  /// The LCD panel to feed; created and docked by MainWindow.
  void        setDisplay(LcdWidget *);
  /// The LCD's colours (LcdWidget::LcdVariant), stored as Display/lcd.
  void        setLcdVariant(int variant);
  /// The analog meter's style from its context menu (0 dark, 1 ivory):
  /// shown at once, saved, and the Appearance page follows.
  void        setMeterStyle(int style);
  /// The limit test (PASS/FAIL), kept in the settings (Limits/test).
  void        setLimitTest(const LimitTest &test);
  const LimitTest &limitTest() const;
  /// The analog meter to feed; created and docked by MainWindow.
  void        setMeter(AnalogMeter *);
  /// The Poincaré plot (MainWindow owns and feeds it), for its value.
  void        setPoincare(PoincarePlot *plot);
  /// Table model that gets every reading (MainWindow owns it).
  void        setReadingLog(ReadingsModel *);
  /// @name Which value of the meter a view shows
  /// Reading::id, 0 the main value; the sidebar's value menu (ValueMenu),
  /// kept with the window layout. A value the meter does not have
  /// (numValues()) gives the main value. The LCD's primary value is also the
  /// one of the min/max memory; the recording stays the main value's.
  /// A ValueRef of another meter shows that meter's value (ValueFeed),
  /// "no value" while it is not open.
  /// @{
  void        setLcdValue(const ValueRef &value);
  /// The LCD's small line (LcdWidget::setSecondaries()); std::nullopt: the
  /// first values after the big one.
  void        setLcdSecondaries(const std::optional<QList<int>> &ids);
  /// The values on the LCD's small line now.
  QList<int>  lcdSecondaries() const;
  /// Whether the small line was chosen (else it is not saved).
  bool        lcdSecondariesChosen() const { return m_lcdSecondaries.has_value(); }
  void        setMeterValue(const ValueRef &value);
  void        setPoincareValue(const ValueRef &value);
  /// Whether the readings table shows value @p id (all at first).
  bool        tableValueShown(int id) const;
  void        setTableValueShown(int id, bool shown);
  ValueRef    lcdValue() const { return m_lcdValue; }
  ValueRef    meterValue() const { return m_meterValue; }
  ValueRef    poincareValue() const { return m_poincareValue; }
  /// The id of @p value among this meter's values; another meter's value
  /// and one the meter does not have (numValues()) give the main value.
  int         ownValue(const ValueRef &value) const;
  /// @p value belongs to this window's meter (no source, or its own).
  bool        ownMeter(const ValueRef &value) const;
  /// The limit test's band to the graph and the analog meter (they draw it
  /// for the value they show).
  void        applyLimitBands();
  /// The values the meter has: DMMInfo::valueCount(), or more when more came;
  /// at most kMaxValues.
  int         numValues() const;
  /// The meter's own name of the value ("Battery voltage"), else "CO2 (ppm)"
  /// from its last reading, "Value 2" before one came.
  QString     valueName(int id) const;
  /// Where room is short: the meter's own name, else unit and coupling
  /// ("V DC", "°C"); valueName() before a reading came.
  QString     valueShortName(int id) const;
  /// Its last reading as shown: "-14.9 A", "OL"; empty before one came.
  QString     valueText(int id) const { return m_lastText.value(id); }
  /// @}
  /// --debug: pass on to MeterConnection::setConsoleLogging().
  void        setConsoleLogging(bool);
  /// The recorder graph (for the zoom/pan shortcuts in MainWindow).
  GraphWidget   *graph() const { return ui_graph; }
  /// False until a device has been chosen once (sidebar, Add device); a fresh
  /// instance does not try to connect to a guessed port on its own.
  bool        dmmConfigured() const;
  /// What the window title shows: the model, else the port, else a hint.
  QString     dmmTitle() const;
  /// The configured port for display ("/dev/ttyUSB0", "BLE AA:BB:...";
  /// never a Bluetooth key).
  QString     portName() const;
  /// The meter session (connection, alarms, SCPI); views connect to it.
  MeterController *controller() const { return m_ctl; }
  Settings   *settings() const { return m_settings; }
  /// "My devices", shared by all instances.
  DeviceLibrary *devices() const { return m_devices; }
  /// The entry of "My devices" this instance uses now (its key
  /// DMM/my-device); empty when none.
  QString     currentDevice() const;
  /// Switches to the device @p id of "My devices": disconnects, takes over
  /// its meter keys and connects again. A running recording stops with a
  /// message - another meter is another function; unsaved readings are
  /// offered for export first unless @p ask is false (confirmSwitch()).
  /// False when there is no such entry or the user cancels.
  bool        switchDevice(const QString &id, bool ask = true);
  /// Before a switch to the device @p name: unsaved readings are exported
  /// or dropped, or the user cancels (false).
  bool        confirmSwitch(const QString &name);
  /// Before a new recording: unsaved readings viewed are exported or
  /// dropped, or the user cancels (false). True at once in Live.
  bool        confirmRecording();
  /// Before a recording started by hand: asks for its length, the last
  /// one ready (RecordLengthDlg); false when the user cancels.
  bool        askRecordingLength();
  /// --config-dir as given (empty: the default), for the instances started from here.
  QString     configPath() const { return m_configPath; }

Q_SIGNALS:
  /// Recording started/stopped (graph state).
  void        running(bool);
  /// The recorder is in View, Live or Recording now (RecordingStore::State).
  void        recorderState(int state);
  /// Message for the status bar's info field.
  void        info(const QString &);
  /// Message for the status bar's connection field (from MeterConnection::error()).
  void        error(const QString &);
  /// The "icons with text" preference changed.
  void        useTextLabel(bool);
  /// The symbols chosen: "colored", "plain" or "system" (Designs::IconSet).
  void        iconSet(const QString &set);
  /// The configured meter has keys QtDMM can press (ControlBar).
  void        remoteControl(bool supported);
  /// Asks MainWindow to connect/disconnect (drives the Connect action).
  void        setConnect(bool);
  /// Whether the display is shown, read from the settings, for MainWindow.
  void        showDisplay(bool show);
  /// The connection state changed; MainWindow checks the Connect action.
  void        connectDMM(bool);
  /// The settings were applied; the meter shown in the title may have changed.
  void        configChanged();
  /// The SCPI server or HiSLIP started, stopped or got a client
  /// (controller()->services()).
  void        servicesChanged();
  /// Another device was taken over (switchDevice()): what the views hold of
  /// the one before is not this one's.
  void        meterChanged();

public Q_SLOTS:
  /// Clears min/max memory and the meter's peak/auto-bipolar latch.
  void        resetSLOT();
  /// Connect (true) or disconnect (false) the meter.
  void        connectSLOT(bool);
  void        quitSLOT();
  void        helpSLOT();
  /// Clears the graph.
  void        clearSLOT();
  /// Starts recording (also triggered remotely via the shared state).
  void        startSLOT();
  void        stopSLOT();
  /// Back to Live from View: unsaved readings are offered for export first.
  void        liveSLOT();
  /// Opens the settings dialog on General (already open: where it is).
  void        configSLOT();
  /// Opens the settings dialog on the recording page.
  void        configRecorderSLOT();
  /// Opens the settings dialog on the SCPI server page.
  void        configScpiSLOT();
  void        printSLOT();
  void        exportSLOT();
  void        importSLOT();
  /// Graph started/stopped recording.
  void        runningSLOT(bool);
  /// Settings dialog OK/Apply: re-reads the configuration (readConfig());
  /// the connection stays as it is.
  void        applySLOT();

protected:
  /// Shows the settings dialog, on @p page (a SettingsDialog::PageType) unless -1.
  void        openConfig(int page = -1);
  void        applyMeterStyle();   ///< style, ballistics, red zone from the settings
  MeterController *m_ctl;
  DeviceSession   *m_session;
  ValueFeed       *m_lcdFeed = nullptr;       ///< the LCD's meter: the window's or one open beside it
  ValueFeed       *m_meterFeed = nullptr;     ///< the analog meter's
  ValueFeed       *m_poincareFeed = nullptr;  ///< the Poincaré plot's
  SettingsDialog  *m_configDlg;
  qtdmm::PrintDlg *m_printDlg;
  QPrinter    m_printer;
  LcdWidget *m_display;
  QColor      m_lcdTint;   ///< the Appearance page's tint last applied
  QColor      m_alertColor;   ///< the raised alarm's colour for display and meter, invalid when none
  bool        m_alertBlink = false;   ///< Alarms/blink
  bool        m_alertPhase = true;    ///< blinking: the colour shown now
  QColor      m_limitColor;   ///< the limit test's green or red, invalid when none; under an alarm's
  LimitTest::Verdict m_lastVerdict = LimitTest::NoVerdict;   ///< for the sounds: only a change sounds
  QTimer     *m_alertTimer = nullptr;
  class AlarmSound *m_alarmSound = nullptr;
  class AlarmSound *m_limitSound = nullptr;   ///< the limit test's sounds, by verdict
  /// Display and meter in the alarm's colour (or their own in the dark
  /// phase of the blinking).
  void        showAlertColor();
  AnalogMeter   *m_meter;
  PoincarePlot  *m_poincare = nullptr;
  QPointer<ReadingsModel> m_readingLog;
  ValueRef    m_lcdValue;
  ValueRef    m_meterValue;
  ValueRef    m_poincareValue;
  std::optional<QList<int>> m_lcdSecondaries;   ///< setLcdSecondaries()
  QMap<int, Reading> m_lastReading;   ///< by id, for valueName()
  QMap<int, QString> m_lastText;      ///< by id, for valueText()
  int         m_seenValues = 0;         ///< highest value id read + 1, since the meter was chosen
  /// The number of values (numValues()) to the display and SCPI, then
  /// applyValueChoice().
  void        applyValueCount();
  /// Another meter: the units, the values seen and SCPI's channels of the
  /// one before go; they come with its first readings.
  void        forgetValues();
  /// The values chosen, as far as the meter has them, to the views and the
  /// min/max memory.
  void        applyValueChoice();
  /// The meter's values by name: the alarm page's choice of value, the
  /// readings table's statistics line.
  void        updateValueNames();
  /// The desktop part of an alarm: sound, raise the window, popup.
  void        alarmRaised(int index, const Alarm &alarm, const QString &shown, const QString &text);
  Settings    *m_settings;
  /// Disconnects, takes over @p keys of the entry @p id, clears the graph,
  /// connects; @p name for the messages.
  void        takeOver(const QVariantMap &keys, const QString &name, const QString &id);
  /// After the settings were applied: the entry in use follows them (same
  /// model: the same meter, its entry changes along); another model is
  /// another meter - the entry at its place, or none.
  void        syncDevice();
  /// Opens or closes the port without touching the recorder (connectSLOT()
  /// stops it on closing; an alarm's program does not).
  void        connectPort(bool on);
  /// Asks about unsaved readings before they are lost (@p text says how,
  /// @p discard is the button that drops them): export first, drop or
  /// cancel. True when the caller may go on - nothing unsaved, exported or
  /// dropped; false for Cancel or a failed export.
  bool        keepUnsavedData(const QString &text, const QString &discard);
  DeviceLibrary *m_devices;
  QString     m_instanceId;
  QString     m_configPath;   ///< --config-dir, passed on to new instances

  /// Applies the settings to the MeterConnection, graph, display and meter.
  void        readConfig();
  /// The meter's keys (and sigrok-cli) the port was set up with last:
  /// readConfig() opens the port again only when they changed.
  QVariantMap m_appliedMeter;
  QRect       parentRect() const;

protected Q_SLOTS:
  /// Graph zoom changed; re-applies the window/total size.
  void        zoomedSLOT();
};

