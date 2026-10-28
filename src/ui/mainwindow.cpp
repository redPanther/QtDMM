//======================================================================
// File:		mainwin.cpp
// Author:	Matthias Toussaint
// Created:	Sun Sep  2 12:15:28 CEST 2001
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

#include <QtGui>
#include <QtWidgets>
#include <QTimer>
#include <QMenu>
#include <cmath>

#include "ui/mainwindow.h"
#include "ui/frameclock.h"
#include "ui/dialogs/helpdlg.h"
#include "ui/dialogs/devicesettingsdlg.h"
#include "ui/devicesettings.h"
#include "ui/devicesidebar.h"
#include "ui/valuemenu.h"
#include "ui/tilebutton.h"
#include "ui/dialogs/adddevicedlg.h"
#include "ui/dialogs/limitdlg.h"
#include "device/transport.h"
#include "device/transports/serial.h"
#include "core/devicelibrary.h"
#include "core/sampletypes.h"
#include "core/siprefix.h"
#include "recording/recordingstore.h"
#include "ui/instancewidget.h"
#include "ui/views/graphwidget.h"
#include "ui/views/lcdwidget.h"
#include "ui/views/poincareplot.h"
#include "ui/settings/settingsdialog.h"
#include "ui/settings/generalprefs.h"
#include "ui/settings/guiprefs.h"
#include "ui/settings/keyboardprefs.h"
#include "ui/views/analogmeter.h"
#include "ui/views/readingswidget.h"
#include "core/settings.h"
#include "ui/alarmbar.h"
#include "ui/mdiarranger.h"
#include "service/metercontroller.h"
#include "device/meterconnection.h"
#include "ui/designs.h"
#include "ui/controlbar.h"
#include "ui/frontpanel.h"
#include "ui/shortcuts.h"
#include "service/devicesession.h"
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QLoggingCategory>
#include <QIconEngine>

namespace
{
// An icon drawn by its paint(): the pixmap is the same drawing.
class PaintedIconEngine : public QIconEngine
{
public:
  QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
  {
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    paint(&painter, QRect(QPoint(0, 0), size), mode, state);
    return pm;
  }
};

// The Record button's symbol, drawn at any size: a grey dot to start, a red
// square while it records
class RecordIconEngine : public PaintedIconEngine
{
public:
  explicit RecordIconEngine(bool stop) : m_stop(stop) {}
  void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
  {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const qreal side = qMin(rect.width(), rect.height()) * (m_stop ? 0.56 : 0.62);
    const QRectF r(QPointF(rect.center()) + QPointF(0.5, 0.5) - QPointF(side / 2, side / 2), QSizeF(side, side));
    QColor fill = m_stop ? QColor(0xd3, 0x2f, 0x2f) : QColor(0x8c, 0x8c, 0x8c);
    if (mode == QIcon::Disabled)
      fill.setAlpha(80);
    QColor edge = fill.darker(140);
    edge.setAlpha(fill.alpha());
    painter->setPen(QPen(edge, qMax<qreal>(1.0, side / 12)));
    painter->setBrush(fill);
    if (m_stop)
      painter->drawRoundedRect(r, side * 0.12, side * 0.12);
    else
      painter->drawEllipse(r);
    painter->restore();
  }
  QIconEngine *clone() const override { return new RecordIconEngine(m_stop); }

private:
  bool m_stop;
};

/// A theme symbol with a "+" in its lower right corner (Add device: the
/// meter and "add"), looked up when painted, so it follows the symbol set.
class PlusIconEngine : public PaintedIconEngine
{
public:
  explicit PlusIconEngine(const QString &name) : m_name(name) {}
  void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
  {
    QIcon::fromTheme(m_name).paint(painter, rect, Qt::AlignCenter, mode, state);
    const int side = qMax(6, int(qMin(rect.width(), rect.height()) * 0.55));
    const QRect corner(rect.right() + 1 - side, rect.bottom() + 1 - side, side, side);
    QIcon::fromTheme("list-add").paint(painter, corner, Qt::AlignCenter, mode, state);
  }
  QIconEngine *clone() const override { return new PlusIconEngine(m_name); }

private:
  QString m_name;
};
}

MainWindow::MainWindow(QCommandLineParser &parser, QWidget *parent)
  : QMainWindow(parent)
  , m_running(false)
  , m_menu(nullptr)
  , m_helpDlg(nullptr)
{
  setupUi(this);
  setupIcons();
  m_configId = parser.value("config-id");

  QWidget* spacer = new QWidget();
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  this->toolBarMenu->addWidget(spacer);
  this->toolBarMenu->addAction(this->action_Menu);
  // the meters of the process share the network services: the window's and
  // those opened without a window (--also, SCPI QTDMM:DEVice:OPEN)
  m_session = new DeviceSession(this);
  m_session->setInstanceId(m_configId.isEmpty() ? QString("default") : m_configId);
  m_wid = new InstanceWidget(m_configId, parser.value("config-dir"), m_session, this);
  m_session->setConfigDir(m_wid->settings()->configDir());
  m_session->setSigrokExe(m_wid->settings()->getString("Port settings/sigrok_exe", "sigrok-cli"));
  // --also <id>: more of "My devices", after the window's meter has its own
  QTimer::singleShot(0, this, [this, ids = parser.values("also")]
  {
    for (const QString &id : ids)
    {
      QString error;
      if (!m_session->open(id, &error))
      {
        qWarning().noquote() << "--also" << id << ":" << error.section(',', 1);
        // one that does not connect said why in the status bar already
        if (!error.startsWith("-240,"))
          m_error->setText(tr("--also %1: %2").arg(id, error.section(',', 1)));
      }
    }
  });
  m_wid->setFrameShape(QFrame::NoFrame);
  setConsoleLogging(parser.isSet("debug"));

  createActions();

  createViews();
  createSidebar();
  fillViewToolbar();
  const QList<QAction *> workspace = createArrangeMenu();
  updateRecentWorkspaces();
  createDesignMenu();

  updateWindowTitle();
  connect(m_wid, &InstanceWidget::configChanged, this, &MainWindow::updateWindowTitle);
  connect(m_wid, &InstanceWidget::configChanged, this, &MainWindow::updateLed);
  // the design can be chosen in the settings (General), too
  connect(m_wid, &InstanceWidget::configChanged, this, [this]
  {
    const Designs::Design d = Designs::fromName(m_wid->settings()->getString("Windows/design", "dark"));
    if (d != Designs::current())
      setDesign(d);
  });

  createExtraActions();
  createShortcuts(workspace);

  connect(m_wid, &InstanceWidget::running, this, &MainWindow::runningSLOT);
  connect(m_wid, &InstanceWidget::recorderState, this, &MainWindow::updateRecorderActions);

  connectSLOT(false);

  createStatusBar();
  placeWindow();

  // auto-connect at start, but not before a meter was ever chosen: a fresh
  // window would otherwise try the first serial port it finds; without
  // one the window starts empty
  // (the window's meter, not the active one)
  if (m_wid->dmmConfigured())
    QTimer::singleShot(1000, this, [this]
    {
      windowConnected(true);
      m_wid->connectSLOT(true);   // may set it back to off (setConnect())
    });
  // the meters open beside it last time, after it
  QTimer::singleShot(1000, this, &MainWindow::openPending);
  updateEmptyStart();
  connect(m_wid, &InstanceWidget::configChanged, this, &MainWindow::updateEmptyStart);
  connect(m_wid, &InstanceWidget::configChanged, this, [this]
  {
    for (QMdiSubWindow *win : std::as_const(m_panelWins))
      applyLcdLook(panelOf(win));
  });
  connect(m_wid, &InstanceWidget::configChanged, this, &MainWindow::updateRecentWorkspaces);
  // the sidebar as left, with two devices or more; a window without a meter
  // shows it when there are devices to choose from
  const bool sidebarSet = !m_wid->settings()->getString("Windows/sidebar").isEmpty();
  showSidebar(DeviceSidebar::shownAtStart(sidebarSet ? QVariant(m_wid->settings()->getBool("Windows/sidebar"))
                                                     : QVariant(),
                                          int(m_wid->devices()->list().size()), m_wid->dmmConfigured()));
  updateServices();   // the servers may have started before this window listened
}

// The MDI area with the alarm banner, the empty start, the views and an action per view.
void MainWindow::createViews()
{
  // the window: alarm banner on top, the MDI area with the four views below
  auto *central = new QWidget(this);
  auto *centralLayout = new QVBoxLayout(central);
  centralLayout->setContentsMargins(0, 0, 0, 0);
  centralLayout->setSpacing(0);
  m_alarmBar = new AlarmBar(central);
  centralLayout->addWidget(m_alarmBar);
  m_mdi = new QMdiArea(central);
  m_mdi->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_mdi->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  centralLayout->addWidget(m_mdi, 1);
  setCentralWidget(central);
  m_arranger = new MdiArranger(m_mdi, this);
  // the empty start: one big button on the area's background, over the
  // views, which stay arranged below it
  m_emptyStart = new QWidget(m_mdi);
  m_emptyStart->setAutoFillBackground(true);
  auto *emptyLayout = new QGridLayout(m_emptyStart);
  auto *addButton = new TileButton(m_emptyStart);
  addButton->setObjectName("ui_emptyStart");
  addButton->setIcon(QIcon::fromTheme("list-add"));
  addButton->setIconSize(QSize(64, 64));
  addButton->setText(tr("&Add device"));
  addButton->setToolTip(tr("Add device: a meter, a sensor or a calculated value"));
  QFont big = addButton->font();
  big.setPointSizeF(big.pointSizeF() * 1.4);
  addButton->setFont(big);
  addButton->setMinimumSize(260, 160);
  emptyLayout->addWidget(addButton, 0, 0, Qt::AlignCenter);
  m_emptyStart->hide();
  m_mdi->installEventFilter(this);
  connect(addButton, &QToolButton::clicked, this, &MainWindow::addDevice);

  MeterController *ctl = m_wid->controller();
  connect(ctl, &MeterController::alarmBannerChanged, m_alarmBar, &AlarmBar::setAlarms);
  connect(m_alarmBar, &AlarmBar::acknowledged, ctl, &MeterController::acknowledgeAlarms);

  // the four views; none of them knows another, they all hang off the
  // controller (see InstanceWidget)
  // the window's meter's front: the LCD with its keys below (only for
  // meters that have them) and the fold button in its corner
  m_panel = new FrontPanel(m_session, this);
  m_panel->setFolded(m_wid->settings()->getBool("Display/controls-hidden", false));
  m_wid->setPanel(m_panel);
  connect(m_panel, &FrontPanel::foldToggled, this, [this](bool folded) { setControlsFolded(folded); });
  connect(m_panel, &FrontPanel::keysChanged, this, [this] { updateControls(); });
  connect(m_panel, &FrontPanel::keyFailed, this, [this]
  {
    statusBar()->showMessage(tr("The key did not reach the meter: it is not connected yet."), 4000);
  });
  // readConfig() has set the protocol: the keys this meter has
  connect(m_wid, &InstanceWidget::remoteControl, m_panel, &FrontPanel::updateKeys);
  m_displayWin = addView(m_panel, tr("Display"), MdiArranger::Instrument, "display");

  m_meter = new AnalogMeter(this);
  m_wid->setMeter(m_meter);
  m_meterWin = addView(m_meter, tr("Analog meter"), MdiArranger::Instrument, "meter");
  // no cell bigger than the instruments can fill
  m_arranger->setContentAspect(m_displayWin, LcdWidget::kMinAspect, LcdWidget::kMaxAspect);
  m_arranger->setContentAspect(m_meterWin, AnalogMeter::kMinAspect, AnalogMeter::kMaxAspect);

  m_graphWin = addView(m_wid, tr("Graph"), MdiArranger::Graph, "graph");
  // the drawing clock of the views; 0 = off, the graph moves with each reading only
  FrameClock::instance()->setRate(m_wid->settings()->getInt("Graph/frame-rate", 30));

  m_readings = new ReadingsWidget(this);
  m_wid->setReadingLog(m_readings->log());   // the row limit is the store's: set it after
  m_readings->setMaxRows(m_wid->settings()->getInt("ReadingLog/max-rows", 10000));
  m_readingsWin = addView(m_readings, tr("Readings"), MdiArranger::Table, "readings");

  // the scatter of the main value: every reading, also while hidden
  m_poincare = new PoincarePlot(this);
  m_poincare->setSettings(m_wid->settings());
  connect(m_wid, &InstanceWidget::meterChanged, m_poincare, &PoincarePlot::clear);
  m_poincareWin = addView(m_poincare, tr("Poincaré plot"), MdiArranger::Table, "poincare");
  m_wid->setPoincare(m_poincare);

  // right-click on display or meter: the window's menu (also without title bar)
  for (auto [view, win] : { std::pair<QWidget *, QMdiSubWindow *>{m_panel->display(), m_displayWin},
                            std::pair<QWidget *, QMdiSubWindow *>{m_meter, m_meterWin},
                            std::pair<QWidget *, QMdiSubWindow *>{m_poincare, m_poincareWin} })
  {
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view, &QWidget::customContextMenuRequested, this, [this, view = view, w = win](const QPoint &pos)
    {
      windowMenu(w, view->mapToGlobal(pos));
    });
  }

  // one checkable action per window: toolbar buttons, menu entries, shortcuts
  m_displayAction = windowAction(m_displayWin, tr("&Display"), "Ctrl+1", "qtdmm-display",
    tr("<html><head/><body><p><span style=\" font-weight:600;\">Display</span></p>"
       "<p>Show the reading on the LCD-style digital display.</p></body></html>"));
  m_meterAction = windowAction(m_meterWin, tr("Analog &meter"), "Ctrl+2", "qtdmm-meter",
    tr("<html><head/><body><p><span style=\" font-weight:600;\">Analog meter</span></p>"
       "<p>Show the reading on a moving-coil style instrument.</p></body></html>"));
  m_readingsAction = windowAction(m_readingsWin, tr("&Readings table"), "Ctrl+4", "table",
    tr("<html><head/><body><p><span style=\" font-weight:600;\">Readings table</span></p>"
       "<p>Every reading the meter sent, one row each, with time, mode and range - "
       "the raw protocol of the session next to the graph. Copy rows to a "
       "spreadsheet or export them as CSV.</p></body></html>"));
  m_poincareAction = windowAction(m_poincareWin, tr("Poi&ncaré plot"), "Ctrl+5", "qtdmm-poincare",
    tr("<html><head/><body><p><span style=\" font-weight:600;\">Poincaré plot</span></p>"
       "<p>Each reading against the next one: noise widens the cloud across the diagonal, "
       "drift stretches it along it. SD1 and SD2 put both in numbers.</p></body></html>"));
  // the graph keeps its action from the .ui (toolbar button with Ctrl+3)
  bindWindowAction(action_Graph, m_graphWin);
  // graph and table have menus of their own: they begin with the window's
  // entry, as the window menu of the others
  auto hideWindow = [this](QMdiSubWindow *win)
  {
    QAction *hide = new QAction(tr("&Hide window"), this);
    connect(hide, &QAction::triggered, win, &QWidget::hide);
    return hide;
  };
  m_wid->graph()->setWindowActions({ hideWindow(m_graphWin) });
  m_readings->setWindowActions({ hideWindow(m_readingsWin) });
  m_displayAction->setObjectName("action_WindowDisplay");
  m_meterAction->setObjectName("action_WindowMeter");
  m_readingsAction->setObjectName("action_WindowReadings");
  m_poincareAction->setObjectName("action_WindowPoincare");

  // a front panel for each meter open beside the window's, also those
  // opened before the window (--also)
  connect(m_session, &DeviceSession::opened, this, &MainWindow::addPanel);
  // a meter of the table opened again: its readings come in again
  connect(m_session, &DeviceSession::opened, this, &MainWindow::connectTableSources);
  connect(m_session, &DeviceSession::closed, this, &MainWindow::connectTableSources);
  connect(m_session, &DeviceSession::closed, this, &MainWindow::removePanel);
  for (MeterController *ctl : m_session->controllers())
    addPanel(ctl);
}

