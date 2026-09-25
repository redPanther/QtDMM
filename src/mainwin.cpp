//======================================================================
// File:		mainwin.cpp
// Author:	Matthias Toussaint
// Created:	Sun Sep  2 12:15:28 CEST 2001
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

#include <QtGui>
#include <QtWidgets>
#include <QTimer>
#include <QMenu>

#include "mainwin.h"
#include "helpdlg.h"
#include "mainwid.h"
#include "dmmgraph.h"
#include "displaywid.h"
#include "meterwid.h"
#include "readinglogwid.h"
#include "settings.h"
#include "alarmbar.h"
#include "mdiarranger.h"
#include "metercontroller.h"
#include "viewframe.h"
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QLoggingCategory>

MainWin::MainWin(QCommandLineParser &parser, QWidget *parent)
  : QMainWindow(parent)
  , m_running(false)
  , m_menu(Q_NULLPTR)
  , m_helpDlg(Q_NULLPTR)
  , m_localRecord(true)
{
  setupUi(this);
  setupIcons();
  m_config_id = parser.value("config-id");

  m_stateMgr = new SharedStateManager(m_config_id.isEmpty()?"default":m_config_id,this);

  QWidget* spacer = new QWidget();
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  this->toolBarMenu->addWidget(spacer);
  this->toolBarMenu->addAction(this->action_Menu);
  m_wid = new MainWid(m_config_id, parser.value("config-dir"), this);
  m_wid->setFrameShape(QFrame::NoFrame);
  setConsoleLogging(parser.isSet("debug"));

  createActions();

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

  MeterController *ctl = m_wid->controller();
  connect(ctl, &MeterController::alarmBannerChanged, m_alarmBar, &AlarmBar::setAlarms);
  connect(m_alarmBar, &AlarmBar::acknowledged, ctl, &MeterController::acknowledgeAlarms);

  // the four views; none of them knows another, they all hang off the
  // controller (see MainWid)
  m_display = new DisplayWid(this);
  m_wid->setDisplay(m_display);
  m_displayFrame = new ViewFrame(m_display, true);
  m_displayWin = addView(m_displayFrame, tr("Display"), MdiArranger::Instrument, "display");

  m_meter = new MeterWid(this);
  m_wid->setMeter(m_meter);
  m_meterFrame = new ViewFrame(m_meter, true);
  m_meterWin = addView(m_meterFrame, tr("Analog meter"), MdiArranger::Instrument, "meter");
  m_wid->setStateManager(m_stateMgr);

  m_graphWin = addView(m_wid, tr("Graph"), MdiArranger::Graph, "graph");

  m_readings = new ReadingLogWid(this);
  m_readings->setMaxRows(m_wid->settings()->getInt("ReadingLog/max-rows", 10000));
  m_wid->setReadingLog(m_readings->log());
  m_readingsFrame = new ViewFrame(m_readings, false);
  m_readingsFrame->setTitle(tr("Readings"));
  m_readingsWin = addView(m_readingsFrame, tr("Readings"), MdiArranger::Table, "readings");

  for (ViewFrame *f : { m_displayFrame, m_meterFrame })
    connect(ctl, &MeterController::reading, f, [f](double, const QString &, const QString &, const QString &,
                                                   const QString &, bool, bool, int id)
    {
      if (id == 0)
        f->pulse();
    });
  for (auto [frame, win] : { std::pair{m_displayFrame, m_displayWin}, std::pair{m_meterFrame, m_meterWin},
                             std::pair{m_readingsFrame, m_readingsWin} })
    connect(frame, &ViewFrame::menuRequested, this, [this, w = win](const QPoint &pos) { windowMenu(w, pos); });

  // one checkable action per window: toolbar buttons, menu entries, shortcuts
  m_displayAction = windowAction(m_displayWin, tr("&Display"), "Ctrl+1", ":/Symbols/display.xpm",
    tr("<html><head/><body><p><span style=\" font-weight:600;\">Display</span></p>"
       "<p>Show the reading on the LCD-style digital display.</p></body></html>"));
  m_meterAction = windowAction(m_meterWin, tr("Analog &meter"), "Ctrl+2", ":/Symbols/meter.xpm",
    tr("<html><head/><body><p><span style=\" font-weight:600;\">Analog meter</span></p>"
       "<p>Show the reading on a moving-coil style instrument.</p></body></html>"));
  m_readingsAction = windowAction(m_readingsWin, tr("&Readings table"), "Ctrl+4", ":/Symbols/table.xpm",
    tr("<html><head/><body><p><span style=\" font-weight:600;\">Readings table</span></p>"
       "<p>Every reading the meter sent, one row each, with time, mode and range - "
       "the raw protocol of the session next to the recorder's graph. Copy rows to a "
       "spreadsheet or export them as CSV.</p></body></html>"));
  // the graph keeps its action from the .ui (toolbar button with Ctrl+G)
  action_Graph->setShortcuts({QKeySequence("Ctrl+G"), QKeySequence("Ctrl+3")});
  bindWindowAction(action_Graph, m_graphWin);

  toolBarDMM->addSeparator();
  toolBarDMM->addAction(m_displayAction);
  toolBarDMM->addAction(m_meterAction);
  toolBarDMM->addAction(m_readingsAction);
  connect(m_displayAction, &QAction::toggled, this, &MainWin::setToolbarVisibilitySLOT);

  // arrangement: automatic ("Displays on top") or free, title bars on/off
  auto *arrangeGroup = new QActionGroup(this);
  m_arrangeTop = new QAction(tr("Displays on &top"), arrangeGroup);
  m_arrangeTop->setCheckable(true);
  m_arrangeTop->setWhatsThis(tr("<html><head/><body><p><span style=\" font-weight:600;\">Displays on top</span></p>"
                                "<p>The displays share a strip at the top, the graph takes the rest and the "
                                "readings table a column on the right; everything follows the window size. "
                                "Drag a window onto another one to swap the two.</p></body></html>"));
  m_arrangeFree = new QAction(tr("&Free"), arrangeGroup);
  m_arrangeFree->setCheckable(true);
  m_arrangeFree->setWhatsThis(tr("<html><head/><body><p><span style=\" font-weight:600;\">Free</span></p>"
                                 "<p>Place and size the windows as you like.</p></body></html>"));
  connect(m_arrangeTop, &QAction::triggered, this, [this] { m_arranger->setMode(MdiArranger::DisplaysOnTop); });
  connect(m_arrangeFree, &QAction::triggered, this, [this] { m_arranger->setMode(MdiArranger::Free); });

  m_titleBars = new QAction(tr("&Hide title bars"), this);
  m_titleBars->setCheckable(true);
  m_titleBars->setShortcut(QKeySequence("Ctrl+L"));
  m_titleBars->setWhatsThis(tr("<html><head/><body><p><span style=\" font-weight:600;\">Hide title bars</span></p>"
                               "<p>Windows without title bar sit flush next to each other. Their header line "
                               "keeps the name, right-click it for the window's menu; Ctrl+drag moves a window."
                               "</p></body></html>"));
  connect(m_titleBars, &QAction::triggered, m_arranger, &MdiArranger::setTitleBarsHidden);
  connect(m_arranger, &MdiArranger::changed, this, &MainWin::syncArrangeActions);

  m_arrangeMenu = new QMenu(tr("&Arrange"), this);
  m_arrangeMenu->addAction(m_arrangeTop);
  m_arrangeMenu->addAction(m_arrangeFree);
  m_arrangeMenu->addSeparator();
  m_arrangeMenu->addAction(m_titleBars);
  QAction *arrangeButton = m_arrangeMenu->menuAction();
  arrangeButton->setIcon(arrangeIcon());
  arrangeButton->setToolTip(tr("Arrange the windows"));
  toolBarDMM->addAction(arrangeButton);
  if (auto *button = qobject_cast<QToolButton *>(toolBarDMM->widgetForAction(arrangeButton)))
    button->setPopupMode(QToolButton::InstantPopup);

  updateWindowTitle();
  connect(m_wid, &MainWid::configChanged, this, &MainWin::updateWindowTitle);
  connect(m_wid, &MainWid::configChanged, this, &MainWin::updateHeaders);

  createExtraActions();
  addShortcutsToToolTips();

  connect(m_wid, SIGNAL(running(bool)), this, SLOT(runningSLOT(bool)));

  connectSLOT(false);

  // status bar
  m_error = new QLabel(statusBar());
  m_error->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(m_error, 20);
  m_error->setLineWidth(1);

  m_info = new QLabel(statusBar());
  m_info->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(m_info, 10);
  m_info->setLineWidth(1);

  m_scpi = new QLabel(statusBar());
  m_scpi->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  m_scpi->setLineWidth(1);
  m_scpi->setToolTip(tr("The SCPI server: other programs can read the meter here (Settings, SCPI server)."));
  statusBar()->addPermanentWidget(m_scpi);
  m_scpi->hide();
  connect(m_wid, &MainWid::scpiStatus, this, [this](const QString &text)
  {
    m_scpi->setText(text);
    m_scpi->setVisible(!text.isEmpty());
  });

  // messages such as the permission hint span several lines; the status bar
  // shows the first one and keeps the rest in the tooltip
  connect(m_wid, &MainWid::error, this, [this](const QString &text)
  {
    const QString firstLine = text.section('\n', 0, 0);
    m_error->setText(firstLine);
    m_error->setToolTip(text.contains('\n') ? text : QString());
  });
  connect(m_wid, SIGNAL(info(const QString &)), m_info, SLOT(setText(const QString &)));
  connect(m_wid, SIGNAL(useTextLabel(bool)), this, SLOT(setUseTextLabel(bool)));
  connect(m_wid, SIGNAL(setConnect(bool)), this, SLOT(setConnectSLOT(bool)));
  connect(m_wid, SIGNAL(connectDMM(bool)), action_Connect, SLOT(setChecked(bool)));
  connect(m_wid, SIGNAL(toolbarVisibility(bool, bool, bool, bool)),
          this, SLOT(toolbarVisibilitySLOT(bool, bool, bool, bool)));

  QRect winRect = m_wid->winRect();

  m_wid->applySLOT();
  // the layout of the windows; the dock state of versions before the MDI
  // window (MainWindow/state) is not taken over
  restoreWindows();

  if (!winRect.isEmpty())
  {
    if (m_wid->saveWindowPosition())
    {
      move(winRect.x(), winRect.y());
    }
    if (m_wid->saveWindowSize())
      resize(winRect.width(), winRect.height());
    else
      resize(550, 250);
  }
  else
    resize(550, 250);

  connect(m_stateMgr, &SharedStateManager::stateChanged, this, [=](const QString& state){
    if (state == "RECORD")
    {
      QMetaObject::invokeMethod(m_wid, "startSLOT", Qt::DirectConnection);
      m_localRecord = false;
    }
    else if (state == "STOP")
    {
      if (!m_localRecord)
        action_Stop->trigger();
    }
    else if (state == "RAISE_"+(m_config_id.isEmpty()?"default":m_config_id))
    {
      bringMainWindowToFront();
      m_stateMgr->writeState("IDLE");
    }
  });

  connect(m_stateMgr, &SharedStateManager::instanceIdAlreadyInUse, this, [=](){
    QMessageBox::critical(this, APP_NAME,tr("Another instance is running."));
    qApp->quit();
  });

  // auto-connect at start, but not before a meter was ever chosen: a fresh
  // instance would otherwise try the first serial port it finds
  if (m_stateMgr->registerInstance() && m_wid->dmmConfigured())
    QTimer::singleShot(1000, action_Connect, &QAction::trigger);
}

