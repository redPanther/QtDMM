//======================================================================
// File:		mainwin.h
// Author:	Matthias Toussaint
// Created:	Sun Sep  2 12:14:07 CEST 2001
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

#include <functional>

#include <QtGui>
#include <QtWidgets>
#include <QMenu>
#include <QCommandLineParser>

#include "ui_uimainwindow.h"
#include "core/recentfiles.h"
#include "core/valueref.h"

class InstanceWidget;
class DeviceSession;
class MeterController;
class LcdWidget;
class HelpDlg;
class AnalogMeter;
class ReadingsWidget;
class PoincarePlot;
class AlarmBar;
class ShortcutRegistry;
class ControlBar;
class FoldButton;
class MdiArranger;
class QMdiArea;
class QToolButton;
class QDockWidget;
class DeviceSidebar;
class QMdiSubWindow;

/// The application window: toolbars, status bar, the alarm banner and an
/// MDI area with four windows - LCD display, analog meter, graph (InstanceWidget)
/// and readings table - placed by an MdiArranger.
///
/// The meters of the process are in a DeviceSession: the window's and those
/// opened beside it (sidebar "Open devices", --also, SCPI); they record
/// together.
class MainWindow : public QMainWindow, private Ui::UIMainWindow
{
  Q_OBJECT
public:
  /// @param parser the processed command line (--debug, --config-dir, --config-id)
  /// @param parent parent widget, normally none
  MainWindow(QCommandLineParser &parser, QWidget *parent = nullptr);
  /// --debug: frame dump and the qtdmm.hid logging category.
  void      setConsoleLogging(bool);

protected Q_SLOTS:
  /// Recording state changed; enables/disables Start/Stop.
  void      runningSLOT(bool);
  /// The Connect action was toggled.
  void      connectSLOT(bool);
  /// Start action: asks about a recording not saved and its length, then records.
  void      startSLOT();
  void      on_action_About_triggered();
  void      on_action_Help_triggered();
  /// Shows the popup menu (the window has no menu bar).
  void      on_action_Menu_triggered();
  /// Checks the Connect action without triggering it.
  void      setConnectSLOT(bool);
  /// Shows the display as the settings say.
  void      showDisplaySLOT(bool show);
  /// The display was shown/hidden: the settings keep it.
  void      storeDisplaySLOT();
  /// Toolbar button style: icons only or icons with text.
  void      setUseTextLabel(bool on);
  /// Title = app name, instance id and the configured meter.
  void      updateWindowTitle();
  /// Record (Space): starts the recorder, or stops it when it is running.
  void      toggleRecordingSLOT();
  /// Record and Live follow the recorder: grey dot or red square, Live
  /// pressed while live and locked while recording.
  void      updateRecorderActions();
  /// F11
  void      setFullScreen(bool on);
  /// Ctrl+M: all toolbars at once
  void      setToolbarsHidden(bool hide);
  void      toolbarHiddenHint();   ///< after hiding the toolbar by hand: how it comes back (until switched off)
  /// Arrange actions and the title-bar action follow the arranger.
  void      syncArrangeActions();
  /// The dot of the device in My devices: green while readings come in,
  /// blinking with them, grey when they stop.
  void      updateLed();
  /// The arrow of the toolbar button at the end of the status line.
  void      updateToolbarButton();

protected:
  /// @name Parts of the constructor, in this order
  /// @{
  void      createViews();
  void      createSidebar();
  void      fillViewToolbar();
  QList<QAction *> createArrangeMenu();
  void      createDesignMenu();
  void      createStatusBar();
  void      placeWindow();
  /// @}