void MainWindow::addPanel(MeterController *ctl)
{
  if (ctl == m_wid->controller() || m_panelWins.contains(ctl->source()))
    return;
  const QString id = ctl->deviceId();
  auto *panel = new FrontPanel(m_session, this);
  panel->setValue(ValueRef(0, ctl->source()));
  applyLcdLook(panel);
  applyFrame(panel);
  QMdiSubWindow *win = addView(panel, ctl->deviceName(), MdiArranger::Instrument, "panel-" + id);
  m_panelWins.insert(ctl->source(), win);
  connect(panel, &FrontPanel::foldToggled, this, [this] { updateControls(); });
  connect(panel, &FrontPanel::keysChanged, this, [this] { updateControls(); });
  connect(panel, &FrontPanel::keyFailed, this, [this, panel]
  {
    statusBar()->showMessage(tr("%1: the key did not reach the meter: it is not connected yet.")
                             .arg(panel->controller() ? panel->controller()->deviceName() : QString()), 4000);
  });
  panel->display()->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(panel->display(), &QWidget::customContextMenuRequested, this, [this, panel, win](const QPoint &pos)
  {
    windowMenu(win, panel->display()->mapToGlobal(pos));
  });
  const PanelState state = m_panelStates.take(id);
  applyPanelState(win, state);
  if (state.shown && !m_restoring)
    growFor(win);
}

void MainWindow::applyPanelState(QMdiSubWindow *win, const PanelState &state)
{
  FrontPanel *panel = panelOf(win);
  MeterController *ctl = panel->controller();
  if (ctl)
  {
    panel->setValue(ValueRef(state.value.id, ctl->source()));
    ctl->setMinMaxValue(state.value.id);
  }
  panel->setFolded(state.folded);
  if (state.secondaries)
    panel->setSecondaries(state.secondaries);
  const QStringList g = state.geometry.split(' ');
  if (m_arranger->mode() == MdiArranger::Free && g.size() == 4)
    win->setGeometry(g[0].toInt(), g[1].toInt(), g[2].toInt(), g[3].toInt());
  win->setVisible(state.shown);
  updateControls();
}

void MainWindow::removePanel(const QString &source)
{
  QMdiSubWindow *win = m_panelWins.take(source);
  if (!win)
    return;
  m_grown.remove(win->widget());
  m_grown.remove(win);
  delete win;
  m_arranger->arrange();
}

QList<QMdiSubWindow *> MainWindow::panelWindows() const
{
  return QList<QMdiSubWindow *>{ m_displayWin } + m_panelWins.values();
}

FrontPanel *MainWindow::panelOf(const QMdiSubWindow *win)
{
  return win ? qobject_cast<FrontPanel *>(win->widget()) : nullptr;
}

void MainWindow::applyLcdLook(FrontPanel *panel)
{
  if (panel == m_panel)
    return;   // InstanceWidget sets the window's (readConfig())
  const GuiPrefs *gui = m_wid->settingsDialog()->guiPage();
  LcdWidget *lcd = panel->display();
  lcd->setFaceColor(gui->displayBgColor());
  lcd->setLcdVariant(m_panel->display()->lcdVariant());
  if (const MeterController *ctl = panel->controller())
  {
    const DmmDecoder::DMMInfo &info = ctl->dmm()->dmmInfo();
    lcd->setDisplayMode(info.display, gui->showMinMax(), gui->showBar(), info.valueCount());
  }
}

void MainWindow::openPending()
{
  const QStringList ids = std::exchange(m_pendingOpen, {});
  // a panel added becomes the active window: the one before stays active
  QMdiSubWindow *active = m_mdi->activeSubWindow();
  for (const QString &id : ids)
    if (!m_session->controller("device:" + id))
      openBeside(id);
  // none yet at the start: the window's meter, and a click on a panel makes it active
  m_mdi->setActiveSubWindow(active);
  if (!active)
    setActive(m_wid->controller());
  // a layout naming panels takes once they are there
  if (!m_pendingLayout.isEmpty() && m_arranger->setLayoutText(m_pendingLayout))
    m_arranger->arrange();
  m_pendingLayout.clear();
}

// The sidebar Devices: My devices, the open devices, the services.
void MainWindow::createSidebar()
{
  // the sidebar "Devices" at the left: My devices, a click switches
  m_sidebar = new DeviceSidebar(m_wid->devices(), this);
  m_sidebarDock = new QDockWidget(tr("Devices"), this);
  m_sidebarDock->setObjectName("ui_devicesDock");
  m_sidebarDock->setFeatures(QDockWidget::NoDockWidgetFeatures);   // the toolbar button shows and hides it
  m_sidebarDock->setWidget(m_sidebar);
  addDockWidget(Qt::LeftDockWidgetArea, m_sidebarDock);
  m_sidebarDock->hide();
  // an own action: without DockWidgetClosable Qt disables the toggleViewAction()
  m_sidebarAction = new QAction(QIcon::fromTheme("view-sidetree"), tr("De&vices"), this);
  m_sidebarAction->setObjectName("ui_sidebarAction");
  m_sidebarAction->setCheckable(true);
  m_sidebarAction->setShortcut(QKeySequence("F9"));   // as the sidebars of KDE's file managers
  QAction *sidebar = m_sidebarAction;
  sidebar->setToolTip(tr("Devices: show or hide the sidebar with My devices and the open devices"));
  addAction(sidebar);   // F9 also with the toolbar hidden
  sidebar->setWhatsThis(tr("<html><head/><body><p><span style=\" font-weight:600;\">Devices</span></p>"
                           "<p>The sidebar with My devices: click one to switch to it, its context menu has its "
                           "settings, rename, a new window and remove. Drag an entry to change the order."
                           "</p></body></html>"));
  connect(sidebar, &QAction::triggered, this, [this](bool on)
  {
    showSidebar(on);
    m_wid->settings()->setBool("Windows/sidebar", on);
  });
  connect(m_sidebar, &DeviceSidebar::switchRequested, m_wid, [this](const QString &id)
  {
    // the selection is the device in use: the new one, or after Cancel at
    // the question about unsaved data the one before
    m_wid->switchDevice(id);
    m_sidebar->setCurrentItem(m_sidebar->deviceItem(m_wid->currentDevice()));
  });
  connect(m_sidebar, &DeviceSidebar::settingsRequested, this, [this](const QString &id) { deviceSettings(id); });
  connect(m_sidebar, &DeviceSidebar::valueMenuRequested, this, &MainWindow::valueMenu);
  m_sidebar->setValueMenuFiller([this](QMenu *menu, const ValueRef &value) { fillValueMenu(menu, value); });
  connect(m_wid->graph(), &GraphWidget::notice, this, [this](const QString &text)
  {
    statusBar()->showMessage(text, 5000);
  });
  connect(m_sidebar, &DeviceSidebar::openBesideRequested, this, &MainWindow::openBeside);
  connect(m_sidebar, &DeviceSidebar::closeRequested, this, [this](const QString &id)
  {
    m_session->close(id);
  });
  connect(m_sidebar, &DeviceSidebar::frontPanelRequested, this, [this](const QString &id)
  {
    QMdiSubWindow *win = id == m_wid->controller()->source() ? m_displayWin : m_panelWins.value("device:" + id);
    if (!win)
      return;
    if (win == m_displayWin)
      m_displayAction->setChecked(true);
    else if (win->isHidden())
    {
      win->show();
      growFor(win);
    }
    m_mdi->setActiveSubWindow(win);
    m_arranger->arrange();
  });
  connect(m_sidebar, &DeviceSidebar::connectRequested, this, [this](const QString &id, bool on)
  {
    if (id == m_wid->controller()->source())
    {
      m_wid->connectSLOT(on);
      windowConnected(on);
    }
    else if (MeterController *ctl = m_session->controller("device:" + id))
    {
      ctl->connectMeter(on);
      if (ctl == m_active)
        action_Connect->setChecked(ctl->dmm()->isWanted());
    }
    updateOpenDevices();
  });
  connect(m_session, &DeviceSession::opened, this, &MainWindow::updateOpenDevices);
  connect(m_session, &DeviceSession::closed, this, &MainWindow::updateOpenDevices);
  connect(m_sidebar, &DeviceSidebar::addDeviceRequested, this, &MainWindow::addDevice);
  connect(m_sidebar, &DeviceSidebar::serviceSettingsRequested, m_wid, &InstanceWidget::configScpiSLOT);
  connect(m_sidebar, &DeviceSidebar::workspaceRequested, this, &MainWindow::loadWorkspaceFile);
  connect(m_sidebar, &DeviceSidebar::removeWorkspaceRequested, this, [this](const QString &file)
  {
    recentWorkspaces().remove(file);
    updateRecentWorkspaces();
  });
  // the readings of the others change all the time
  m_openTimer = new QTimer(this);
  m_openTimer->setInterval(1000);
  connect(m_openTimer, &QTimer::timeout, this, &MainWindow::updateOpenDevices);
  m_openTimer->start();
}

// Add device, Devices and the views on the toolbar.
void MainWindow::fillViewToolbar()
{
  toolBarDMM->insertAction(toolBarDMM->actions().value(0), m_sidebarAction);
  auto *plusIcon = new PlusIconEngine("qtdmm-dmm");   // QIcon owns it
  m_addDeviceAction = new QAction(QIcon(plusIcon), tr("&Add device..."), this);
  m_addDeviceAction->setObjectName("action_AddDevice");
  m_addDeviceAction->setToolTip(tr("Add device: a meter, a sensor or a calculated value"));
  m_addDeviceAction->setWhatsThis(tr("<html><head/><body><p><span style=\" font-weight:600;\">Add device</span></p>"
                                     "<p>Step by step: how the meter is connected, which one it is, and whether it "
                                     "goes into this window or a new one. It is kept in My devices.</p></body></html>"));
  connect(m_addDeviceAction, &QAction::triggered, this, &MainWindow::addDevice);
  toolBarDMM->insertAction(m_sidebarAction, m_addDeviceAction);

  toolBarDMM->addSeparator();
  toolBarDMM->addAction(m_displayAction);
  toolBarDMM->addAction(m_meterAction);
  toolBarDMM->addAction(m_readingsAction);
  toolBarDMM->addAction(m_poincareAction);
  // the graph is a window like the others (it sat with the recording)
  toolBarDMM->addAction(action_Graph);
  connect(m_displayAction, &QAction::toggled, this, &MainWindow::storeDisplaySLOT);
}