// "QtDMM: UNI-T UT61E", with the instance id for non-default instances
void MainWin::updateWindowTitle()
{
  QString title = APP_NAME;
  if (!m_config_id.isEmpty())
    title += QString(" [%1]").arg(m_config_id);
  setWindowTitle(QString("%1: %2").arg(title, m_wid->dmmTitle()));
}

void MainWin::sendStateSLOT(const QString & state)
{
  m_stateMgr->writeState(state);
}


void MainWin::setConsoleLogging(bool on)
{
  if (on)
    QLoggingCategory::setFilterRules("qtdmm.hid.debug=true\nqtdmm.ble.debug=true\nqtdmm.mdns.debug=true\nqtdmm.scpi.debug=true");
  m_wid->setConsoleLogging(on);
}

void MainWin::setUseTextLabel(bool on)
{
  Qt::ToolButtonStyle Style = Qt::ToolButtonTextUnderIcon;
  if (!on)
    Style = Qt::ToolButtonIconOnly;
  toolBarDMM->setToolButtonStyle(Style);
  toolBarRecorder->setToolButtonStyle(Style);
  toolBarFile->setToolButtonStyle(Style);
  toolBarMenu->setToolButtonStyle(Style);
}

void MainWin::createActions()
{
  connect(action_Connect, SIGNAL(triggered(bool)), m_wid, SLOT(connectSLOT(bool)));
  connect(action_Connect, SIGNAL(triggered(bool)), this, SLOT(connectSLOT(bool)));
  connect(action_Reset, SIGNAL(triggered()), m_wid, SLOT(resetSLOT()));
  connect(action_Start, SIGNAL(triggered()), this, SLOT(startSLOT()));
  connect(action_Stop, SIGNAL(triggered()), m_wid, SLOT(stopSLOT()));
  connect(action_Stop, SIGNAL(triggered()), this, SLOT(stopSLOT()));
  connect(action_Clear, SIGNAL(triggered()), m_wid, SLOT(clearSLOT()));
  connect(action_Print, SIGNAL(triggered()), m_wid, SLOT(printSLOT()));
  connect(action_Import, SIGNAL(triggered()), m_wid, SLOT(importSLOT()));
  connect(action_Export, SIGNAL(triggered()), m_wid, SLOT(exportSLOT()));
  connect(action_Configure, SIGNAL(triggered()), m_wid, SLOT(configSLOT()));
  connect(action_ConfigureDMM, SIGNAL(triggered()), m_wid, SLOT(configDmmSLOT()));
  connect(actionConfigureRecorder, SIGNAL(triggered()), m_wid, SLOT(configRecorderSLOT()));
  connect(action_Quit, SIGNAL(triggered()), this, SLOT(setToolbarVisibilitySLOT()));
  connect(action_Quit, SIGNAL(triggered()), m_wid, SLOT(quitSLOT()));
  connect(action_Direct_help, SIGNAL(triggered()), m_wid, SLOT(helpSLOT()));
  connect(action_Tip_of_the_day, SIGNAL(triggered()), m_wid, SLOT(showTipsSLOT()));
  connect(action_Instances, SIGNAL(triggered()), m_wid, SLOT(instancesSLOT()));

  connect(toolBarMenu, SIGNAL(visibilityChanged(bool)),  this, SLOT(setToolbarVisibilitySLOT()));
  connect(toolBarFile, SIGNAL(visibilityChanged(bool)), this, SLOT(setToolbarVisibilitySLOT()));
  connect(toolBarRecorder, SIGNAL(visibilityChanged(bool)), this, SLOT(setToolbarVisibilitySLOT()));
  connect(toolBarDMM, SIGNAL(visibilityChanged(bool)), this, SLOT(setToolbarVisibilitySLOT()));

  connect(m_stateMgr, SIGNAL(instancesChanged(QStringList&)), m_wid, SLOT(instancesChangedSlot(QStringList&)));

}