  DeviceSession     *m_session;   ///< the meters of this process and their network services
  InstanceWidget    *m_wid;
  LcdWidget *m_display;
  AnalogMeter   *m_meter;
  ReadingsWidget *m_readings;
  PoincarePlot *m_poincare;
  AlarmBar   *m_alarmBar;
  QMdiArea   *m_mdi;
  MdiArranger *m_arranger;
  QToolButton *m_toolbarButton = nullptr;   ///< toolbar on/off, at the end of the status line
  QTimer     *m_ledIdle;
  bool        m_ledActive = false;
  bool        m_ledBlink = false;
  qint64      m_lastReadingAt = 0;   ///< ms since the epoch, the newest main reading
  bool        m_lowBattery = false;  ///< its low battery annunciator
  QMdiSubWindow *m_displayWin;
  QMdiSubWindow *m_meterWin;
  QMdiSubWindow *m_graphWin;
  QMdiSubWindow *m_readingsWin;
  QMdiSubWindow *m_poincareWin;
  QAction    *m_displayAction = nullptr;
  QAction    *m_meterAction = nullptr;
  QAction    *m_readingsAction = nullptr;
  QAction    *m_poincareAction = nullptr;
  QAction    *m_addDeviceAction = nullptr;
  QDockWidget   *m_sidebarDock = nullptr;
  QElapsedTimer  m_designApplied;          ///< since setDesign(): its own palette change is no switch
  bool           m_designPending = false;  ///< the desktop switched: setDesign() on its way
  QAction       *m_sidebarAction = nullptr; ///< shows and hides the sidebar
  DeviceSidebar *m_sidebar = nullptr;     ///< "My devices" at the left
  /// "Settings..." of an entry: its meter's settings; the device in use
  /// takes them at once (unsaved readings first), unchanged ones change nothing.
  /// Info and settings of the entry @p id, on its Info tab or the Settings tab.
  void        deviceSettings(const QString &id, bool settingsTab = false);
  /// The Info tab's state of the entry @p id: in use where, connected,
  /// the last reading, the values; rich text.
  QString     deviceStatus(const QString &id) const;
  /// Opens the entry @p id beside this window's device (DeviceSession::open()),
  /// unless its port is in use.
  void        openBeside(const QString &id);
  void        showSidebar(bool show);
  /// The node "Open devices" anew: the meters of the session with their
  /// readings (each second while the sidebar is shown).
  void        updateOpenDevices();
  /// The node "Services": the SCPI server and HiSLIP of this window.
  void        updateServices();
  QTimer     *m_openTimer = nullptr;
  /// The places (DeviceLibrary::place()) the open devices use, with the
  /// meter that uses each.
  QMap<QString, const MeterController *> placeOwners() const;
  /// The places (DeviceLibrary::place()) the open devices use, with
  /// "In use here" / "Open beside: x" for the assistant's cards.
  QMap<QString, QString> placesInUse() const;
  /// The assistant "Add device": the device into My devices, then into
  /// this window or beside its device.
  void        addDevice();
  /// The empty start: a window without a meter shows the big button "Add
  /// device" over the area instead of the views.
  void        updateEmptyStart();
  QWidget    *m_emptyStart = nullptr;   ///< covers the area, with the button
  QAction    *m_arrangeTop;
  QAction    *m_arrangeLeft;
  QAction    *m_arrangeFixed;
  QAction    *m_arrangeFree;
  QAction    *m_titleBars;
  QAction    *m_autoSaveLayout;   ///< Windows/auto-save: the layout is saved on exit
  bool        m_startDisplay = true;   ///< Display/show of the start layout (auto-save off)
  QMenu      *m_arrangeMenu;
  QMenu      *m_recentMenu = nullptr;   ///< Arrange > Recent workspaces
  QMenu      *m_designMenu;
  bool        m_restoring = false;   ///< restoreWindows() is setting the actions
  ControlBar *m_controls;            ///< the meter's keys under the display
  FoldButton *m_fold;
  bool        m_controlsSupported = false;
  /// Folds the keys away (Display/controls-hidden), from the fold button
  /// or "Hide controls" in the display's menu.
  void        setControlsFolded(bool folded);
  /// Keys and fold button shown for a meter that has keys, not folded.
  void        updateControls();
  // Window size (package 26.2, 4a): the window starts at the size its
  // content needs and grows when a view is shown for the first time, up to
  // 84 % of the screen per side. It never shrinks by itself and stops
  // growing for good once the user sized it. Views shown while it is
  // maximized or full screen get their room once when it comes back.
  bool        m_userSized = false;   ///< persisted as Windows/user-sized
  bool        m_growEnabled = false; ///< off until the window is on screen
  QSize       m_expectSize;          ///< the size we asked for last
  QSize       m_pendingGrow{0, 0};   ///< growth asked for while maximized
  bool        m_wasFull = false;     ///< maximized or full screen before the last state change
  bool        m_unmaximizing = false;   ///< back from maximized, the size settling
  QSet<QObject *> m_grown;           ///< views the window already grew for
  int         em() const;            ///< a font height, the unit of all sizes
  /// Width and height a view adds to the window (0 = none in that direction).
  QSize       growthFor(QMdiSubWindow *win) const;
  /// Grows the window by @p delta within the limits, moving it up/left when
  /// the frame would stick out of the screen.
  void        autoGrow(const QSize &delta);
  /// A view was shown: grow for it once per session.
  void        growFor(QMdiSubWindow *win);
  QAction    *m_fullScreen;
  QAction    *m_toolbars;
  QAction    *m_zoomIn;
  QAction    *m_zoomOut;
  QAction    *m_zoomFit;
  QAction    *m_copyImage;
  ShortcutRegistry *m_shortcuts = nullptr;
  bool        m_running;
  QLabel     *m_error;
  QLabel     *m_info;
  QLabel     *m_limitCount;   ///< PASS/FAIL count, while the limit test is on
  QMenu      *m_menu;
  HelpDlg    *m_helpDlg;
  QString     m_configId;