// Arrange: the arrangement, title bars, workspaces; returns Load and Save workspace for the keys.
QList<QAction *> MainWindow::createArrangeMenu()
{
  // arrangement: automatic (displays on top or on the left), fixed or free,
  // title bars on/off
  auto *arrangeGroup = new QActionGroup(this);
  auto arrangeAction = [this, arrangeGroup](const QString &text, MdiArranger::Mode mode, const QString &whatsThis)
  {
    auto *a = new QAction(text, arrangeGroup);
    a->setCheckable(true);
    a->setWhatsThis(whatsThis);
    connect(a, &QAction::triggered, this, [this, mode] { m_arranger->setMode(mode); });
    return a;
  };
  const QString arrangeHint = tr("<p>Drag a divider between two windows to share the space differently. "
                                 "Ctrl+drag a window (or drag its title bar) onto another one to swap them.</p>");
  m_arrangeTop = arrangeAction(tr("Displays on &top"), MdiArranger::DisplaysOnTop,
                               tr("<html><head/><body><p><span style=\" font-weight:600;\">Displays on top</span></p>"
                                  "<p>The displays share a strip at the top, the graph takes the rest and the "
                                  "readings table a column on the right; everything follows the window size."
                                  "</p>%1</body></html>").arg(arrangeHint));
  m_arrangeLeft = arrangeAction(tr("Displays on the &left"), MdiArranger::DisplaysOnLeft,
                                tr("<html><head/><body><p><span style=\" font-weight:600;\">Displays on the left</span></p>"
                                   "<p>The displays share a column on the left, the graph and the readings table "
                                   "take the rest; everything follows the window size.</p>%1</body></html>")
                                  .arg(arrangeHint));
  m_arrangeFixed = arrangeAction(tr("F&ixed"), MdiArranger::Fixed,
                                 tr("<html><head/><body><p><span style=\" font-weight:600;\">Fixed</span></p>"
                                    "<p>Keeps the layout as it is: from Displays on top or on the left as they "
                                    "are, from Free the windows snap into a grid made from their positions. "
                                    "A window shown later gets a place at the edge.</p>%1</body></html>")
                                   .arg(arrangeHint));
  m_arrangeFree = arrangeAction(tr("&Free"), MdiArranger::Free,
                                tr("<html><head/><body><p><span style=\" font-weight:600;\">Free</span></p>"
                                   "<p>Place and size the windows as you like; Ctrl+drag moves a window, "
                                   "also one without title bar.</p></body></html>"));

  m_titleBars = new QAction(tr("&Hide title bars"), this);
  m_titleBars->setObjectName("action_TitleBars");
  m_titleBars->setCheckable(true);
  m_titleBars->setShortcut(QKeySequence("F10"));
  m_titleBars->setWhatsThis(tr("<html><head/><body><p><span style=\" font-weight:600;\">Hide title bars</span></p>"
                               "<p>Windows without title bar sit flush next to each other. Right-click the display "
                               "or the meter for the window's menu; Ctrl+drag moves a window, in the arranged modes onto "
                               "another one to swap them."
                               "</p></body></html>"));
  connect(m_titleBars, &QAction::triggered, m_arranger, &MdiArranger::setTitleBarsHidden);
  connect(m_arranger, &MdiArranger::changed, this, &MainWindow::syncArrangeActions);

  m_arrangeMenu = new QMenu(tr("&Arrange"), this);
  m_arrangeMenu->addAction(m_arrangeTop);
  m_arrangeMenu->addAction(m_arrangeLeft);
  m_arrangeMenu->addAction(m_arrangeFixed);
  m_arrangeMenu->addAction(m_arrangeFree);
  m_arrangeMenu->addSeparator();
  m_arrangeMenu->addAction(m_titleBars);
  m_arrangeMenu->addSeparator();
  QAction *loadWs = m_arrangeMenu->addAction(QIcon::fromTheme("document-open"), tr("L&oad workspace..."));
  loadWs->setObjectName("action_LoadWorkspace");
  loadWs->setWhatsThis(tr("<html><head/><body><p><span style=\" font-weight:600;\">Load workspace</span></p>"
                          "<p>Takes a window layout saved with <i>Save workspace</i>: which windows are shown, "
                          "the arrangement, title bars, toolbar, design and the size of the main window.</p>"
                          "</body></html>"));
  connect(loadWs, &QAction::triggered, this, &MainWindow::loadWorkspace);
  // filled from the file each time: another QtDMM may have added one
  m_recentMenu = m_arrangeMenu->addMenu(QIcon::fromTheme("document-open-recent"), tr("&Recent workspaces"));
  m_recentMenu->setObjectName("menu_RecentWorkspaces");
  connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::updateRecentWorkspaces);
  QAction *saveWs = m_arrangeMenu->addAction(QIcon::fromTheme("document-save-as"), tr("&Save workspace..."));
  saveWs->setObjectName("action_SaveWorkspace");
  saveWs->setWhatsThis(tr("<html><head/><body><p><span style=\" font-weight:600;\">Save workspace</span></p>"
                          "<p>Writes the window layout to a file: which windows are shown, the arrangement, "
                          "title bars, toolbar, design and the size of the main window.</p></body></html>"));
  connect(saveWs, &QAction::triggered, this, &MainWindow::saveWorkspace);
  m_autoSaveLayout = m_arrangeMenu->addAction(tr("Save layout on e&xit"));
  m_autoSaveLayout->setCheckable(true);
  m_autoSaveLayout->setChecked(true);
  m_autoSaveLayout->setWhatsThis(tr("<html><head/><body><p><span style=\" font-weight:600;\">Save layout on exit</span></p>"
                                    "<p>On: QtDMM starts with the window layout it had when it was closed. Off: it "
                                    "starts with the layout last saved while this was on, or the workspace last "
                                    "loaded.</p></body></html>"));
  QAction *arrangeButton = m_arrangeMenu->menuAction();
  arrangeButton->setIcon(QIcon::fromTheme("qtdmm-arrange"));
  arrangeButton->setToolTip(tr("Arrange the windows"));
  // on the right, next to the menu button: it arranges the whole window,
  // not the meter
  toolBarMenu->insertAction(action_Menu, arrangeButton);
  if (auto *button = qobject_cast<QToolButton *>(toolBarMenu->widgetForAction(arrangeButton)))
    button->setPopupMode(QToolButton::InstantPopup);
  return { loadWs, saveWs };
}

// Design: the colour designs of the window.
void MainWindow::createDesignMenu()
{
  // colour designs of the window (the LCD tint and the meter style are
  // settings of their own)
  m_designMenu = new QMenu(tr("D&esign"), this);
  auto *designGroup = new QActionGroup(this);
  for (auto [d, text] : { std::pair{Designs::System, tr("&System")}, std::pair{Designs::Silver, tr("&Light")},
                          std::pair{Designs::Dark, tr("&Dark")}, std::pair{Designs::BrushedMetal, tr("Light &Metal")},
                          std::pair{Designs::DarkMetal, tr("Dark Me&tal")} })
  {
    QAction *a = m_designMenu->addAction(text);
    a->setCheckable(true);
    a->setData(int(d));
    designGroup->addAction(a);
    connect(a, &QAction::triggered, this, [this, d = d] { setDesign(d); });
  }
}

// The readings dot, the connection and the info field, and what feeds them.
void MainWindow::createStatusBar()
{
  // the dot of the device in My devices blinks green while readings come
  // in and turns grey when they stop
  m_ledIdle = new QTimer(this);
  m_ledIdle->setSingleShot(true);
  m_ledIdle->setInterval(3000);
  connect(m_ledIdle, &QTimer::timeout, this, [this]
  {
    m_ledActive = false;
    updateLed();
  });
  connect(m_wid->controller(), &MeterController::reading, this, [this](const Reading &r)
  {
    if (r.id != 0)
      return;
    m_lastReadingAt = QDateTime::currentMSecsSinceEpoch();
    m_lowBattery = r.flags & SampleFlag::LowBattery;
    m_ledActive = true;
    m_ledBlink = !m_ledBlink;
    m_ledIdle->start();
    updateLed();
  });
  updateLed();

  m_error = new QLabel(statusBar());
  m_error->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(m_error, 20);
  m_error->setLineWidth(1);

  m_info = new QLabel(statusBar());
  m_info->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(m_info, 10);
  m_info->setLineWidth(1);

  // the active device, while there is more than one
  m_activeName = new QLabel(statusBar());
  m_activeName->setObjectName("activeDevice");
  m_activeName->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  m_activeName->setLineWidth(1);
  statusBar()->addWidget(m_activeName);
  m_activeName->hide();
  connect(m_mdi, &QMdiArea::subWindowActivated, this, [this](QMdiSubWindow *win)
  {
    if (win == m_displayWin)
      setActive(m_wid->controller());
    else if (FrontPanel *panel = panelOf(win))
      setActive(panel->controller());
  });
  connect(m_session, &DeviceSession::opened, this, [this] { setActive(m_active); });
  // the active one closed: the window's again
  connect(m_session, &DeviceSession::closed, this, [this] { setActive(m_active); });

  // the limit test's count: "PASS 812 · FAIL 23 (2.8 %)", reset with min/max
  m_limitCount = new QLabel(statusBar());
  m_limitCount->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  m_limitCount->setLineWidth(1);
  statusBar()->addPermanentWidget(m_limitCount);
  auto showCount = [this](const LimitTest::Count &count)
  {
    m_limitCount->setVisible(m_wid->limitTest().enabled);
    const QLocale locale;
    const QString share = count.total() > 0 ? locale.toString(100.0 * count.fail() / count.total(), 'f', 1) : "-";
    m_limitCount->setText(tr("PASS %1 · FAIL %2 (%3 %)").arg(count.pass).arg(count.fail()).arg(share));
    m_limitCount->setToolTip(tr("Readings judged since the limits were set or min/max was reset: "
                                "%1 too high, %2 too low").arg(count.high).arg(count.low));
  };
  connect(m_wid->controller(), &MeterController::limitCountChanged, this, showCount);
  showCount(m_wid->controller()->limitCount());

  // the toolbar on and off: the way back with the mouse, always there
  m_toolbarButton = new QToolButton(statusBar());
  m_toolbarButton->setObjectName("toolbarButton");
  m_toolbarButton->setAutoRaise(true);
  statusBar()->addPermanentWidget(m_toolbarButton);
  connect(m_toolbarButton, &QToolButton::clicked, m_toolbars, &QAction::trigger);
  connect(m_toolbars, &QAction::toggled, this, &MainWindow::updateToolbarButton);
  updateToolbarButton();

  // the SCPI server and HiSLIP are in the sidebar (Services), not here
  connect(m_wid, &InstanceWidget::servicesChanged, this, &MainWindow::updateServices);

  // messages such as the permission hint span several lines; the status bar
  // shows the first one and keeps the rest in the tooltip
  connect(m_wid, &InstanceWidget::error, this, [this](const QString &text)
  {
    const QString firstLine = text.section('\n', 0, 0);
    m_error->setText(firstLine);
    m_error->setToolTip(text.contains('\n') ? text : QString());
  });
  connect(m_wid, &InstanceWidget::info, m_info, &QLabel::setText);
  connect(m_wid, &InstanceWidget::useTextLabel, this, &MainWindow::setUseTextLabel);
  connect(m_wid, &InstanceWidget::iconSet, this, [](const QString &set)
          { Designs::setIconSet(Designs::iconSetFromName(set)); });
  connect(m_wid, &InstanceWidget::setConnect, this, &MainWindow::windowConnected);
  connect(m_wid, &InstanceWidget::connectDMM, this, &MainWindow::windowConnected);
  connect(m_wid, &InstanceWidget::showDisplay, this, &MainWindow::showDisplaySLOT);
}

// Applies the settings and puts the window where it was, or sizes it to the views shown.
void MainWindow::placeWindow()
{
  QRect winRect = m_wid->settingsDialog()->winRect();

  m_wid->applySLOT();
  // the layout of the windows; the dock state of versions before the MDI
  // window (MainWindow/state) is not taken over
  restoreWindows();

  setMinimumSize(19 * em(), 13 * em());
  // winRect() falls back to 500 x 350: only a stored width means a stored
  // geometry; a first start lets the window manager place the window
  const bool stored = m_wid->settings()->getInt("Position/width", 0) > 0;
  if (stored && m_wid->settingsDialog()->generalPage()->saveWindowPosition())
    move(winRect.x(), winRect.y());
  // the views shown at the start count as grown for; the table column gets
  // the width the window has for it
  for (QMdiSubWindow *w : { m_displayWin, m_meterWin, m_graphWin, m_readingsWin, m_poincareWin })
    if (!w->isHidden())
      m_grown.insert(w);
  if (m_panel->keysShown())
    m_grown.insert(m_panel);
  if (m_grown.contains(m_readingsWin) || m_grown.contains(m_poincareWin))
    m_arranger->setTableWidth(growthFor(m_readingsWin).width());
  if (stored && m_wid->settingsDialog()->generalPage()->saveWindowSize())
  {
    resize(winRect.width(), winRect.height());
    m_userSized = m_wid->settings()->getBool("Windows/user-sized", false);
  }
  else
  {
    // from the content: an LCD, plus what each shown view adds
    QSize start(28 * em(), 22 * em());
    if (m_grown.contains(m_displayWin) && m_grown.contains(m_meterWin))
      start += growthFor(m_meterWin);
    for (QMdiSubWindow *w : { m_graphWin, m_readingsWin, m_poincareWin })
      if (m_grown.contains(w))
        start += growthFor(w);
    if (m_panel->keysShown())
      start += QSize(0, 2 * em());
    const QRect av = screen()->availableGeometry();
    resize(qMin(start.width(), int(av.width() * 0.84)), qMin(start.height(), int(av.height() * 0.84)));
  }
  m_expectSize = size();
}

void MainWindow::updateEmptyStart()
{
  const bool empty = !m_wid->dmmConfigured();
  if (empty)
  {
    m_emptyStart->setGeometry(m_mdi->rect());
    m_emptyStart->raise();
  }
  m_emptyStart->setVisible(empty);
}

void MainWindow::addDevice()
{
  AddDeviceDlg dlg(m_wid->devices(), this);
  dlg.setPorts(Transport::availablePorts() + m_wid->settings()->customPorts());
  dlg.setSigrokExe(m_wid->settings()->getString("Port settings/sigrok_exe", "sigrok-cli"));
  dlg.setCalcInputs(m_session->calcInputs());
  dlg.setPlacesInUse(placesInUse());
  if (m_wid->dmmConfigured())
    dlg.setCurrentDevice(m_wid->dmmTitle());
  else
    dlg.setTargetChoice(false);   // an empty window: the device goes into it
  if (dlg.exec() != QDialog::Accepted)
    return;
  QVariantMap keys = dlg.keys();
  keys.insert("Port settings/device", SerialDevice::stableDevice(keys.value("Port settings/device").toString()));
  const int before = int(m_wid->devices()->list().size());
  // a find that is one of My devices changes that entry: no second one
  QString id = dlg.knownDevice();
  if (id.isEmpty())
    id = m_wid->devices()->add(dlg.name(), keys);
  else
  {
    m_wid->devices()->update(id, keys);
    m_wid->devices()->rename(id, dlg.name());
  }
  if (dlg.target() == AddDeviceDlg::Beside)
    openBeside(id);
  else
    m_wid->switchDevice(id);
  // the second device: from now on there is a choice
  if (DeviceSidebar::opensAfterAdd(before, int(m_wid->devices()->list().size())))
  {
    showSidebar(true);
    m_wid->settings()->setBool("Windows/sidebar", true);
  }
}

