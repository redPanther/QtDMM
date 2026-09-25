//======================================================================
// File:		mainwid.h
// Author:	Matthias Toussaint
// Created:	Tue Apr 10 17:25:07 CEST 2001
//----------------------------------------------------------------------
// This file is part of QtDMM.
//
// QtDMM is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 3
// as published by the Free Software Foundation.
//
// QtDMM is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
//----------------------------------------------------------------------
// Copyright (c) 2001 Matthias Toussaint
//======================================================================

#pragma once


#include <QtGui>
#include <QtPrintSupport>

#include "ui_uimainwid.h"

#include "printdlg.h"

class MeterController;
class ConfigDlg;
class DisplayWid;
class TipDlg;
class Settings;
class InstancesDlg;
class MeterWid;
class ReadingLog;
struct Alarm;
class SharedStateManager;

/// The recorder graph (shown in its own MDI window), plus the settings and
/// the other dialogs.
///
/// The meter session itself - connection, min/max memory, alarms, SCPI
/// server, external program - is a MeterController, which MainWid creates.
/// The views (display, analog meter, readings table, graph) are connected to
/// its signals here and know nothing of each other. MainWin provides the
/// frame (menus, toolbars, docks, status bar) and hooks its actions up to
/// the *SLOT members here.
class MainWid : public QFrame, private Ui::UIMainWid
{
  Q_OBJECT
public:
  /// @param instance_id  name of this instance for multi-instance setups (--config-id)
  /// @param config_path  directory of the settings file (--config-dir), or empty
  /// @param parent       the MainWin
  MainWid(QString instance_id, QString config_path, QWidget *parent = Q_NULLPTR);
  /// Disconnects, saves the settings and asks about unsaved data. Returns
  /// false when the user cancels; MainWin then ignores the close event.
  bool        closeWin();
  /// Window geometry stored in the settings.
  QRect       winRect() const;
  bool        saveWindowPosition() const;
  bool        saveWindowSize() const;
  /// The LCD panel to feed; created and docked by MainWin.
  void        setDisplay(DisplayWid *);
  /// The analog meter to feed; created and docked by MainWin.
  void        setMeter(MeterWid *);
  /// Table model that gets every reading (MainWin owns it).
  void        setReadingLog(ReadingLog *);
  /// The instance coordinator; readings are published through it.
  void        setStateManager(SharedStateManager *);
  /// --debug: pass on to DMM::setConsoleLogging().
  void        setConsoleLogging(bool);
  /// Stores the toolbar visibility (display, dmm, graph, file) in the settings.
  void        setToolbarVisibility(bool, bool, bool, bool);
  /// The recorder graph (for the zoom/pan shortcuts in MainWin).
  DMMGraph   *graph() const { return ui_graph; }
  /// False until a meter has been chosen in the settings once; a fresh
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

Q_SIGNALS:
  /// Recording started/stopped (graph state).
  void        running(bool);
  /// Message for the status bar's info field.
  void        info(const QString &);
  /// Message for the status bar's connection field (from DMM::error()).
  void        error(const QString &);
  /// The "icons with text" preference changed.
  void        useTextLabel(bool);
  /// Asks MainWin to connect/disconnect (drives the Connect action).
  void        setConnect(bool);
  /// Toolbar visibility read from the settings, for MainWin to apply.
  void        toolbarVisibility(bool, bool, bool, bool);
  /// The connection state changed; MainWin checks the Connect action.
  void        connectDMM(bool);
  /// The settings were applied; the meter shown in the title may have changed.
  void        configChanged();
  /// A state string for the other instances (SharedStateManager).
  void        sendState(const QString&);
  /// The SCPI server's state for the status bar ("SCPI 5025", empty = off).
  void        scpiStatus(const QString&);

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
  /// Opens the settings dialog on its first page.
  void        configSLOT();
  /// Opens the settings dialog on the multimeter page.
  void        configDmmSLOT();
  /// Opens the settings dialog on the recording page.
  void        configRecorderSLOT();
  void        printSLOT();
  void        exportSLOT();
  void        importSLOT();
  /// Graph started/stopped recording.
  void        runningSLOT(bool);
  /// Settings dialog OK/Apply: re-reads the configuration (readConfig()).
  void        applySLOT();
  /// Settings dialog Cancel.
  void        rejectSLOT();
  void        showTipsSLOT();
  /// Shows the instances dialog.
  void        instancesSLOT();
  /// The set of running instances changed (from SharedStateManager).
  void        instancesChangedSlot(QStringList&);

protected:
  MeterController *m_ctl;
  ConfigDlg  *m_configDlg;
  qtdmm::PrintDlg *m_printDlg;
  QPrinter    m_printer;
  DisplayWid *m_display;
  MeterWid   *m_meter;
  /// The desktop part of an alarm: beep, raise the window, popup.
  void        alarmRaised(const Alarm &alarm, const QString &shown, const QString &text);
  TipDlg     *m_tipDlg;
  InstancesDlg *m_instancesDlg;
  Settings    *m_settings;
  QString     m_instanceId;

  /// Applies the settings to DMM, graph, display and meter.
  void        readConfig();
  QRect       parentRect() const;

protected Q_SLOTS:
  /// Launches the configured external application (threshold trigger).
  void        startExternalSLOT();
  /// The external application exited.
  void        exitedSLOT(int exitStatus);
  /// Graph zoom changed; re-applies the window/total size.
  void        zoomedSLOT();
};