  void        setupIcons();
  void        createActions();
  /// Menu-only actions and their shortcuts (the window has no menu bar).
  void        createExtraActions();
  /// The commands with a key for the page Keyboard (ShortcutRegistry);
  /// the user's keys applied, the tooltips name them: "Record (Space)".
  /// @p workspace: the menu's Load/Save workspace.
  void        createShortcuts(const QList<QAction *> &workspace);
  /// Adds @p view as an MDI window with @p title (@p role: MdiArranger::Role;
  /// @p name identifies it in the settings).
  QMdiSubWindow *addView(QWidget *view, const QString &title, int role, const QString &name);
  /// The context menu of the sidebar's row of value @p id (ValueMenu).
  void        valueMenu(int id, const QPoint &globalPos);
  /// A checkable action that shows/hides @p win.
  QAction    *windowAction(QMdiSubWindow *win, const QString &text, const char *shortcut,
                           const char *icon, const QString &whatsThis);
  void        bindWindowAction(QAction *action, QMdiSubWindow *win);
  /// The window's context menu (from its header line).
  void        windowMenu(QMdiSubWindow *win, const QPoint &globalPos);
  /// The limit test's dialog; @p enable ticks "Check the limits" in it
  /// (switched on from the menu without limits yet).
  void        editLimits(bool enable);
  /// Window layout from/to the settings (Windows/... keys).
  void        restoreWindows();
  void        saveWindows();
  /// @name Workspace: the window layout, from the settings or a file
  /// @{
  /// Reads a key; the type of @p def says bool or string.
  using WorkspaceGet = std::function<QVariant(const QString &key, const QVariant &def)>;
  using WorkspaceSet = std::function<void(const QString &key, const QVariant &value)>;
  /// Shows the windows, mode, tree, title bars, design and (Free) positions.
  void        applyWorkspace(const WorkspaceGet &get);
  void        storeWorkspace(const WorkspaceSet &set);
  void        loadWorkspace();
  /// Loads the workspace @p file; false (with a message) when it is none.
  bool        loadWorkspaceFile(const QString &file);
  void        saveWorkspace();
  /// The workspaces used last, shared by all instances (workspaces.conf).
  RecentFiles recentWorkspaces() const;
  /// The sidebar's node and the menu "Recent workspaces" anew.
  void        updateRecentWorkspaces();
  /// @}
  /// Applies a colour design (Designs::Design) to the window and the views.
  void        setDesign(int design);
  /// Keeps the window actions checked when a window is closed or shown.
  bool        eventFilter(QObject *watched, QEvent *event) override;
  /// Saves window state; vetoed by InstanceWidget::closeWin() on unsaved data.
  void        closeEvent(QCloseEvent *)Q_DECL_OVERRIDE;
  /// No context menu to hide the toolbars: they are always there.
  QMenu      *createPopupMenu() override;
  /// A size we did not ask for came from the user (or the window manager
  /// on their behalf, like snapping to a screen half): no more growing.
  void        resizeEvent(QResizeEvent *) override;
  void        changeEvent(QEvent *) override;
  void        showEvent(QShowEvent *) override;
  /// Raises this window when another instance asks for it ("RAISE_<id>").
  void        bringMainWindowToFront();
};