QMap<QString, const MeterController *> MainWindow::placeOwners() const
{
  QMap<QString, const MeterController *> owners;
  auto add = [&owners](const QVariantMap &keys, const MeterController *ctl)
  {
    // a formula is no port: any number of meters may compute it
    const QString place = DeviceLibrary::place(keys);
    if (!place.isEmpty() && !place.startsWith(QLatin1String("calc ")))
      owners.insert(place, ctl);
  };
  add(m_wid->settings()->meterKeys(), m_wid->controller());
  for (const MeterController *ctl : m_session->controllers())
    if (ctl != m_wid->controller())
      if (const std::optional<MyDevice> device = m_wid->devices()->find(ctl->deviceId()))
        add(device->keys, ctl);
  return owners;
}

QMap<QString, QString> MainWindow::placesInUse() const
{
  QMap<QString, QString> inUse;
  const QMap<QString, const MeterController *> owners = placeOwners();
  for (auto it = owners.constBegin(); it != owners.constEnd(); ++it)
    inUse.insert(it.key(), it.value() == m_wid->controller() ? tr("In use here")
                                                             : tr("Open beside: %1").arg(it.value()->deviceName()));
  return inUse;
}

// "QtDMM: UNI-T UT61E", with the --config-id when one is given
void MainWindow::updateWindowTitle()
{
  QString title = APP_NAME;
  if (!m_configId.isEmpty())
    title += QString(" [%1]").arg(m_configId);
  setWindowTitle(QString("%1: %2").arg(title, m_wid->dmmTitle()));
}

void MainWindow::setConsoleLogging(bool on)
{
  if (on)
    QLoggingCategory::setFilterRules("qtdmm.hid.debug=true\nqtdmm.ble.debug=true\nqtdmm.mdns.debug=true\nqtdmm.scpi.debug=true");
  m_wid->setConsoleLogging(on);
}

void MainWindow::setUseTextLabel(bool on)
{
  Qt::ToolButtonStyle Style = Qt::ToolButtonTextUnderIcon;
  if (!on)
    Style = Qt::ToolButtonIconOnly;
  toolBarDMM->setToolButtonStyle(Style);
  toolBarRecorder->setToolButtonStyle(Style);
  toolBarFile->setToolButtonStyle(Style);
  toolBarMenu->setToolButtonStyle(Style);
}

void MainWindow::createActions()
{
  // Connect and Reset act on the active device
  connect(action_Connect, &QAction::triggered, this, [this](bool on)
  {
    MeterController *ctl = activeController();
    if (ctl == m_wid->controller())
      m_wid->connectSLOT(on);
    else if (!ctl->connectMeter(on))
      action_Connect->setChecked(false);
  });
  // toggled, not triggered: a connection made by the program (Add device, the
  // sidebar, the start, SCPI) sets the check mark only - Record stayed off.
  // Record follows the window's meter only
  connect(action_Connect, &QAction::toggled, this, [this](bool on)
  {
    if (!m_activeOther)
      connectSLOT(on);
  });
  connect(action_Reset, &QAction::triggered, this, [this] { activeController()->resetMinMax(); });
  connect(action_Start, &QAction::triggered, this, &MainWindow::startSLOT);
  // one button for both: a grey dot starts, the red square stops
  connect(action_Record, &QAction::triggered, this, &MainWindow::toggleRecordingSLOT);
  connect(action_Live, &QAction::triggered, this, [this]
  {
    m_wid->liveSLOT();
    updateRecorderActions();   // pressed again when the question was cancelled
  });
  // the graph's context menu starts and stops like the button
  connect(m_wid->graph(), &GraphWidget::recordRequested, this, [this](bool start)
  {
    if (!start)
      action_Stop->trigger();
    else if (action_Start->isEnabled())
      action_Start->trigger();
  });
  connect(action_Stop, &QAction::triggered, m_wid, &InstanceWidget::stopSLOT);
  connect(action_Clear, &QAction::triggered, m_wid, &InstanceWidget::clearSLOT);
  connect(action_Print, &QAction::triggered, m_wid, &InstanceWidget::printSLOT);
  connect(action_Import, &QAction::triggered, m_wid, &InstanceWidget::importSLOT);
  connect(action_Export, &QAction::triggered, m_wid, &InstanceWidget::exportSLOT);
  connect(action_Configure, &QAction::triggered, m_wid, &InstanceWidget::configSLOT);
  // Shift+F8: the settings of the device in use, as its context menu in the sidebar
  connect(action_ConfigureDMM, &QAction::triggered, this, [this]
  {
    if (m_activeOther && m_active)
      deviceSettings(m_active->deviceId(), true);
    else if (m_wid->devices()->find(m_wid->currentDevice()))
      deviceSettings(m_wid->currentDevice(), true);
    else
      addDevice();
  });
  connect(actionConfigureRecorder, &QAction::triggered, m_wid, &InstanceWidget::configRecorderSLOT);
  connect(action_Quit, &QAction::triggered, this, &MainWindow::storeDisplaySLOT);
  connect(action_Quit, &QAction::triggered, m_wid, &InstanceWidget::quitSLOT);
  connect(action_Direct_help, &QAction::triggered, m_wid, &InstanceWidget::helpSLOT);


}

// Actions that live only in the popup menu are not attached to any widget,
// so their shortcuts would be dead - adding them to the window fixes that.
void MainWindow::createExtraActions()
{
  m_fullScreen = new QAction(tr("&Full screen"), this);
  m_fullScreen->setObjectName("action_FullScreen");
  m_fullScreen->setCheckable(true);
  m_fullScreen->setShortcut(QKeySequence("F11"));
  m_fullScreen->setWhatsThis(tr("<html><head/><body><p><span style=\" font-weight:600;\">Full screen</span></p>"
                                "<p>Use the whole screen for the instruments, e.g. on a lab monitor. The same key "
                                "again returns to the normal window.</p></body></html>"));
  connect(m_fullScreen, &QAction::toggled, this, &MainWindow::setFullScreen);

  m_toolbars = new QAction(tr("Hide &toolbar"), this);
  m_toolbars->setObjectName("action_Toolbars");
  m_toolbars->setCheckable(true);
  m_toolbars->setShortcut(QKeySequence("Ctrl+M"));
  m_toolbars->setWhatsThis(tr("<html><head/><body><p><span style=\" font-weight:600;\">Hide toolbar</span></p>"
                              "<p>More room for the instruments. The menu stays at hand with F7, the same key "
                              "again brings the toolbar back.</p></body></html>"));
  connect(m_toolbars, &QAction::toggled, this, &MainWindow::setToolbarsHidden);
  // hidden by hand (not by a workspace): say how it comes back, a pressed Ctrl+M is easily missed
  connect(m_toolbars, &QAction::triggered, this, [this](bool hidden)
  {
    if (hidden)
      QTimer::singleShot(0, this, &MainWindow::toolbarHiddenHint);
  });

  m_zoomIn = new QAction(tr("Zoom &in"), this);
  m_zoomIn->setObjectName("action_ZoomIn");
  m_zoomIn->setShortcuts({QKeySequence::ZoomIn, QKeySequence("Ctrl+=")});
  connect(m_zoomIn, &QAction::triggered, m_wid->graph(), &GraphWidget::zoomInSLOT);
  m_zoomOut = new QAction(tr("Zoom &out"), this);
  m_zoomOut->setObjectName("action_ZoomOut");
  m_zoomOut->setShortcut(QKeySequence::ZoomOut);
  connect(m_zoomOut, &QAction::triggered, m_wid->graph(), &GraphWidget::zoomOutSLOT);
  m_zoomFit = new QAction(tr("Show &whole recording"), this);
  m_zoomFit->setObjectName("action_ZoomFit");
  m_zoomFit->setShortcut(QKeySequence("Ctrl+0"));
  connect(m_zoomFit, &QAction::triggered, m_wid->graph(), &GraphWidget::zoomFitSLOT);
  m_copyImage = new QAction(tr("Cop&y graph image"), this);
  m_copyImage->setObjectName("action_CopyImage");
  m_copyImage->setShortcut(QKeySequence("Ctrl+Shift+C"));
  m_copyImage->setWhatsThis(tr("<html><head/><body><p><span style=\" font-weight:600;\">Copy graph image</span></p>"
                               "<p>Puts a picture of the graph on the clipboard, ready to paste into a "
                               "report or a chat.</p></body></html>"));
  connect(m_copyImage, &QAction::triggered, m_wid->graph(), &GraphWidget::copyImageSLOT);

  // Space (Record) toggles the recorder; Start and Stop have neither a
  // button nor a key of their own any more
  addActions({action_Configure, action_ConfigureDMM, action_Direct_help, action_Help, action_Quit,
              m_displayAction, m_meterAction, m_readingsAction, m_poincareAction, m_titleBars,
              m_fullScreen, m_toolbars, m_zoomIn, m_zoomOut, m_zoomFit, m_copyImage});
}

// Every command that may have a key, in the groups of the page Keyboard;
// the keys the user set (shortcuts.conf, all instances) replace the ones
// given above, and the tooltips name them
void MainWindow::createShortcuts(const QList<QAction *> &workspace)
{
  m_shortcuts = new ShortcutRegistry(this, QDir(m_wid->settings()->configDir()).filePath("shortcuts.conf"), this);
  const QString meter = tr("Meter"), recording = tr("Recording and graph"), windows = tr("Windows"),
                help = tr("Settings and help");
  for (QAction *a : { action_Connect, action_Reset, action_ConfigureDMM, m_addDeviceAction })
    m_shortcuts->add(a, meter);
  m_shortcuts->add(action_Record, recording, tr("Record / Stop"));
  for (QAction *a : { action_Live, action_Clear, m_zoomIn, m_zoomOut, m_zoomFit, m_copyImage, action_Export,
                      action_Import, action_Print })
    m_shortcuts->add(a, recording);
  for (QAction *a : { m_sidebarAction, m_displayAction, m_meterAction, action_Graph, m_readingsAction,
                      m_poincareAction, m_titleBars, m_toolbars, m_fullScreen })
    m_shortcuts->add(a, windows);
  for (QAction *a : workspace)
    m_shortcuts->add(a, windows);
  m_shortcuts->add(action_Configure, help, tr("Settings"));
  m_shortcuts->add(actionConfigureRecorder, help, tr("Recording settings"));
  for (QAction *a : { action_Help, action_Direct_help, action_About, action_Menu, action_Quit })
    m_shortcuts->add(a, help);
  m_shortcuts->load();
  // Record names its key in a tooltip of its own
  connect(m_shortcuts, &ShortcutRegistry::changed, this, &MainWindow::updateRecorderActions);
  m_wid->settingsDialog()->keyboardPage()->setRegistry(m_shortcuts);
}

void MainWindow::toggleRecordingSLOT()
{
  if (m_running)
    action_Stop->trigger();
  else if (action_Start->isEnabled())
    action_Start->trigger();
}

void MainWindow::setFullScreen(bool on)
{
  if (on)
    showFullScreen();
  else
    showNormal();
}

void MainWindow::setToolbarsHidden(bool hide)
{
  for (QToolBar *bar : { toolBarDMM, toolBarRecorder, toolBarFile, toolBarMenu })
    bar->setVisible(!hide);
}

void MainWindow::toolbarHiddenHint()
{
  Settings *settings = m_wid->settings();
  if (!m_toolbars->isChecked() || !settings->getBool("Windows/toolbar-hint", true))
    return;
  const QString key = m_toolbars->shortcut().toString(QKeySequence::NativeText);
  const QString menuKey = action_Menu->shortcut().toString(QKeySequence::NativeText);
  QString text = key.isEmpty() ? tr("The toolbar is hidden. The arrow at the right end of the status line "
                                    "brings it back.")
                               : tr("The toolbar is hidden. %1 brings it back, and so does the arrow at the right "
                                    "end of the status line.").arg(key);
  if (!menuKey.isEmpty())
    text += ' ' + tr("The menu opens with %1.").arg(menuKey);
  QMessageBox box(QMessageBox::Information, tr("Hide toolbar"), text, QMessageBox::Ok, this);
  auto *again = new QCheckBox(tr("&Do not show this again"), &box);
  box.setCheckBox(again);
  box.exec();
  if (again->isChecked())
  {
    settings->setBool("Windows/toolbar-hint", false);
    settings->save();   // set only stages it, get reads the file
  }
}

void MainWindow::startSLOT()
{
  // a recording viewed and not saved yet: the new one clears it
  if (!m_wid->confirmRecording())
    return;
  if (!m_wid->askRecordingLength())
    return;
  // the meters open beside it follow (DeviceSession)
  m_wid->startSLOT();
}

void MainWindow::runningSLOT(bool on)
{
  m_running = on;
  if (on)
    action_Graph->setChecked(true);   // a recording wants to be seen

  action_Start->setEnabled(!on);
  action_Stop->setEnabled(on);
  action_Print->setEnabled(!on);
  action_Export->setEnabled(!on);
  action_Import->setEnabled(!on);
  updateRecorderActions();
}

void MainWindow::connectSLOT(bool on)
{
  action_Start->setEnabled(on);
  action_Stop->setEnabled(on && m_running);
  // whether it records is runningSLOT()'s: an alarm's program has the port
  // a while and the recording runs on
  updateRecorderActions();
}