// Actions that live only in the popup menu are not attached to any widget,
// so their shortcuts would be dead - adding them to the window fixes that.
void MainWin::createExtraActions()
{
  // Ctrl+C is the historical Connect key; Ctrl+D is the one that does not
  // fight the copy reflex.
  action_Connect->setShortcuts({QKeySequence("Ctrl+C"), QKeySequence("Ctrl+D")});

  m_fullScreen = new QAction(tr("&Full screen"), this);
  m_fullScreen->setCheckable(true);
  m_fullScreen->setShortcut(QKeySequence("F11"));
  m_fullScreen->setWhatsThis(tr("<html><head/><body><p><span style=\" font-weight:600;\">Full screen</span></p>"
                                "<p>Use the whole screen for the instruments, e.g. on a lab monitor. F11 again "
                                "returns to the normal window.</p></body></html>"));
  connect(m_fullScreen, &QAction::toggled, this, &MainWin::setFullScreen);

  m_zoomIn = new QAction(tr("Zoom &in"), this);
  m_zoomIn->setShortcuts({QKeySequence::ZoomIn, QKeySequence("Ctrl+=")});
  connect(m_zoomIn, &QAction::triggered, m_wid->graph(), &DMMGraph::zoomInSLOT);
  m_zoomOut = new QAction(tr("Zoom &out"), this);
  m_zoomOut->setShortcut(QKeySequence::ZoomOut);
  connect(m_zoomOut, &QAction::triggered, m_wid->graph(), &DMMGraph::zoomOutSLOT);
  m_zoomFit = new QAction(tr("Show &whole recording"), this);
  m_zoomFit->setShortcut(QKeySequence("Ctrl+0"));
  connect(m_zoomFit, &QAction::triggered, m_wid->graph(), &DMMGraph::zoomFitSLOT);
  m_copyImage = new QAction(tr("Copy graph &image"), this);
  m_copyImage->setShortcut(QKeySequence("Ctrl+Shift+C"));
  m_copyImage->setWhatsThis(tr("<html><head/><body><p><span style=\" font-weight:600;\">Copy graph image</span></p>"
                               "<p>Puts a picture of the recorder graph on the clipboard, ready to paste into a "
                               "report or a chat.</p></body></html>"));
  connect(m_copyImage, &QAction::triggered, m_wid->graph(), &DMMGraph::copyImageSLOT);

  // Space toggles the recorder; a bare key, so only while this window is active
  QAction *toggleRecord = new QAction(this);
  toggleRecord->setShortcut(QKeySequence(Qt::Key_Space));
  connect(toggleRecord, &QAction::triggered, this, &MainWin::toggleRecordingSLOT);

  addActions({action_Configure, action_Direct_help, action_Help, action_Quit, action_Tip_of_the_day,
              m_displayAction, m_meterAction, m_readingsAction, m_titleBars,
              m_fullScreen, m_zoomIn, m_zoomOut, m_zoomFit, m_copyImage, toggleRecord});
}

