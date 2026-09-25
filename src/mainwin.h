//======================================================================
// File:		mainwin.h
// Author:	Matthias Toussaint
// Created:	Sun Sep  2 12:14:07 CEST 2001
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
#include <QtWidgets>
#include <QMenu>
#include <QCommandLineParser>

#include "ui_uimainwin.h"
#include "sharedstatemanager.h"

class MainWid;
class DisplayWid;
class HelpDlg;
class MeterWid;
class ReadingLogWid;
class AlarmBar;
class MdiArranger;
class QMdiArea;
class QMdiSubWindow;

/// The application window: toolbars, status bar, the alarm banner and an
/// MDI area with four windows - LCD display, analog meter, graph (MainWid)
/// and readings table - placed by an MdiArranger.
///
/// Also the place where several QtDMM instances talk to each other: the
/// SharedStateManager's state changes ("RECORD", "STOP", "RAISE_<id>") are
/// turned into actions here, and a second instance with the same id is
/// refused.
class MainWin : public QMainWindow, private Ui::UIMainWin
{
  Q_OBJECT
public:
  /// @param parser the processed command line (--debug, --config-dir, --config-id)
  /// @param parent parent widget, normally none
  MainWin(QCommandLineParser &parser, QWidget *parent = Q_NULLPTR);
  /// --debug: frame dump and the qtdmm.hid logging category.
  void      setConsoleLogging(bool);

protected Q_SLOTS:
  /// Recording state changed; enables/disables Start/Stop.
  void      runningSLOT(bool);
  /// The Connect action was toggled.
  void      connectSLOT(bool);
  /// Start action: records locally and tells the other instances.
  void      startSLOT();
  void      stopSLOT();
  /// Writes a state string for the other instances.
  void      sendStateSLOT(const QString &);
  void      on_action_About_triggered();
  void      on_action_Help_triggered();
  /// Shows the popup menu (the window has no menu bar).
  void      on_action_Menu_triggered();
  /// Checks the Connect action without triggering it.
  void      setConnectSLOT(bool);
  /// Applies toolbar visibility from the settings.
  void      toolbarVisibilitySLOT(bool, bool, bool, bool);
  /// A toolbar was shown/hidden by the user; stores the new state.
  void      setToolbarVisibilitySLOT();
  /// Toolbar button style: icons only or icons with text.
  void      setUseTextLabel(bool on);
  /// Title = app name, instance id and the configured meter.
  void      updateWindowTitle();
  /// Space: starts the recorder, or stops it when it is running.
  void      toggleRecordingSLOT();
  /// F11
  void      setFullScreen(bool on);
  /// Arrange actions and the title-bar action follow the arranger.
  void      syncArrangeActions();
  /// The readings dot in the status bar and its tooltip (meter and port).
  void      updateLed();

protected:
  MainWid    *m_wid;
  DisplayWid *m_display;
  MeterWid   *m_meter;
  ReadingLogWid *m_readings;
  AlarmBar   *m_alarmBar;
  QMdiArea   *m_mdi;
  MdiArranger *m_arranger;
  QLabel     *m_led;        ///< readings dot in the status bar
  QTimer     *m_ledIdle;
  bool        m_ledActive = false;
  bool        m_ledBlink = false;
  QMdiSubWindow *m_displayWin;
  QMdiSubWindow *m_meterWin;
  QMdiSubWindow *m_graphWin;
  QMdiSubWindow *m_readingsWin;
  QAction    *m_displayAction = nullptr;
  QAction    *m_meterAction = nullptr;
  QAction    *m_readingsAction = nullptr;
  QAction    *m_arrangeTop;
  QAction    *m_arrangeFree;
  QAction    *m_titleBars;
  QMenu      *m_arrangeMenu;
  QMenu      *m_designMenu;
  bool        m_restoring = false;   ///< restoreWindows() is setting the actions
  QAction    *m_fullScreen;
  QAction    *m_zoomIn;
  QAction    *m_zoomOut;
  QAction    *m_zoomFit;
  QAction    *m_copyImage;
  bool        m_running;
  QLabel     *m_error;
  QLabel     *m_info;
  QLabel     *m_scpi;      ///< SCPI server state, hidden while it is off
  QMenu      *m_menu;
  HelpDlg    *m_helpDlg;
  SharedStateManager* m_stateMgr;
  QString     m_config_id;
  bool        m_localRecord;

  void        setupIcons();
  void        createActions();
  /// Menu-only actions and their shortcuts (the window has no menu bar).
  void        createExtraActions();
  /// Appends the shortcut to every action's tooltip: "Start (Ctrl+S)".
  void        addShortcutsToToolTips();
  /// Adds @p view as an MDI window with @p title (@p role: MdiArranger::Role;
  /// @p name identifies it in the settings).
  QMdiSubWindow *addView(QWidget *view, const QString &title, int role, const QString &name);
  /// A checkable action that shows/hides @p win.
  QAction    *windowAction(QMdiSubWindow *win, const QString &text, const char *shortcut,
                           const char *icon, const QString &whatsThis);
  void        bindWindowAction(QAction *action, QMdiSubWindow *win);
  /// The window's context menu (from its header line).
  void        windowMenu(QMdiSubWindow *win, const QPoint &globalPos);
  /// Window layout from/to the settings (Windows/... keys).
  void        restoreWindows();
  void        saveWindows();
  QIcon       arrangeIcon() const;
  /// Applies a colour design (Designs::Design) to the window and the views.
  void        setDesign(int design);
  /// Keeps the window actions checked when a window is closed or shown.
  bool        eventFilter(QObject *watched, QEvent *event) override;
  /// Saves window state; vetoed by MainWid::closeWin() on unsaved data.
  void        closeEvent(QCloseEvent *)Q_DECL_OVERRIDE;
  /// Raises this window when another instance asks for it ("RAISE_<id>").
  void        bringMainWindowToFront();
};