void MainWindow::updateRecorderActions()
{
  const RecordingStore::State state = m_wid->controller()->recorder()->state();
  const bool recording = state == RecordingStore::Record;
  action_Record->setEnabled(recording ? action_Stop->isEnabled() : action_Start->isEnabled());
  action_Record->setIcon(QIcon(new RecordIconEngine(recording)));
  action_Record->setText(recording ? tr("S&top") : tr("&Record"));
  const QString key = action_Record->shortcut().toString(QKeySequence::NativeText);
  action_Record->setToolTip(QString("%1 (%2)").arg(recording ? tr("Stop recording") : tr("Start recording"), key));
  action_Live->setEnabled(!recording);
  action_Live->setChecked(state == RecordingStore::Live);
}

void MainWindow::on_action_Help_triggered()
{
  if (!m_helpDlg)
    m_helpDlg = new HelpDlg(m_wid->settings(), this);
  m_helpDlg->show();
  m_helpDlg->raise();
  m_helpDlg->activateWindow();
}

void MainWindow::on_action_About_triggered()
{
  QMessageBox about(this);
  about.setWindowTitle(tr("About QtDMM"));
  about.setIconPixmap(QPixmap(":/Symbols/qtdmm_64.png"));
  about.setTextFormat(Qt::RichText);
  about.setText(tr("<h2>QtDMM %1</h2>"
                   "<p>A readout and transient recorder for digital multimeters.</p>"
                   "<p>Built with <b>Qt</b> %2. Licensed under the <b>GNU GPL 3 or later</b> "
                   "(versions before 0.9.0 under GPL 2 or later).</p>"
                   "<p>0.9.5 onwards: tuxmaster and contributors, see the AUTHORS file.<br>"
                   "0.9.3 and before: &copy; 2001-2016 M. Toussaint "
                   "&lt;<a href='mailto:qtdmm@mtoussaint.de'>qtdmm@mtoussaint.de</a>&gt;</p>"
                   "<p>Website: <a href='https://qtdmm.de'>qtdmm.de</a> &middot; "
                   "Contact: <a href='mailto:hello@qtdmm.de'>hello@qtdmm.de</a><br>"
                   "Source and bug reports: <a href='https://github.com/qtdmm/QtDMM'>github.com/qtdmm/QtDMM</a><br>"
                   "Symbols from the <b>Oxygen</b> and <b>Breeze</b> icon themes of the KDE community (LGPL 3); "
                   "QtDMM's own symbols are drawn in its style.</p>")
                .arg(APP_VERSION).arg(qVersion()));

  // The device list used to be pasted in here as a table; it lives in the
  // handbook now, where it is readable, searchable on the web and generated
  // from the decoders instead of maintained by hand.
  QPushButton *devices = about.addButton(tr("Supported devices..."), QMessageBox::ActionRole);
  about.addButton(QMessageBox::Close);
  about.setDefaultButton(QMessageBox::Close);
  about.exec();

  if (about.clickedButton() == devices)
  {
    on_action_Help_triggered();
    m_helpDlg->showPage("supported-devices.md");
  }
}

void MainWindow::on_action_Menu_triggered()
{
  if (!m_menu)
  {
    m_menu = new QMenu(this);
    m_menu->addAction(action_Configure);
    m_menu->addAction(m_sidebarAction);
    m_menu->addAction(action_Graph);
    m_menu->addAction(m_displayAction);
    m_menu->addAction(m_meterAction);
    m_menu->addAction(m_readingsAction);
    m_menu->addAction(m_poincareAction);
    m_menu->addMenu(m_arrangeMenu);
    m_menu->addMenu(m_designMenu);
    m_menu->addAction(m_toolbars);
    m_menu->addAction(m_fullScreen);
    m_menu->addSeparator();
    m_menu->addAction(m_zoomIn);
    m_menu->addAction(m_zoomOut);
    m_menu->addAction(m_zoomFit);
    m_menu->addAction(m_copyImage);
    m_menu->addSeparator();
    m_menu->addAction(action_Help);
    m_menu->addAction(action_Direct_help);
    m_menu->addAction(action_About);
    m_menu->addSeparator();
    m_menu->addAction(action_Quit);
  }

  // below the menu button; with the toolbar hidden at the top right of the window
  QWidget* widget = this->toolBarMenu->widgetForAction(this->action_Menu);
  if (widget && widget->isVisible())
  {
    m_menu->popup(widget->mapToGlobal(
      QPoint( widget->width() - m_menu->sizeHint().width(), widget->height())
    ));
  }
  else
    m_menu->popup(mapToGlobal(QPoint(width() - m_menu->sizeHint().width(), 0)));
}


void MainWindow::closeEvent(QCloseEvent *ev)
{
  storeDisplaySLOT();
  // the settings dialog saves "Show display" as well: without auto-save it
  // keeps the start layout's
  if (!m_autoSaveLayout->isChecked())
    m_wid->settingsDialog()->guiPage()->setShowDisplay(m_startDisplay);
  // dock layout (meter position, floating state, size) and toolbar layout
  saveWindows();
  m_wid->settings()->setInt("ReadingLog/max-rows", m_readings->maxRows());

  if (m_wid->closeWin())
    ev->accept();
  else
    ev->ignore();
}

// ---------------------------------------------------------------- MDI windows

QMdiSubWindow *MainWindow::addView(QWidget *view, const QString &title, int role, const QString &name)
{
  QMdiSubWindow *win = m_mdi->addSubWindow(view);
  // closing a window hides it; its action brings it back
  win->setAttribute(Qt::WA_DeleteOnClose, false);
  win->setWindowTitle(title);
  win->setWindowIcon(windowIcon());
  win->setObjectName(name);
  win->installEventFilter(this);
  m_arranger->addWindow(win, static_cast<MdiArranger::Role>(role));
  return win;
}

QAction *MainWindow::windowAction(QMdiSubWindow *win, const QString &text, const char *shortcut,
                               const char *icon, const QString &whatsThis)
{
  auto *action = new QAction(QIcon::fromTheme(icon), text, this);
  action->setShortcut(QKeySequence(shortcut));
  action->setWhatsThis(whatsThis);
  bindWindowAction(action, win);
  return action;
}

void MainWindow::bindWindowAction(QAction *action, QMdiSubWindow *win)
{
  action->setCheckable(true);
  action->setProperty("window", QVariant::fromValue<QObject *>(win));
  connect(action, &QAction::toggled, win, [this, win](bool on)
  {
    // the view inside may have been closed with the window before (Qt
    // closes it along): show it again, or the window stays empty
    if (on && win->widget())
      win->widget()->show();
    win->setVisible(on);
    if (on)
    {
      m_mdi->setActiveSubWindow(win);
      if (!m_restoring)
        growFor(win);
    }
  });
}

// A window was closed with its title bar button, or shown: its action follows.
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
  if (watched == m_mdi && event->type() == QEvent::Resize)
    m_emptyStart->setGeometry(m_mdi->rect());
  // closing a window (its title bar button, Ctrl+F4) only hides it: a real
  // close would close the view inside as well, and it would not come back
  if (event->type() == QEvent::Close)
    if (auto *win = qobject_cast<QMdiSubWindow *>(watched))
    {
      win->hide();
      event->ignore();
      return true;
    }
  // minimizing the main window hides its children spontaneously; they are
  // not closed, so only explicit show/hide counts (isHidden() stays false)
  if ((event->type() == QEvent::Show || event->type() == QEvent::Hide) && !event->spontaneous())
    for (QAction *a : { m_displayAction, m_meterAction, m_readingsAction, m_poincareAction, action_Graph })
      if (a && a->property("window").value<QObject *>() == watched)
      {
        const bool visible = !static_cast<QWidget *>(watched)->isHidden();
        if (a->isChecked() != visible && !m_restoring)
        {
          QSignalBlocker block(a);
          a->setChecked(visible);
          // the settings dialog keeps its own "Show display": update it, or
          // the next OK there would bring the display back
          if (a == m_displayAction)
            storeDisplaySLOT();
        }
      }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::windowMenu(QMdiSubWindow *win, const QPoint &globalPos)
{
  QMenu menu(this);
  QAction *hide = menu.addAction(tr("&Hide window"));
  FrontPanel *panel = panelOf(win);
  if (panel && panel->keysSupported())
  {
    QAction *hideControls = menu.addAction(tr("Hide &controls"));
    hideControls->setCheckable(true);
    hideControls->setChecked(panel->isFolded());
    connect(hideControls, &QAction::toggled, this, [this, panel](bool on)
    {
      if (panel == m_panel)
        setControlsFolded(on);
      else
      {
        panel->setFolded(on);
        updateControls();
      }
    });
  }
  if (panel)
  {
    QMenu *lcd = menu.addMenu(tr("&LCD colours"));
    const std::pair<LcdWidget::LcdVariant, QString> variants[] = {
      { LcdWidget::Classic, tr("&Classic") }, { LcdWidget::BacklightBlue, tr("&Backlight blue") },
      { LcdWidget::Amber, tr("&Amber") }, { LcdWidget::HighContrast, tr("&High contrast") },
      { LcdWidget::Custom, tr("C&ustom (from the settings)") } };
    for (const auto &[v, text] : variants)
    {
      if (v == LcdWidget::Custom)
        lcd->addSeparator();
      QAction *a = lcd->addAction(text);
      a->setCheckable(true);
      a->setChecked(m_panel->display()->lcdVariant() == v);
      // one look for all panels
      connect(a, &QAction::triggered, this, [this, v = v]
      {
        m_wid->setLcdVariant(v);
        for (QMdiSubWindow *w : std::as_const(m_panelWins))
          applyLcdLook(panelOf(w));
      });
    }
  }
  if (win == m_displayWin)
  {
    menu.addSeparator();
    // the limit test: on and off here, the limits in the dialog; on without
    // limits set yet asks for them
    QAction *test = menu.addAction(tr("PASS/FAIL &test"));
    test->setCheckable(true);
    test->setChecked(m_wid->limitTest().enabled);
    connect(test, &QAction::toggled, this, [this](bool on)
    {
      LimitTest t = m_wid->limitTest();
      t.enabled = on;
      if (on && t.low() == t.high())
        editLimits(true);
      else
        m_wid->setLimitTest(t);
    });
    connect(menu.addAction(tr("L&imits...")), &QAction::triggered, this, [this] { editLimits(false); });
  }
  if (win == m_meterWin)
  {
    QMenu *style = menu.addMenu(tr("Meter &style"));
    const std::pair<int, QString> styles[] = { { 0, tr("&Dark studio") }, { 1, tr("Classic &ivory") } };
    for (const auto &[s, text] : styles)
    {
      QAction *a = style->addAction(text);
      a->setCheckable(true);
      a->setChecked(m_wid->settingsDialog()->guiPage()->meterStyle() == s);
      connect(a, &QAction::triggered, this, [this, s = s] { m_wid->setMeterStyle(s); });
    }
  }
  if (win == m_poincareWin)
  {
    menu.addSeparator();
    m_poincare->addMenuActions(&menu);
  }
  if (menu.exec(globalPos) == hide)
    win->hide();
}

void MainWindow::editLimits(bool enable)
{
  const MeterController *ctl = m_wid->controller();
  const Reading &r = ctl->lastReading();
  const double reading = r.overload || r.noNumber() || r.text.isEmpty() ? std::nan("") : r.value;
  LimitTest t = m_wid->limitTest();
  if (enable)
    t.enabled = true;
  LimitDlg dlg(t, ctl->baseUnit(), reading, this);
  if (dlg.exec() == QDialog::Accepted)
    m_wid->setLimitTest(dlg.limitTest());
}

void MainWindow::showSidebar(bool show)
{
  m_sidebarDock->setVisible(show);
  m_sidebarAction->setChecked(show);
  updateOpenDevices();
}

void MainWindow::updateServices()
{
  QList<DeviceSidebar::Service> rows;
  for (const ServiceState &state : m_wid->controller()->services())
  {
    DeviceSidebar::Service row;
    row.id = state.id;
    row.name = state.name;
    row.running = state.running;
    row.active = state.clients > 0;
    row.address = state.address;
    row.failed = !state.running && !state.error.isEmpty();
    row.value = state.running ? QString::number(state.port) : row.failed ? tr("failed") : tr("off");
    const QString clients = state.clients == 1 ? tr("1 client") : tr("%1 clients").arg(state.clients);
    if (row.failed)
      row.tooltip = tr("%1 could not start: %2. Double-click for its settings.").arg(state.name, state.error);
    else if (!state.running)
      row.tooltip = tr("%1 is off. Double-click for its settings.").arg(state.name);
    else if (state.id == QLatin1String("hislip"))
      row.tooltip = tr("HiSLIP for VISA (NI-VISA, LabVIEW, PyVISA): %1, %2 connected.").arg(state.address, clients);
    else if (state.id == QLatin1String("vxi11"))
      row.tooltip = tr("VXI-11 for VISA and lxi-tools: %1, %2 connected.").arg(state.address, clients);
    else
      row.tooltip = tr("SCPI server (raw socket) on %1, %2 connected.").arg(state.address, clients);
    rows << row;
  }
  m_sidebar->setServices(rows);
}

// A reading as the sidebar shows it: "1.5 V DC", "OL", "" without a number.
static QString readingText(const Reading &r)
{
  if (r.overload)
    return QStringLiteral("OL");
  if (r.noNumber())
    return QString();
  QString prefix;
  const QString value = SiPrefix::format(r.value, r.baseUnit, &prefix);
  return QString("%1 %2%3 %4").arg(value.left(8), prefix, r.baseUnit, couplingText(r.port.defining)).trimmed();
}

QString MainWindow::sourceName(const QString &source) const
{
  const QString id = source.startsWith("device:") ? source.mid(7) : QString();
  if (const std::optional<MyDevice> device = m_wid->devices()->find(id))
    return device->name;
  return source == m_wid->controller()->source() ? m_wid->dmmTitle() : source;
}