void MainWin::addShortcutsToToolTips()
{
  for (QAction *a : findChildren<QAction *>())
  {
    if (a->shortcut().isEmpty() || a->isSeparator())
      continue;
    QString tip = a->toolTip();
    if (tip.isEmpty())
      tip = a->text().remove('&');
    a->setToolTip(QString("%1 (%2)").arg(tip, a->shortcut().toString(QKeySequence::NativeText)));
  }
}

void MainWin::toggleRecordingSLOT()
{
  if (m_running)
    action_Stop->trigger();
  else if (action_Start->isEnabled())
    action_Start->trigger();
}

void MainWin::setFullScreen(bool on)
{
  if (on)
    showFullScreen();
  else
    showNormal();
}

void MainWin::startSLOT()
{
  if (m_stateMgr->instances().count()<=1)
  {
    QMetaObject::invokeMethod(m_wid, "startSLOT", Qt::DirectConnection);
    m_localRecord = true;
  }
  else
  {
    QMessageBox question(
      QMessageBox::Question,
      tr("Record DMM data"),
      tr("Multiple instances of QtDMM have been detected.\n"
         "Please choose which instance should record."),
      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    question.button(QMessageBox::Yes)->setText(tr("This instance"));
    question.button(QMessageBox::No)->setText(tr("All instances"));
    question.setEscapeButton(QMessageBox::Cancel);

    switch (question.exec())
    {
      case QMessageBox::Yes:
        QMetaObject::invokeMethod(m_wid, "startSLOT", Qt::DirectConnection);
        m_localRecord = true;
        return;
      case QMessageBox::No:
        m_stateMgr->writeState("RECORD");
        m_localRecord = false;
        return;
    }
  }
}

void MainWin::stopSLOT()
{
  qInfo() << "stop" << m_localRecord;
  if (! m_localRecord)
    m_stateMgr->writeState("STOP");
  m_localRecord = false;

}

void MainWin::runningSLOT(bool on)
{
  m_running = on;
  if (on)
    action_Graph->setChecked(true);   // a recording wants to be seen

  action_Start->setEnabled(!on);
  action_Stop->setEnabled(on);
  action_Print->setEnabled(!on);
  action_Export->setEnabled(!on);
  action_Import->setEnabled(!on);
}

void MainWin::connectSLOT(bool on)
{
  action_Start->setEnabled(on);
  action_Stop->setEnabled(on && m_running);

  if (!on)
    m_running = false;
}

void MainWin::on_action_Help_triggered()
{
  if (!m_helpDlg)
    m_helpDlg = new HelpDlg(m_wid->settings(), this);
  m_helpDlg->show();
  m_helpDlg->raise();
  m_helpDlg->activateWindow();
}

void MainWin::on_action_About_triggered()
{
  QMessageBox about(this);
  about.setWindowTitle(tr("About QtDMM"));
  about.setIconPixmap(QPixmap(":/Symbols/icon.xpm"));
  about.setTextFormat(Qt::RichText);
  about.setText(tr("<h2>QtDMM %1</h2>"
                   "<p>A readout and transient recorder for digital multimeters.</p>"
                   "<p>Built with <b>Qt</b> %2. Licensed under the <b>GNU GPL 3</b> "
                   "(versions before 0.9.0 under GPL 2).</p>"
                   "<p>0.9.5 onwards: tuxmaster and contributors, see the AUTHORS file.<br>"
                   "0.9.3 and before: &copy; 2001-2016 M. Toussaint "
                   "&lt;<a href='mailto:qtdmm@mtoussaint.de'>qtdmm@mtoussaint.de</a>&gt;</p>"
                   "<p>Website: <a href='https://qtdmm.de'>qtdmm.de</a> &middot; "
                   "Contact: <a href='mailto:hello@qtdmm.de'>hello@qtdmm.de</a><br>"
                   "Source and bug reports: <a href='https://github.com/qtdmm/QtDMM'>github.com/qtdmm/QtDMM</a><br>"
                   "Icons (except the DMM icon) are taken from the KDE project.</p>")
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

void MainWin::on_action_Menu_triggered()
{
  if (!m_menu)
  {
    m_menu = new QMenu(this);
    m_menu->addAction(action_Configure);
    m_menu->addAction(action_Graph);
    m_menu->addAction(m_displayAction);
    m_menu->addAction(m_meterAction);
    m_menu->addAction(m_readingsAction);
    m_menu->addMenu(m_arrangeMenu);
    m_menu->addAction(m_fullScreen);
    m_menu->addSeparator();
    m_menu->addAction(m_zoomIn);
    m_menu->addAction(m_zoomOut);
    m_menu->addAction(m_zoomFit);
    m_menu->addAction(m_copyImage);
    m_menu->addSeparator();
    m_menu->addAction(action_Help);
    m_menu->addAction(action_Tip_of_the_day);
    m_menu->addAction(action_Direct_help);
    m_menu->addAction(action_About);
    m_menu->addSeparator();
    m_menu->addAction(action_Quit);
  }

  QWidget* widget = this->toolBarMenu->widgetForAction(this->action_Menu);
  if (widget)
  {
    m_menu->popup(widget->mapToGlobal(
      QPoint( widget->width() - m_menu->sizeHint().width(), widget->height())
    ));
  }
}


void MainWin::closeEvent(QCloseEvent *ev)
{
  setToolbarVisibilitySLOT();
  // dock layout (meter position, floating state, size) and toolbar layout
  saveWindows();
  m_wid->settings()->setInt("ReadingLog/max-rows", m_readings->maxRows());

  if (m_wid->closeWin())
    ev->accept();
  else
    ev->ignore();
}

// ---------------------------------------------------------------- MDI windows

QMdiSubWindow *MainWin::addView(QWidget *view, const QString &title, int role, const QString &name)
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

QAction *MainWin::windowAction(QMdiSubWindow *win, const QString &text, const char *shortcut,
                               const char *icon, const QString &whatsThis)
{
  auto *action = new QAction(QIcon(icon), text, this);
  action->setShortcut(QKeySequence(shortcut));
  action->setWhatsThis(whatsThis);
  bindWindowAction(action, win);
  return action;
}

void MainWin::bindWindowAction(QAction *action, QMdiSubWindow *win)
{
  action->setCheckable(true);
  action->setProperty("window", QVariant::fromValue<QObject *>(win));
  connect(action, &QAction::toggled, win, [this, win](bool on)
  {
    win->setVisible(on);
    if (on)
      m_mdi->setActiveSubWindow(win);
  });
}

// A window was closed with its title bar button, or shown: its action follows.
bool MainWin::eventFilter(QObject *watched, QEvent *event)
{
  if (event->type() == QEvent::Show || event->type() == QEvent::Hide)
    for (QAction *a : { m_displayAction, m_meterAction, m_readingsAction, action_Graph })
      if (a && a->property("window").value<QObject *>() == watched)
      {
        const bool visible = event->type() == QEvent::Show;
        if (a->isChecked() != visible && !m_restoring)
        {
          QSignalBlocker block(a);
          a->setChecked(visible);
        }
      }
  return QMainWindow::eventFilter(watched, event);
}

void MainWin::windowMenu(QMdiSubWindow *win, const QPoint &globalPos)
{
  QMenu menu(this);
  QAction *hide = menu.addAction(tr("&Hide window"));
  QAction *title = menu.addAction(tr("&Title bar"));
  title->setCheckable(true);
  title->setChecked(!MdiArranger::titleBarHidden(win));
  menu.addSeparator();
  menu.addMenu(m_arrangeMenu);
  QAction *chosen = menu.exec(globalPos);
  if (chosen == hide)
    win->hide();
  else if (chosen == title)
    m_arranger->setTitleBarHidden(win, !title->isChecked());
}

void MainWin::syncArrangeActions()
{
  m_arrangeTop->setChecked(m_arranger->mode() == MdiArranger::DisplaysOnTop);
  m_arrangeFree->setChecked(m_arranger->mode() == MdiArranger::Free);
  m_titleBars->setChecked(m_arranger->titleBarsHidden());
}

void MainWin::updateHeaders()
{
  for (ViewFrame *f : { m_displayFrame, m_meterFrame })
  {
    f->setTitle(m_wid->dmmTitle());
    f->setDetail(m_wid->dmmConfigured() ? m_wid->portName() : QString());
  }
}

// Which windows are shown, their order, the mode and - in Free mode - where
// they are. The display's visibility is the old Display/show key.
void MainWin::restoreWindows()
{
  Settings *cfg = m_wid->settings();
  m_restoring = true;
  const QString order = cfg->getString("Windows/order", QString());
  if (!order.isEmpty())
  {
    QList<QMdiSubWindow *> wins;
    for (const QString &name : order.split(','))
      if (auto *w = m_mdi->findChild<QMdiSubWindow *>(name))
        wins << w;
    m_arranger->setOrder(wins);
  }
  const bool free = cfg->getString("Windows/arrange", "top") == "free";
  m_arranger->setMode(free ? MdiArranger::Free : MdiArranger::DisplaysOnTop);
  m_arranger->setTitleBarsHidden(cfg->getBool("Windows/title-bars-hidden", !free));
  if (free)
  {
    // once the window is shown and the area has its size: the automatic
    // layout for a start, then the stored positions on top of it
    QTimer::singleShot(0, this, [this, cfg]
    {
      m_arranger->arrangeNow();
      for (QMdiSubWindow *w : { m_displayWin, m_meterWin, m_graphWin, m_readingsWin })
      {
        const QStringList g = cfg->getString("Windows/geometry-" + w->objectName(), QString()).split(' ');
        if (g.size() == 4)
          w->setGeometry(g[0].toInt(), g[1].toInt(), g[2].toInt(), g[3].toInt());
      }
    });
  }
  m_displayAction->setChecked(cfg->getBool("Display/show", true));
  m_meterAction->setChecked(cfg->getBool("Windows/meter", true));
  action_Graph->setChecked(cfg->getBool("MainWindow/show-graph", true));
  m_readingsAction->setChecked(cfg->getBool("Windows/readings", false));
  // an unchecked action did not toggle: hide its window explicitly
  for (QAction *a : { m_displayAction, m_meterAction, action_Graph, m_readingsAction })
    qobject_cast<QWidget *>(a->property("window").value<QObject *>())->setVisible(a->isChecked());
  m_restoring = false;
  syncArrangeActions();
  updateHeaders();
  m_arranger->arrange();
}

void MainWin::saveWindows()
{
  Settings *cfg = m_wid->settings();
  QStringList order;
  for (QMdiSubWindow *w : m_arranger->order())
    order << w->objectName();
  cfg->setString("Windows/order", order.join(','));
  cfg->setString("Windows/arrange", m_arranger->mode() == MdiArranger::Free ? "free" : "top");
  cfg->setBool("Windows/title-bars-hidden", m_arranger->titleBarsHidden());
  if (m_arranger->mode() == MdiArranger::Free)
    for (QMdiSubWindow *w : { m_displayWin, m_meterWin, m_graphWin, m_readingsWin })
    {
      const QRect g = w->geometry();
      cfg->setString("Windows/geometry-" + w->objectName(),
                     QString("%1 %2 %3 %4").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()));
    }
  cfg->setBool("Windows/meter", m_meterAction->isChecked());
  cfg->setBool("Windows/readings", m_readingsAction->isChecked());
  cfg->setBool("MainWindow/show-graph", action_Graph->isChecked());
}

// Drawn, so it follows the palette (there is no bundled icon for it).
QIcon MainWin::arrangeIcon() const
{
  const QColor fg = palette().color(QPalette::ButtonText);
  QIcon icon;
  for (int sz : { 24, 48 })
  {
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(sz / 24.0, sz / 24.0);
    p.setPen(QPen(fg, 1.6));
    p.drawRect(QRectF(3, 3, 8, 6));
    p.drawRect(QRectF(13, 3, 8, 6));
    p.drawRect(QRectF(3, 11, 18, 10));
    icon.addPixmap(pm);
  }
  return icon;
}

void MainWin::setToolbarVisibilitySLOT()
{
  m_wid->setToolbarVisibility(m_displayAction->isChecked(),
                              toolBarDMM->isVisible(),
                              toolBarRecorder->isVisible(),
                              toolBarFile->isVisible());
}

void MainWin::setConnectSLOT(bool on)
{
  action_Connect->setChecked(on);
}

void MainWin::toolbarVisibilitySLOT(bool disp, bool dmm, bool graph, bool file)
{
  toolBarDMM->setVisible(dmm);
  toolBarRecorder->setVisible(graph);
  toolBarFile->setVisible(file);
  m_displayAction->setChecked(disp);
}

void MainWin::setupIcons()
{
  // theme icons exist on Linux desktops only; Windows and macOS get the
  // bundled ones
  QIcon iconConnectOn = QIcon::fromTheme("network-connect", QIcon(":/Symbols/connect_on.xpm"));
  QIcon iconConnectOff = QIcon::fromTheme("network-disconnect", QIcon(":/Symbols/connect_icon.xpm"));

  this->action_Connect->setIcon(iconConnectOff);
  connect(this->action_Connect, &QAction::toggled, this, [ = ](bool checked)
  {
    this->action_Connect->setIcon(checked ? iconConnectOn : iconConnectOff);
  });
}

void MainWin::bringMainWindowToFront()
{
  QWidget *mainWin = nullptr;
  const auto topWidgets = QApplication::topLevelWidgets();

  for (QWidget *w : topWidgets)
  {
    if (w->inherits("MainWin"))
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