bool MainWindow::tableShows(const ValueRef &value) const
{
  const ValueRef ref = m_wid->ownMeter(value) ? ValueRef(value.id) : value;
  const ReadingsModel *log = m_readings->log();
  return (ref.source.isEmpty() || m_tableOthers.contains(ref.source)) && !log->hiddenValues().contains(ref);
}

void MainWindow::setTableShown(const ValueRef &value, bool shown)
{
  const ValueRef ref = m_wid->ownMeter(value) ? ValueRef(value.id) : value;
  ReadingsModel *log = m_readings->log();
  if (ref.source.isEmpty())
  {
    log->setValueHidden(ref, !shown);
    return;
  }
  const MeterController *ctl = m_session->controller(ref.source);
  const int count = ctl ? ctl->valueCount() : kMaxValues;
  if (shown && !m_tableOthers.contains(ref.source))
  {
    // another meter comes in with this value only
    for (int id = 0; id < count; ++id)
      log->setValueHidden(ValueRef(id, ref.source), id != ref.id);
    m_tableOthers << ref.source;
    applyTableSources();
    return;
  }
  log->setValueHidden(ref, !shown);
  // none of its values left: it goes out of the table
  bool any = false;
  for (int id = 0; id < count; ++id)
    any = any || !log->hiddenValues().contains(ValueRef(id, ref.source));
  if (!any)
  {
    for (int id = 0; id < kMaxValues; ++id)
      log->setValueHidden(ValueRef(id, ref.source), false);
    m_tableOthers.removeAll(ref.source);
    applyTableSources();
  }
}

void MainWindow::applyTableSources()
{
  ReadingsModel *log = m_readings->log();
  const int rows = log->maxRows();
  const QString own = m_wid->controller()->source();
  log->setDevices(!m_tableOthers.isEmpty(), own, [this](const QString &source) { return sourceName(source); });
  if (m_tableOthers.isEmpty())
  {
    // the window's recorder's readings, as always
    log->setStore(m_wid->controller()->recorder());
    connectTableSources();
    return;
  }
  // the rows the meters' recorders hold, merged by time, into the table's own store
  QList<LoggedReading> merged;
  for (const MeterController *ctl : m_session->controllers())
    if (ctl == m_wid->controller() || m_tableOthers.contains(ctl->source()))
      for (int i = 0; i < ctl->recorder()->readingCount(); ++i)
      {
        LoggedReading row = ctl->recorder()->readingAt(i);
        row.source = ctl->source();
        merged << row;
      }
  std::stable_sort(merged.begin(), merged.end(),
                   [](const LoggedReading &a, const LoggedReading &b) { return a.when < b.when; });
  log->setStore(nullptr);
  log->setMaxRows(rows);
  for (const LoggedReading &row : std::as_const(merged).mid(qMax(qsizetype(0), merged.size() - rows)))
    log->store()->logRow(row);
  connectTableSources();
}

void MainWindow::connectTableSources()
{
  for (const QMetaObject::Connection &c : std::as_const(m_tableLinks))
    disconnect(c);
  m_tableLinks.clear();
  if (m_tableOthers.isEmpty())
    return;   // the window's recorder feeds the table
  ReadingsModel *log = m_readings->log();
  for (MeterController *ctl : m_session->controllers())
    if (ctl == m_wid->controller() || m_tableOthers.contains(ctl->source()))
      m_tableLinks << connect(ctl, &MeterController::reading, log, &ReadingsModel::appendReading);
}

DeviceSidebar::Value MainWindow::otherValueRow(const MeterController *ctl, int id) const
{
  const Reading r = ctl->lastReading(id);
  DeviceSidebar::Value row;
  row.id = id;
  if (r.msecs <= 0)
    row.name = tr("Value %1").arg(id + 1);
  else
  {
    row.name = r.name.isEmpty() ? RecordingStore::describe(r.port, r.baseUnit) : r.name;
    row.text = readingText(r);
    const QString unit = SiPrefix::displayText(r.baseUnit);
    if (r.name.isEmpty() && !unit.isEmpty())
      row.shortName = QString("%1 %2").arg(unit, couplingText(r.port.defining)).trimmed();
  }
  row.color = m_wid->graph()->otherValueColor(ValueRef(id, ctl->source()));
  return row;
}

void MainWindow::updateOpenDevices()
{
  // the active one connected or disconnected elsewhere (SCPI, an error)
  if (m_activeOther && m_active && action_Connect->isChecked() != m_active->dmm()->isWanted())
    action_Connect->setChecked(m_active->dmm()->isWanted());
  if (!m_sidebarDock->isVisible())
    return;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const QString own = m_wid->controller()->source();
  QList<DeviceSidebar::OpenDevice> rows;
  for (const MeterController *ctl : m_session->controllers())
  {
    DeviceSidebar::OpenDevice row;
    const bool here = ctl == m_wid->controller();
    row.id = here ? own : ctl->deviceId();
    // the window's by its name in My devices too, else its model
    row.name = here && ctl->deviceName().isEmpty() ? m_wid->dmmTitle() : ctl->deviceName();
    row.current = ctl == activeController();
    row.connected = here ? (m_activeOther ? m_windowConnect : action_Connect->isChecked()) : ctl->dmm()->isWanted();
    const Reading &r = ctl->lastReading();
    if (r.msecs > 0)
    {
      row.value = readingText(r);
      row.active = now - r.msecs < 3000;
    }
    // its values, with what the graph shows
    if (here)
      for (int value = 0; value < m_wid->numValues(); ++value)
      {
        const QString text = m_wid->valueText(value);
        const bool drawn = m_wid->graph()->valueDrawn(value);
        row.values << DeviceSidebar::Value{ value, m_wid->valueName(value), text,
                                            drawn ? m_wid->graph()->valueColor(value) : QColor(),
                                            m_wid->valueShortName(value) };
      }
    else
      for (int value = 0; value < ctl->valueCount(); ++value)
        row.values << otherValueRow(ctl, value);
    rows << row;
  }
  m_sidebar->setOpenDevices(rows, own);
}

void MainWindow::valueMenu(const ValueRef &value, const QPoint &globalPos)
{
  QMenu menu(this);
  fillValueMenu(&menu, value);
  menu.exec(globalPos);
  updateOpenDevices();
}

void MainWindow::fillValueMenu(QMenu *menu, const ValueRef &value)
{
  ValueMenu::Target target;
  target.wid = m_wid;
  target.panel = [this](const QString &source) { return panelOf(m_panelWins.value(source)); };
  target.tableShown = [this](const ValueRef &v) { return tableShows(v); };
  target.setTableShown = [this](const ValueRef &v, bool on) { setTableShown(v, on); };
  target.show = [this](ValueMenu::View view, const QString &source)
  {
    QMdiSubWindow *panel = m_panelWins.value(source);
    if (view == ValueMenu::View::Lcd && panel)
    {
      panel->show();
      m_mdi->setActiveSubWindow(panel);
      m_arranger->arrange();
      return;
    }
    QAction *action = view == ValueMenu::View::Lcd     ? m_displayAction
                    : view == ValueMenu::View::Meter   ? m_meterAction
                    : view == ValueMenu::View::Poincare ? m_poincareAction
                    : view == ValueMenu::View::Table   ? m_readingsAction
                                                        : action_Graph;
    action->setChecked(true);
  };
  ValueMenu::fill(menu, target, value);
}

void MainWindow::deviceSettings(const QString &id, bool settingsTab)
{
  const std::optional<MyDevice> device = m_wid->devices()->find(id);
  if (!device)
    return;
  DeviceSettingsDlg dlg(device->name, this);
  dlg.settings()->setPorts(Transport::availablePorts() + m_wid->settings()->customPorts());
  dlg.settings()->setSigrokExe(m_wid->settings()->getString("Port settings/sigrok_exe", "sigrok-cli"));
  dlg.settings()->setCalcInputs(m_session->calcInputs());
  dlg.load(device->keys);
  dlg.setStatus([this, id] { return deviceStatus(id); });
  if (settingsTab)
    dlg.showTab(DeviceSettingsDlg::SettingsTab);
  // the help beside the dialog: a child of it, the dialog blocks the others
  connect(&dlg, &DeviceSettingsDlg::helpRequested, &dlg, [this, &dlg](const QString &page)
  {
    auto *help = new HelpDlg(m_wid->settings(), &dlg);
    help->setAttribute(Qt::WA_DeleteOnClose);
    help->showPage(page);
    help->show();
  });
  if (dlg.exec() != QDialog::Accepted)
    return;
  QVariantMap keys = dlg.keys();
  keys.insert("Port settings/device", SerialDevice::stableDevice(keys.value("Port settings/device").toString()));
  if (DeviceLibrary::entryKeys(keys) == device->keys)
    return;   // nothing changed: the meter stays connected, the recording too
  // the device in use takes them at once; its unsaved readings first
  const bool inUse = id == m_wid->currentDevice();
  if (inUse && !m_wid->confirmSwitch(device->name))
    return;
  m_wid->devices()->update(id, keys);
  if (inUse)
    m_wid->switchDevice(id, false);
}

QString MainWindow::deviceStatus(const QString &id) const
{
  const std::optional<MyDevice> device = m_wid->devices()->find(id);
  if (!device)
    return QString();
  auto para = [](const QString &text) { return QString("<p>%1</p>").arg(text.toHtmlEscaped()); };
  if (id != m_wid->currentDevice())
  {
    const MeterController *owner = placeOwners().value(DeviceLibrary::place(device->keys));
    return para(!owner                         ? tr("Not in use. A click on it in My devices takes it into this window.")
                : owner == m_wid->controller() ? tr("Its port is in use in this window.")
                                               : tr("Open beside this window's device."));
  }
  QString state;
  const qint64 ago = m_lastReadingAt > 0 ? (QDateTime::currentMSecsSinceEpoch() - m_lastReadingAt) / 1000 : -1;
  switch (m_wid->controller()->dmm()->linkState())
  {
    case MeterConnection::LinkState::Closed:     state = tr("Not connected."); break;
    case MeterConnection::LinkState::Connecting: state = tr("Connecting ..."); break;
    case MeterConnection::LinkState::Timeout:    state = tr("Connected, but the meter sends nothing."); break;
    case MeterConnection::LinkState::Error:      state = tr("The connection went away; QtDMM tries again."); break;
    case MeterConnection::LinkState::Connected:
      state = m_ledActive ? tr("Connected, readings are coming in.") : tr("Connected, no reading for %n s.", "", int(ago));
      break;
  }
  QString html = para(tr("In use in this window.")) + para(state);
  if (ago >= 0)
    html += para(tr("Last reading %n s ago.", "", int(ago)));
  if (m_lowBattery)
    html += para(tr("The meter's battery is low."));
  // its values with their readings
  QString rows;
  for (int value = 0; value < m_wid->numValues(); ++value)
    rows += QString("<tr><td>%1&nbsp;&nbsp;</td><td>%2&nbsp;&nbsp;</td><td align=\"right\">%3</td></tr>")
              .arg(value + 1).arg(m_wid->valueName(value).toHtmlEscaped(), m_wid->valueText(value).toHtmlEscaped());
  if (!rows.isEmpty())
    html += "<table cellspacing=\"0\" cellpadding=\"2\">" + rows + "</table>";
  return html;
}

void MainWindow::openBeside(const QString &id)
{
  const std::optional<MyDevice> device = m_wid->devices()->find(id);
  if (!device)
    return;
  // one port, one reader
  const MeterController *owner = placeOwners().value(DeviceLibrary::place(device->keys));
  if (id == m_wid->currentDevice() || owner == m_wid->controller())
  {
    statusBar()->showMessage(tr("%1 is in use in this window").arg(device->name), 4000);
    return;
  }
  if (owner)
  {
    statusBar()->showMessage(tr("%1 is open already").arg(device->name), 4000);
    return;
  }
  QString error;
  if (m_session->open(id, &error))
    statusBar()->showMessage(tr("%1 opened beside this window's device").arg(device->name), 4000);
  // one that does not connect said why in the status bar already
  else if (!error.startsWith("-240,"))
    m_error->setText(error.section(',', 1));
}

void MainWindow::syncArrangeActions()
{
  m_arrangeTop->setChecked(m_arranger->mode() == MdiArranger::DisplaysOnTop);
  m_arrangeLeft->setChecked(m_arranger->mode() == MdiArranger::DisplaysOnLeft);
  m_arrangeFixed->setChecked(m_arranger->mode() == MdiArranger::Fixed);
  m_arrangeFree->setChecked(m_arranger->mode() == MdiArranger::Free);
  m_titleBars->setChecked(m_arranger->titleBarsHidden());
}

void MainWindow::updateLed()
{
  m_sidebar->setCurrent(m_wid->currentDevice(), m_ledActive, m_ledBlink);
}

void MainWindow::updateToolbarButton()
{
  // an arrow where the toolbar goes or comes from
  const bool hidden = m_toolbars->isChecked();
  m_toolbarButton->setArrowType(hidden ? Qt::DownArrow : Qt::UpArrow);
  const QString key = m_toolbars->shortcut().toString(QKeySequence::NativeText);
  const QString tip = hidden ? tr("Show the toolbar") : tr("Hide the toolbar");
  m_toolbarButton->setToolTip(key.isEmpty() ? tip : QString("%1 (%2)").arg(tip, key));
}

// Which windows are shown, the mode and - in Free mode - where
// they are. The display's visibility is the old Display/show key.
void MainWindow::restoreWindows()
{
  Settings *cfg = m_wid->settings();
  applyWorkspace([cfg](const QString &key, const QVariant &def) -> QVariant
  {
    if (def.typeId() == QMetaType::Bool)
      return cfg->getBool(key, def.toBool());
    return cfg->getString(key, def.toString());
  });
  m_autoSaveLayout->setChecked(cfg->getBool("Windows/auto-save", true));
  m_startDisplay = m_displayAction->isChecked();
}

void MainWindow::saveWindows()
{
  Settings *cfg = m_wid->settings();
  cfg->setBool("Windows/auto-save", m_autoSaveLayout->isChecked());
  // without auto-save the layout stays as last saved (or loaded)
  if (!m_autoSaveLayout->isChecked())
    return;
  storeWorkspace([cfg](const QString &key, const QVariant &value)
  {
    if (value.typeId() == QMetaType::Bool)
      cfg->setBool(key, value.toBool());
    else
      cfg->setString(key, value.toString());
  });
  cfg->setBool("Windows/user-sized", m_userSized);
}

void MainWindow::applyWorkspace(const WorkspaceGet &get)
{
  setDesign(Designs::fromName(get("Windows/design", QString("dark")).toString()));
  m_restoring = true;
  const QString arrange = get("Windows/arrange", QString("left")).toString();
  const bool free = arrange == "free";
  const MdiArranger::Mode mode = free ? MdiArranger::Free : arrange == "left" ? MdiArranger::DisplaysOnLeft
                                 : arrange == "fixed" ? MdiArranger::Fixed : MdiArranger::DisplaysOnTop;
  // Fixed gets its tree first: switching to it from Free would derive one
  // from the positions
  if (mode == MdiArranger::Fixed)
    m_arranger->setMode(MdiArranger::DisplaysOnTop);
  m_arranger->setMode(mode);
  // the layout the user made (dividers, swaps); none or a broken one: the rule
  // the meters the workspace had open beside, with their panels: opened
  // now, at the start with the window's meter (openPending())
  m_pendingOpen = get("Windows/open-devices", QString()).toString().split(' ', Qt::SkipEmptyParts);
  for (const QString &id : std::as_const(m_pendingOpen))
  {
    const QString key = "Windows/panel-" + id;
    PanelState state;
    state.value = ValueRef::fromString(get(key + "-value", QString("0")).toString());
    state.folded = get(key + "-folded", false).toBool();
    const QString small = get(key + "-secondary-values", QStringLiteral("-")).toString();
    if (small != QLatin1String("-"))
    {
      state.secondaries = QList<int>();
      for (const QString &v : small.split(' ', Qt::SkipEmptyParts))
        state.secondaries->append(v.toInt());
    }
    state.shown = get(key + "-shown", true).toBool();
    state.geometry = get("Windows/geometry-panel-" + id, QString()).toString();
    if (QMdiSubWindow *win = m_panelWins.value("device:" + id))
      applyPanelState(win, state);
    else
      m_panelStates.insert(id, state);
  }
  const QString layout = get("Windows/layout", QString()).toString();
  if (!m_arranger->setLayoutText(layout))
  {
    m_arranger->setLayoutText(QString());   // a broken one: the rule
    // or one naming panels not there yet
    if (!m_pendingOpen.isEmpty())
      m_pendingLayout = layout;
  }
  m_arranger->setTitleBarsHidden(get("Windows/title-bars-hidden", !free).toBool());
  m_toolbars->setChecked(get("Windows/toolbar-hidden", false).toBool());
  if (free)
  {
    QMap<QString, QString> geometry;
    for (QMdiSubWindow *w : { m_displayWin, m_meterWin, m_graphWin, m_readingsWin, m_poincareWin })
      geometry[w->objectName()] = get("Windows/geometry-" + w->objectName(), QString()).toString();
    // once the window is shown and the area has its size: the automatic
    // layout for a start, then the stored positions on top of it
    QTimer::singleShot(0, this, [this, geometry]
    {
      m_arranger->arrangeNow();
      for (QMdiSubWindow *w : { m_displayWin, m_meterWin, m_graphWin, m_readingsWin, m_poincareWin })
      {
        const QStringList g = geometry.value(w->objectName()).split(' ');
        if (g.size() == 4)
          w->setGeometry(g[0].toInt(), g[1].toInt(), g[2].toInt(), g[3].toInt());
      }
    });
  }
  m_displayAction->setChecked(get("Display/show", true).toBool());
  m_meterAction->setChecked(get("Windows/meter", true).toBool());
  action_Graph->setChecked(get("MainWindow/show-graph", false).toBool());   // a fresh start is a compact instrument
  m_readingsAction->setChecked(get("Windows/readings", false).toBool());
  m_poincareAction->setChecked(get("Windows/poincare", false).toBool());
  m_wid->graph()->setReferenceSetting(get("Windows/graph-reference", QString()).toString());
  m_wid->graph()->setMinimapVisible(get("Windows/graph-minimap", true).toBool());
  m_wid->graph()->setEnvelopeVisible(get("Windows/graph-envelope", false).toBool());
  // "1", another meter's value "1@device:ab12cd34" (ValueRef)
  m_wid->setLcdValue(ValueRef::fromString(get("Windows/lcd-value", 0).toString()));
  m_wid->setMeterValue(ValueRef::fromString(get("Windows/meter-value", 0).toString()));
  m_wid->setPoincareValue(ValueRef::fromString(get("Windows/poincare-value", 0).toString()));
  // the LCD's small line, "1 2"; missing: the first values after the big one
  {
    const QString small = get("Windows/lcd-secondary-values", QStringLiteral("-")).toString();
    std::optional<QList<int>> ids;
    if (small != QLatin1String("-"))
    {
      ids = QList<int>();
      for (const QString &id : small.split(' ', Qt::SkipEmptyParts))
        ids->append(id.toInt());
    }
    m_wid->setLcdSecondaries(ids);
  }
  // the axis of each value in the graph, "1:right 2:left"; missing: automatic
  {
    const QStringList axes = get("Windows/graph-value-axes", QString()).toString().split(' ', Qt::SkipEmptyParts);
    for (int id = 1; id <= 3; ++id)
    {
      GraphWidget::ValueAxis axis = GraphWidget::AxisAuto;
      if (axes.contains(QString("%1:left").arg(id)))
        axis = GraphWidget::AxisLeft;
      else if (axes.contains(QString("%1:right").arg(id)))
        axis = GraphWidget::AxisRight;
      m_wid->graph()->setValueAxis(id, axis);
    }
    // another meter's: "1@device:ab12cd34:right"
    for (const QString &entry : axes)
    {
      const qsizetype colon = entry.lastIndexOf(':');
      if (!entry.contains('@') || colon < 0)
        continue;
      const QString side = entry.mid(colon + 1);
      m_wid->graph()->setOtherValueAxis(ValueRef::fromString(entry.left(colon)),
                                        side == "left" ? GraphWidget::AxisLeft
                                        : side == "right" ? GraphWidget::AxisRight : GraphWidget::AxisAuto);
    }
  }
  // the meter's other values shown in the graph: their ids, "1 2"; none at
  // first. Another meter's ("1@device:ab12cd34") is drawn from its store.
  {
    QSet<int> own;
    QList<ValueRef> others;
    for (const QString &text : get("Windows/graph-shown-values", QString()).toString().split(' ', Qt::SkipEmptyParts))
    {
      const ValueRef v = ValueRef::fromString(text);
      if (m_wid->ownMeter(v))
        own.insert(v.id);
      else
        others << v;
    }
    m_wid->graph()->setOtherValues(others);
    for (int id = 1; id <= 3; ++id)
      m_wid->graph()->setValueShown(id, own.contains(id));
    m_wid->graph()->setValueShown(0, get("Windows/graph-main-shown", true).toBool());
  }
  // the values the readings table leaves out, "1 2", another meter's
  // "1@device:ab12cd34"; none at first. The other meters in it, by source
  {
    const QStringList hidden = get("Windows/table-hidden-values", QString()).toString().split(' ', Qt::SkipEmptyParts);
    ReadingsModel *log = m_readings->log();
    for (const ValueRef &v : log->hiddenValues())
      log->setValueHidden(v, false);
    for (const QString &text : hidden)
      log->setValueHidden(ValueRef::fromString(text), true);
    m_tableOthers = get("Windows/table-sources", QString()).toString().split(' ', Qt::SkipEmptyParts);
    applyTableSources();
  }
  // an unchecked action did not toggle: hide its window explicitly
  for (QAction *a : { m_displayAction, m_meterAction, action_Graph, m_readingsAction, m_poincareAction })
    qobject_cast<QWidget *>(a->property("window").value<QObject *>())->setVisible(a->isChecked());
  m_restoring = false;
  syncArrangeActions();
  m_arranger->arrange();
}

void MainWindow::storeWorkspace(const WorkspaceSet &set)
{
  static const char *const modes[] = { "top", "left", "fixed", "free" };
  set("Windows/arrange", QString(modes[m_arranger->mode()]));
  set("Windows/layout", m_arranger->layoutText());
  set("Windows/title-bars-hidden", m_arranger->titleBarsHidden());
  set("Windows/toolbar-hidden", m_toolbars->isChecked());
  set("Windows/design", Designs::name(Designs::current()));
  if (m_arranger->mode() == MdiArranger::Free)
    for (QMdiSubWindow *w : QList<QMdiSubWindow *>{ m_meterWin, m_graphWin, m_readingsWin, m_poincareWin }
                            + panelWindows())
    {
      const QRect g = w->geometry();
      set("Windows/geometry-" + w->objectName(),
          QString("%1 %2 %3 %4").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()));
    }
  QStringList open;
  for (QMdiSubWindow *win : std::as_const(m_panelWins))
  {
    const QString id = win->objectName().mid(QStringLiteral("panel-").size());
    const QString key = "Windows/panel-" + id;
    open << id;
    set(key + "-value", ValueRef(panelOf(win)->value().id).toString());
    set(key + "-folded", panelOf(win)->isFolded());
    if (panelOf(win)->secondariesChosen())
    {
      QStringList small;
      for (int v : panelOf(win)->secondaries())
        small << QString::number(v);
      set(key + "-secondary-values", small.join(' '));
    }
    set(key + "-shown", !win->isHidden());
  }
  set("Windows/open-devices", open.join(' '));
  set("Display/show", m_displayAction->isChecked());
  set("Windows/meter", m_meterAction->isChecked());
  set("Windows/readings", m_readingsAction->isChecked());
  set("Windows/poincare", m_poincareAction->isChecked());
  set("MainWindow/show-graph", action_Graph->isChecked());
  set("Windows/graph-reference", m_wid->graph()->referenceSetting());
  set("Windows/graph-minimap", m_wid->graph()->minimapVisible());
  set("Windows/graph-envelope", m_wid->graph()->envelopeVisible());
  set("Windows/lcd-value", m_wid->lcdValue().toString());
  set("Windows/meter-value", m_wid->meterValue().toString());
  set("Windows/poincare-value", m_wid->poincareValue().toString());
  QStringList shown;
  for (int id : m_wid->graph()->shownValues())
    shown << ValueRef(id).toString();
  for (const ValueRef &v : m_wid->graph()->otherValues())
    shown << v.toString();
  set("Windows/graph-shown-values", shown.join(' '));
  set("Windows/graph-main-shown", m_wid->graph()->mainShown());
  QStringList tableHidden;
  for (const ValueRef &v : m_readings->log()->hiddenValues())
    tableHidden << v.toString();
  tableHidden.sort();
  set("Windows/table-hidden-values", tableHidden.join(' '));
  set("Windows/table-sources", m_tableOthers.join(' '));
  if (m_wid->lcdSecondariesChosen())
  {
    QStringList small;
    for (int id : m_wid->lcdSecondaries())
      small << QString::number(id);
    set("Windows/lcd-secondary-values", small.join(' '));
  }
  QStringList axes;
  for (int id = 1; id <= 3; ++id)
    if (m_wid->graph()->valueAxis(id) != GraphWidget::AxisAuto)
      axes << QString("%1:%2").arg(id).arg(m_wid->graph()->valueAxis(id) == GraphWidget::AxisLeft ? "left" : "right");
  for (const ValueRef &v : m_wid->graph()->otherValues())
    if (m_wid->graph()->otherValueAxis(v) != GraphWidget::AxisAuto)
      axes << QString("%1:%2").arg(v.toString(), m_wid->graph()->otherValueAxis(v) == GraphWidget::AxisLeft ? "left" : "right");
  set("Windows/graph-value-axes", axes.join(' '));
}

// A workspace file is an ini file with the same keys as the settings, plus
// the size of the main window.
static const char *kWorkspaceSuffix = "qtdmm-workspace";

void MainWindow::saveWorkspace()
{
  Settings *cfg = m_wid->settings();
  const QString dir = cfg->getString("Windows/workspace-dir",
                                     QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
  QString file = QFileDialog::getSaveFileName(this, tr("Save workspace"), dir,
                                              tr("QtDMM workspace (*.%1)").arg(kWorkspaceSuffix));
  if (file.isEmpty())
    return;
  if (QFileInfo(file).suffix().isEmpty())
    file += QString(".") + kWorkspaceSuffix;
  QFile::remove(file);   // only our keys in it
  QSettings ws(file, QSettings::IniFormat);
  ws.setValue("Workspace/version", 1);
  ws.setValue("Workspace/size", QString("%1 %2").arg(width()).arg(height()));
  storeWorkspace([&ws](const QString &key, const QVariant &value) { ws.setValue(key, value); });
  ws.sync();
  if (ws.status() != QSettings::NoError)
  {
    QMessageBox::warning(this, tr("QtDMM: Save workspace"), tr("Could not write %1.").arg(QDir::toNativeSeparators(file)));
    return;
  }
  cfg->setString("Windows/workspace-dir", QFileInfo(file).absolutePath());
  recentWorkspaces().add(file);
  updateRecentWorkspaces();
  Q_EMIT m_wid->error(tr("Workspace saved to %1").arg(QDir::toNativeSeparators(file)));
}

void MainWindow::loadWorkspace()
{
  Settings *cfg = m_wid->settings();
  const QString dir = cfg->getString("Windows/workspace-dir",
                                     QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
  const QString file = QFileDialog::getOpenFileName(this, tr("Load workspace"), dir,
                                                    tr("QtDMM workspace (*.%1)").arg(kWorkspaceSuffix));
  if (!file.isEmpty())
    loadWorkspaceFile(file);
}

bool MainWindow::loadWorkspaceFile(const QString &file)
{
  Settings *cfg = m_wid->settings();
  QSettings ws(file, QSettings::IniFormat);
  if (!QFileInfo::exists(file) || ws.status() != QSettings::NoError || ws.value("Workspace/version").toInt() < 1)
  {
    QMessageBox::warning(this, tr("QtDMM: Load workspace"),
                         tr("%1 is no QtDMM workspace.").arg(QDir::toNativeSeparators(file)));
    return false;
  }
  cfg->setString("Windows/workspace-dir", QFileInfo(file).absolutePath());
  // the size first: the arrangement fills the area it gets
  const QStringList size = ws.value("Workspace/size").toString().split(' ');
  if (size.size() == 2 && !isMaximized() && !isFullScreen())
  {
    resize(size[0].toInt(), size[1].toInt());
    m_userSized = true;   // a size chosen on purpose: no growing
  }
  applyWorkspace([&ws](const QString &key, const QVariant &def) -> QVariant
  {
    const QVariant v = ws.value(key, def);
    return def.typeId() == QMetaType::Bool ? QVariant(v.toBool()) : QVariant(v.toString());
  });
  // without auto-save the loaded workspace is the one to start with next time
  if (!m_autoSaveLayout->isChecked())
  {
    m_autoSaveLayout->setChecked(true);
    saveWindows();
    m_autoSaveLayout->setChecked(false);
    m_startDisplay = m_displayAction->isChecked();
  }
  openPending();
  recentWorkspaces().add(file);
  updateRecentWorkspaces();
  Q_EMIT m_wid->error(tr("Workspace loaded from %1").arg(QDir::toNativeSeparators(file)));
  return true;
}

RecentFiles MainWindow::recentWorkspaces() const
{
  return RecentFiles(QDir(m_wid->settings()->configDir()).filePath("workspaces.conf"));
}

void MainWindow::updateRecentWorkspaces()
{
  const QStringList files = recentWorkspaces().list().mid(0, m_wid->settings()->getInt("Sidebar/recent-workspaces", 10));
  m_sidebar->setWorkspaces(files);
  m_recentMenu->clear();
  for (int i = 0; i < files.size(); ++i)
  {
    const QString &file = files[i];
    // 1 ... 9 as the letters, then none
    const QString name = QFileInfo(file).completeBaseName().replace('&', "&&");
    QAction *a = m_recentMenu->addAction(i < 9 ? QString("&%1 %2").arg(i + 1).arg(name) : name);
    a->setToolTip(QDir::toNativeSeparators(file));
    a->setEnabled(QFileInfo::exists(file));
    connect(a, &QAction::triggered, this, [this, file] { loadWorkspaceFile(file); });
  }
  m_recentMenu->setToolTipsVisible(true);
  m_recentMenu->menuAction()->setEnabled(!files.isEmpty());
  if (files.isEmpty())
    return;
  m_recentMenu->addSeparator();
  QAction *clear = m_recentMenu->addAction(QIcon::fromTheme("edit-clear-history"), tr("&Clear list"));
  connect(clear, &QAction::triggered, this, [this]
  {
    recentWorkspaces().clear();
    updateRecentWorkspaces();
  });
}

// ---------------------------------------------------------------- window size

int MainWindow::em() const
{
  return QFontMetrics(font()).height();
}

QSize MainWindow::growthFor(QMdiSubWindow *win) const
{
  if (win == m_graphWin)
    return QSize(0, 18 * em());    // below the instruments
  if (win == m_readingsWin || win == m_poincareWin)
    return QSize(19 * em(), 0);    // a column on the right
  if (win == m_meterWin || win == m_displayWin)
    return QSize(20 * em(), 0);    // next to the other instrument
  return QSize();
}

void MainWindow::growFor(QMdiSubWindow *win)
{
  if (m_grown.contains(win))
    return;
  m_grown.insert(win);
  // the display and the analog meter share the top row: the first of them
  // needs no room of its own
  if ((win == m_displayWin || win == m_meterWin)
      && !m_grown.contains(win == m_displayWin ? m_meterWin : m_displayWin))
    return;
  // the same for the readings table and the Poincaré plot in the column
  if ((win == m_readingsWin || win == m_poincareWin)
      && m_grown.contains(win == m_readingsWin ? m_poincareWin : m_readingsWin))
    return;
  const QSize before = size();
  autoGrow(growthFor(win));
  if (win == m_readingsWin || win == m_poincareWin)
    m_arranger->setTableWidth(size().width() - before.width());
}

void MainWindow::autoGrow(const QSize &delta)
{
  if (m_userSized || !m_growEnabled)
    return;
  // maximized: the room is kept for when the window comes back
  if (isMaximized() || isFullScreen())
  {
    m_pendingGrow += delta;
    return;
  }
  const QRect av = screen()->availableGeometry();
  // Wayland neither tells a window where it is nor lets it move, so a
  // grown frame cannot be pulled back onto the screen: grow less there.
  // Compositors place new windows in the upper left part (Miriway at a
  // quarter of the width, KWin further left), which leaves room for 70 %.
  const double limit = QGuiApplication::platformName().startsWith("wayland") ? 0.70 : 0.84;
  const int maxW = int(av.width() * limit), maxH = int(av.height() * limit);
  const QSize s = size();
  const QSize n(qMax(s.width(), qMin(s.width() + delta.width(), maxW)),
                qMax(s.height(), qMin(s.height() + delta.height(), maxH)));
  if (n == s)
    return;
  const QSize frameExtra = frameGeometry().size() - s;
  m_expectSize = n;
  resize(n);
  // stay on screen: move up/left when the grown frame would stick out
  QPoint p = pos();
  p.setX(qMax(av.left(), qMin(p.x(), av.right() + 1 - (n.width() + frameExtra.width()))));
  p.setY(qMax(av.top(), qMin(p.y(), av.bottom() + 1 - (n.height() + frameExtra.height()))));
  if (p != pos())
    move(p);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
  QMainWindow::resizeEvent(event);
  if (m_growEnabled && !m_unmaximizing && event->size() != m_expectSize && event->oldSize().isValid()
      && !isMaximized() && !isFullScreen())
    m_userSized = true;
}

void MainWindow::changeEvent(QEvent *event)
{
  QMainWindow::changeEvent(event);
  // the desktop switched between light and dark: the design afresh, or the
  // palettes set on the views and the sidebar's background stay the old
  // ones. Not again for the palette change the design itself makes
  if ((event->type() == QEvent::ThemeChange || event->type() == QEvent::ApplicationPaletteChange)
      && !m_designPending && (!m_designApplied.isValid() || m_designApplied.elapsed() > 500))
  {
    m_designPending = true;
    QTimer::singleShot(0, this, [this]
    {
      m_designPending = false;
      setDesign(Designs::current());
    });
  }
  if (event->type() != QEvent::WindowStateChange)
    return;
  // maximized is no size chosen: back from it, the window grows once for
  // the views shown meanwhile
  const bool full = isMaximized() || isFullScreen();
  if (!full && m_wasFull)
  {
    m_unmaximizing = true;
    // the window manager restores the size in steps; what it settles on is
    // the size the window had
    QTimer::singleShot(300, this, [this]
    {
      m_unmaximizing = false;
      m_expectSize = size();
      const QSize pending = m_pendingGrow;
      m_pendingGrow = QSize(0, 0);
      if (!pending.isNull() && !isMaximized() && !isFullScreen())
        autoGrow(pending);
    });
  }
  m_wasFull = full;
}

void MainWindow::showEvent(QShowEvent *event)
{
  QMainWindow::showEvent(event);
  // the window manager may still adjust the first size; what it settles on
  // is the start, not a size the user chose
  if (!m_growEnabled)
    QTimer::singleShot(300, this, [this]
    {
      m_expectSize = size();
      m_growEnabled = true;
    });
}

// ---------------------------------------------------------------- meter keys

void MainWindow::setControlsFolded(bool folded)
{
  m_panel->setFolded(folded);
  m_wid->settings()->setBool("Display/controls-hidden", folded);
  updateControls();
}

void MainWindow::updateControls()
{
  for (QMdiSubWindow *win : panelWindows())
  {
    // the keys below the LCD are part of its window
    FrontPanel *panel = panelOf(win);
    m_arranger->setContentAspect(win, LcdWidget::kMinAspect, LcdWidget::kMaxAspect, panel->keysHeight());
    // the first time a panel's keys appear, the window grows for them
    if (panel->keysShown() && !m_grown.contains(panel))
    {
      m_grown.insert(panel);
      autoGrow(QSize(0, 2 * em()));
    }
  }
  m_arranger->arrange();
}

void MainWindow::setDesign(int design)
{
  const auto d = static_cast<Designs::Design>(design);
  m_designApplied.start();
  Designs::apply(d);
  m_mdi->setBackground(Designs::areaBrush(d));
  QPalette empty = m_emptyStart->palette();
  empty.setBrush(QPalette::Window, Designs::areaBrush(d));
  m_emptyStart->setPalette(empty);
  // the instruments are drawn on a transparent background: the window's
  // (metal in Silver and Brushed metal) shows around them. The front
  // panels, not the LCDs: the meters' keys below them stand on the same sheet
  const QBrush frame = Designs::frameBrush(d);
  for (QWidget *view : { static_cast<QWidget *>(m_panel), static_cast<QWidget *>(m_meter),
                         static_cast<QWidget *>(m_readings) })
    applyFrame(view);
  for (QMdiSubWindow *win : std::as_const(m_panelWins))
    applyFrame(win->widget());
  updateLed();
  const Designs::GraphColors g = Designs::graphColors(d);
  m_wid->graph()->setThemeColors(g.background, g.grid, g.labels, g.data);
  // the Poincaré plot on the background around the instruments, not on the
  // graph's: it stands next to the display and the meter. Not on a texture
  // though, the points would get lost in it: there on the graph's
  QBrush poincare = frame.style() != Qt::NoBrush ? frame : QApplication::palette().window();
  if (frame.style() == Qt::TexturePattern)
    poincare = g.background;
  m_poincare->setThemeColors(poincare, g.grid, g.labels, g.data);
  for (QAction *a : m_designMenu->actions())
    a->setChecked(a->data().toInt() == design);
  // at once, so the settings dialog shows the design of the menu (and its
  // OK does not set the one of before again)
  m_wid->settings()->setString("Windows/design", Designs::name(d));
  m_wid->settingsDialog()->guiPage()->setDesign(Designs::name(d));
}

void MainWindow::applyFrame(QWidget *view)
{
  const QBrush frame = Designs::frameBrush(Designs::current());
  const bool own = frame.style() != Qt::NoBrush;
  QPalette pal = view->palette();
  pal.setBrush(QPalette::Window, own ? frame : QApplication::palette().window());
  view->setPalette(pal);
  view->setAutoFillBackground(own);
  Designs::fitTexture(view, frame.style() == Qt::TexturePattern);
  view->update();
}

void MainWindow::storeDisplaySLOT()
{
  m_wid->settingsDialog()->guiPage()->setShowDisplay(m_displayAction->isChecked());
}

void MainWindow::setConnectSLOT(bool on)
{
  windowConnected(on);
}

void MainWindow::windowConnected(bool on)
{
  if (!m_activeOther)
  {
    action_Connect->setChecked(on);
    return;
  }
  // another meter's state is shown: Record follows the window's all the same
  m_windowConnect = on;
  connectSLOT(on);
}

MeterController *MainWindow::activeController() const
{
  return m_active ? m_active.data() : m_wid->controller();
}

void MainWindow::setActive(MeterController *ctl)
{
  if (ctl == m_wid->controller())
    ctl = nullptr;
  if (ctl || m_activeOther)
  {
    // the window's state kept while another one is shown
    if (!m_activeOther)
      m_windowConnect = action_Connect->isChecked();
    m_active = ctl;
    m_activeOther = ctl != nullptr;
    action_Connect->setChecked(ctl ? ctl->dmm()->isWanted() : m_windowConnect);
  }
  if (!m_activeName)
    return;
  const QList<MeterController *> meters = m_session->controllers();
  const MeterController *active = activeController();
  const QString name = active == m_wid->controller() && active->deviceName().isEmpty() ? m_wid->dmmTitle()
                                                                                       : active->deviceName();
  m_activeName->setText(tr("Active: %1").arg(name));
  m_activeName->setToolTip(tr("Connect, Reset and the device settings act on this device; "
                              "a click on another front panel makes that one active"));
  m_activeName->setVisible(meters.size() > 1);
  updateOpenDevices();
}

void MainWindow::showDisplaySLOT(bool show)
{
  m_displayAction->setChecked(show);
}

QMenu *MainWindow::createPopupMenu()
{
  // the toolbars are always there: no menu to hide them
  return nullptr;
}

void MainWindow::setupIcons()
{
  // theme icons exist on Linux desktops only; Windows and macOS get the
  // bundled ones
  QIcon iconConnectOn = QIcon::fromTheme("network-connect");
  QIcon iconConnectOff = QIcon::fromTheme("network-disconnect");

  this->action_Connect->setIcon(iconConnectOff);
  connect(this->action_Connect, &QAction::toggled, this, [ = ](bool checked)
  {
    this->action_Connect->setIcon(checked ? iconConnectOn : iconConnectOff);
  });
}

void MainWindow::bringMainWindowToFront()
{
  QWidget *mainWin = nullptr;
  const auto topWidgets = QApplication::topLevelWidgets();

  for (QWidget *w : topWidgets)
  {
    if (w->inherits("MainWindow"))
    {
      mainWin = w;
      break;
    }
  }

  if (!mainWin)
    return;

  mainWin->showNormal();
  mainWin->raise();
  mainWin->activateWindow();
}
