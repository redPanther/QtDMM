//======================================================================
// File:		mainwid.cpp
// Author:	Matthias Toussaint
// Created:	Tue Apr 10 17:29:01 CEST 2001
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
#include <QPrinter>
#include <iostream>
#include <cmath>

#include "mainwid.h"
#include "dmmgraph.h"
#include "configdlg.h"
#include "dmm.h"
#include "metercontroller.h"
#include "displaywid.h"
#include "meterwid.h"
#include "readinglog.h"
#include "alarm.h"
#include "alarmbar.h"
#include "siprefix.h"
#include "tipdlg.h"
#include "settings.h"
#include "instancesdlg.h"
#include "sharedstatemanager.h"



MainWid::MainWid(QString instance_id, QString config_path, QWidget *parent) :  QFrame(parent),
  m_display(nullptr),
  m_meter(nullptr),
  m_tipDlg(nullptr)
{
  setupUi(this);
  setWindowIcon(QPixmap(":/Symbols/icon.xpm"));

  // the meter session: connection, min/max, alarms, SCPI, external program
  m_ctl = new MeterController(this);
  m_ctl->setInstanceId(instance_id.isEmpty() ? QString("default") : instance_id);

  m_instanceId = instance_id;
  m_settings  = new Settings(instance_id, config_path, this);
  m_configDlg = new ConfigDlg(m_settings, this);
  m_configDlg->hide();
  m_configDlg->readPrinter(&m_printer);

  m_printDlg = new qtdmm::PrintDlg(this);
  m_printDlg->hide();

  m_instancesDlg = new InstancesDlg(m_settings, instance_id, config_path,this);

  connect(m_instancesDlg, SIGNAL(writeState(const QString &)), parent, SLOT(sendStateSLOT(const QString &)));
  connect(this, SIGNAL(sendState(const QString &)), parent, SLOT(sendStateSLOT(const QString &)));
  connect(m_ctl, &MeterController::error, this, &MainWid::error);
  connect(m_ctl, &MeterController::info, this, &MainWid::info);
  connect(m_ctl, &MeterController::sample, ui_graph, &DMMGraph::addValue);
  connect(m_ctl, &MeterController::unitChanged, ui_graph, &DMMGraph::setUnit);
  connect(ui_graph, SIGNAL(info(const QString &)), this, SIGNAL(info(const QString &)));
  connect(ui_graph, SIGNAL(error(const QString &)), this, SIGNAL(error(const QString &)));
  connect(ui_graph, SIGNAL(running(bool)), this, SLOT(runningSLOT(bool)));
  connect(m_configDlg, SIGNAL(accepted()), this, SLOT(applySLOT()));
  // Apply: take the settings over while the dialog stays open. Through a
  // lambda so sender() is not the dialog and applySLOT() does not reconnect
  // the meter, which only happens when the dialog closes.
  connect(m_configDlg, &ConfigDlg::applied, this, [this]() { applySLOT(); });
  connect(m_configDlg, SIGNAL(zoomed()), this, SLOT(zoomedSLOT()));
  connect(m_configDlg, SIGNAL(rejected()), this, SLOT(rejectSLOT()));
  connect(ui_graph, SIGNAL(sampleTime(int)), m_configDlg, SLOT(setSampleTimeSLOT(int)));
  connect(ui_graph, SIGNAL(graphSize(int, int)), m_configDlg, SLOT(setGraphSizeSLOT(int, int)));
  connect(ui_graph, SIGNAL(externalTriggered()), this, SLOT(startExternalSLOT()));
  connect(m_ctl, &MeterController::externalFinished, this, &MainWid::exitedSLOT);
  connect(ui_graph, SIGNAL(configure()), this, SLOT(configSLOT()));
  connect(ui_graph, SIGNAL(exportData()), this, SLOT(exportSLOT()));
  connect(ui_graph, SIGNAL(importData()), this, SLOT(importSLOT()));

  connect(ui_graph, SIGNAL(connectDMM(bool)), this, SIGNAL(connectDMM(bool)));

  connect(ui_graph, SIGNAL(zoomOut(double)), m_configDlg, SLOT(zoomOutSLOT(double)));
  connect(ui_graph, SIGNAL(zoomIn(double)), m_configDlg, SLOT(zoomInSLOT(double)));
  connect(ui_graph, SIGNAL(zoomFit()), m_configDlg, SLOT(zoomFitSLOT()));
  connect(ui_graph, SIGNAL(thresholdChanged(DMMGraph::CursorMode, double)),
          m_configDlg, SLOT(thresholdChangedSLOT(DMMGraph::CursorMode, double)));

  ui_graph->setSettings(m_settings);

  m_settings->save();
  Q_EMIT sendState("UPDATE_INSTANCES_"+QString::number(QDateTime::currentMSecsSinceEpoch()));

  // alarms: the controller judges every reading and runs the program; the
  // banner above the graph, beep, popup and raising the window are UI
  m_alarmBar = new AlarmBar(this);
  if (auto *box = qobject_cast<QBoxLayout *>(layout()))
    box->insertWidget(0, m_alarmBar);
  connect(m_ctl, &MeterController::alarmRaised, this, &MainWid::alarmRaised);
  connect(m_ctl, &MeterController::alarmBannerChanged, m_alarmBar, &AlarmBar::setAlarms);
  connect(m_alarmBar, &AlarmBar::acknowledged, m_ctl, &MeterController::acknowledgeAlarms);
  connect(m_ctl, &MeterController::recordingRequested, this, [this](bool start)
  {
    if (start)
      startSLOT();
    else
      stopSLOT();
  });
  connect(m_ctl, &MeterController::markRequested, this, [this](const QColor &color, const QString &name, bool graph, bool)
  {
    if (graph)
      ui_graph->addMark(color, name);
  });
  m_ctl->setAlarms(m_configDlg->alarms());

  // SCPI server: the meter as a network instrument (applyScpi() starts it)
  connect(m_ctl, &MeterController::connectRequested, this, [this](bool on)
  {
    Q_EMIT setConnect(on);
    Q_EMIT connectDMM(on);
    connectSLOT(on);
  });
  connect(m_ctl, &MeterController::scpiStatusChanged, this, [this](const QString &status, const QString &detail)
  {
    Q_EMIT scpiStatus(status);
    m_configDlg->setScpiStatus(detail);
  });
  // HCOPy:SDUMp:DATA? - the main window as the "instrument screen"
  m_ctl->setScreenshotSource([this](const QByteArray &format) -> QByteArray
  {
    QWidget *top = window() ? window() : this;
    const QPixmap shot = top->grab();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!shot.save(&buffer, format.constData()))
      return {};
    return bytes;
  });

  if (m_configDlg->showTip())
    showTipsSLOT();
}

void MainWid::setConsoleLogging(bool on)
{
  m_ctl->dmm()->setConsoleLogging(on);
}

void MainWid::setDisplay(DisplayWid *display)
{
  m_display = display;
  connect(m_ctl, &MeterController::reading, display, &DisplayWid::showReading);
  connect(m_ctl, &MeterController::minimumChanged, display, &DisplayWid::showMinimum);
  connect(m_ctl, &MeterController::maximumChanged, display, &DisplayWid::showMaximum);
  connect(m_ctl, &MeterController::minMaxReset, display, &DisplayWid::clearMinMax);
}

void MainWid::setMeter(MeterWid *meter)
{
  m_meter = meter;
  connect(m_ctl, &MeterController::reading, meter, &MeterWid::showReading);
  connect(m_ctl, &MeterController::minimumChanged, meter, &MeterWid::showMinimum);
  connect(m_ctl, &MeterController::maximumChanged, meter, &MeterWid::showMaximum);
  connect(m_ctl, &MeterController::minMaxReset, meter, &MeterWid::clearMinMax);
}

void MainWid::setReadingLog(ReadingLog *log)
{
  connect(m_ctl, &MeterController::reading, log, &ReadingLog::appendReading);
  connect(m_ctl, &MeterController::markRequested, log, [log](const QColor &color, const QString &name, bool, bool table)
  {
    if (table)
      log->markLast(color, name);
  });
}

bool MainWid::closeWin()
{
  m_ctl->connectMeter(false);
  m_configDlg->setWinRect(parentRect());
  m_configDlg->on_ui_buttonBox_accepted();

  Q_EMIT setConnect(false);

  if (ui_graph->dirty() && m_configDlg->alertUnsavedData())
  {
    QMessageBox question;
    question.setWindowTitle(tr("QtDMM: Unsaved data"));
    question.setText(tr("<font size=+2><b>Unsaved data</b></font><p>"
                        "You still have unsaved measured data in memory."
                        " If you quit now it will be lost."
                        "<p>Do you want to export your unsaved data first?"));
    question.setIcon(QMessageBox::Information);
    question.setIconPixmap(QPixmap(":/Symbols/icon.xpm"));

    // Set standard buttons
    question.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    question.setDefaultButton(QMessageBox::Yes);
    question.setEscapeButton(QMessageBox::Cancel);

    // Set custom button texts
    QAbstractButton *yesButton = question.button(QMessageBox::Yes);
    if (yesButton)
      yesButton->setText(tr("Export data first"));

    QAbstractButton *noButton = question.button(QMessageBox::No);
    if (noButton)
      noButton->setText(tr("Quit without saving"));

    switch (question.exec())
    {
      case QMessageBox::Yes:
        return ui_graph->exportDataSLOT();

      case QMessageBox::No:
        break;

      case QMessageBox::Cancel:
        return false;
    }
  }

  return true;
}

QRect MainWid::winRect() const
{
  return m_configDlg->winRect();
}

bool MainWid::saveWindowPosition() const
{
  return m_configDlg->saveWindowPosition();
}

bool MainWid::saveWindowSize() const
{
  return m_configDlg->saveWindowSize();
}

QRect MainWid::parentRect() const
{
  QRect fRect = parentWidget()->frameGeometry();
  QRect rect  = parentWidget()->rect();

  return QRect(fRect.x(), fRect.y(), rect.width(), rect.height());
}

void MainWid::setStateManager(SharedStateManager *mgr)
{
  m_ctl->setStateManager(mgr);
  m_configDlg->setStateManager(mgr);
  m_instancesDlg->setStateManager(mgr);
}

void MainWid::resetSLOT()
{
  m_ctl->resetMinMax();
}

void MainWid::connectSLOT(bool on)
{
  if (on)
  {
    if (m_ctl->connectMeter(true))
      ui_graph->clearSLOT();
    else
      Q_EMIT setConnect(false);   // the port could not be opened: button back to "off"
  }
  else
  {
    m_ctl->connectMeter(false);
    ui_graph->stopSLOT();
  }

  m_configDlg->connectSLOT(on);
  ui_graph->connectSLOT(on);
}

void MainWid::quitSLOT()
{
  if (closeWin()) qApp->quit();
}

void MainWid::helpSLOT()
{
  QWhatsThis::enterWhatsThisMode();
}

void MainWid::configSLOT()
{
  Q_EMIT setConnect(false);
  Q_EMIT connectDMM(false);
  connectSLOT(false);

  m_configDlg->show();
  m_configDlg->raise();
}

void MainWid::configDmmSLOT()
{
  configSLOT();
  m_configDlg->showPage(ConfigDlg::DMM);
}

void MainWid::configRecorderSLOT()
{
  configSLOT();
  m_configDlg->showPage(ConfigDlg::Recorder);
}

void MainWid::rejectSLOT()
{
  if ((sender() == m_configDlg))
  {
    Q_EMIT setConnect(true);
    Q_EMIT connectDMM(true);
    connectSLOT(true);
  }
}

void MainWid::applySLOT()
{
  readConfig();
  m_ctl->setAlarms(m_configDlg->alarms());
  ui_graph->setAlertUnsaved(m_configDlg->alertUnsavedData());
  m_ctl->setModel(m_configDlg->dmmName());
  ScpiConfig scpi;
  scpi.enabled = m_configDlg->scpiEnabled();
  scpi.allInterfaces = m_configDlg->scpiAllInterfaces();
  scpi.port = quint16(m_configDlg->scpiPort());
  scpi.mdns = m_configDlg->scpiMdns();
  m_ctl->applyScpi(scpi);
  Q_EMIT configChanged();

  if ((sender() == m_configDlg))
  {
    Q_EMIT setConnect(true);
    Q_EMIT connectDMM(true);
    connectSLOT(true);
  }
}

void MainWid::zoomedSLOT()
{
  ui_graph->setGraphSize(m_configDlg->windowSeconds(), m_configDlg->totalSeconds());
}

void MainWid::exportSLOT()
{
  ui_graph->exportDataSLOT();
}

void MainWid::importSLOT()
{
  ui_graph->importDataSLOT();
}

void MainWid::printSLOT()
{
  m_printDlg->setPrinter(&m_printer);

  if (m_printDlg->exec())
  {
    m_configDlg->writePrinter(&m_printer);
    ui_graph->print(&m_printer, m_printDlg->title(), m_printDlg->comment());
  }
}

void MainWid::clearSLOT()
{
  ui_graph->clearSLOT();
}

void MainWid::startSLOT()
{
  ui_graph->startSLOT();
}

void MainWid::stopSLOT()
{
  ui_graph->stopSLOT();
}

void MainWid::readConfig()
{
  DMM *dmm = m_ctl->dmm();
  bool reopen = false;

  if (dmm->isOpen())
  {
    dmm->close();
    reopen = true;
  }

  dmm->setDmmInfo(m_configDlg->dmmInfo());
  dmm->setDevice(m_configDlg->device());
  dmm->setSpeed(m_configDlg->speed());
  dmm->setFormat(m_configDlg->format());
  dmm->setPortSettings(static_cast<QSerialPort::DataBits>(m_configDlg->bits()), static_cast<QSerialPort::StopBits>(m_configDlg->stopBits()),
                         m_configDlg->parity(), m_configDlg->externalSetup(), m_configDlg->rts(), m_configDlg->dtr() );

  ui_graph->setGraphSize(m_configDlg->windowSeconds(), m_configDlg->totalSeconds());
  ui_graph->setStartTime(m_configDlg->startTime());
  ui_graph->setMode(m_configDlg->sampleMode());

  ui_graph->setSampleTime(m_configDlg->sampleStep());
  ui_graph->setSampleLength(m_configDlg->sampleLength());

  ui_graph->setCrosshair(m_configDlg->crosshair());

  ui_graph->setThresholds(m_configDlg->fallingThreshold(),
                          m_configDlg->raisingThreshold());

  ui_graph->setScale(m_configDlg->automaticScale(),
                     m_configDlg->includeZero(),
                     m_configDlg->scaleMin(),
                     m_configDlg->scaleMax());

  ui_graph->setColors(m_configDlg->bgColor(),
                      m_configDlg->gridColor(),
                      m_configDlg->dataColor(),
                      m_configDlg->cursorColor(),
                      m_configDlg->startColor(),
                      m_configDlg->externalColor(),
                      m_configDlg->intColor(),
                      m_configDlg->intThresholdColor());

  ui_graph->setExternal(m_configDlg->startExternal(),
                        m_configDlg->externalFalling(),
                        m_configDlg->externalThreshold());

  ui_graph->setLineStyle(m_configDlg->lineMode(),
                         m_configDlg->pointMode(),
                         m_configDlg->intLineMode(),
                         m_configDlg->intPointMode());

  m_display->setFaceColor(m_configDlg->displayBgColor());
  m_display->setDisplayMode(m_configDlg->display(), m_configDlg->showMinMax(),
                            m_configDlg->showBar(), m_configDlg->numValues());
  dmm->setNumValues(m_configDlg->numValues());

  if (m_meter)
  {
    m_meter->setDisplayCounts(m_configDlg->display());
    MeterStyle style = m_configDlg->meterStyle() == 1 ? MeterStyle::ivory() : MeterStyle::dark();
    style.ballistics = m_configDlg->meterBallistics();
    style.redZoneFrom = m_configDlg->meterRedZone() / 100.0;
    m_meter->setStyle(style);
    m_meter->setScaleMode(static_cast<MeterWid::ScaleMode>(
      m_configDlg->meterScaleMode() == 1 ? MeterWid::Unipolar :
      m_configDlg->meterScaleMode() == 2 ? MeterWid::Bipolar : MeterWid::Auto));
  }

  ui_graph->setLine(m_configDlg->lineWidth(), m_configDlg->intLineWidth());

  ui_graph->setIntegration(m_configDlg->showIntegration(),
                           m_configDlg->intScale(),
                           m_configDlg->intThreshold(),
                           m_configDlg->intOffset());

  if (m_configDlg->sampleMode() == DMMGraph::Time)
    Q_EMIT info(tr("Automatic start at %1").arg(m_configDlg->startTime().toString()));
  else if (m_configDlg->sampleMode() == DMMGraph::Raising)
    Q_EMIT info(tr("Raising threshold %1").arg(m_configDlg->raisingThreshold()));
  else if (m_configDlg->sampleMode() == DMMGraph::Falling)
    Q_EMIT info(tr("Falling threshold %1").arg(m_configDlg->fallingThreshold()));
  Q_EMIT useTextLabel(m_configDlg->useTextLabel());
  Q_EMIT toolbarVisibility(m_configDlg->showDisplay(),
                           m_configDlg->showDmmToolbar(),
                           m_configDlg->showGraphToolbar(),
                           m_configDlg->showFileToolbar());

  if (reopen)
    dmm->open();
}

void MainWid::runningSLOT(bool on)
{
  m_ctl->setRecording(on);
  Q_EMIT running(on);
}

void MainWid::startExternalSLOT()
{
  const QString command = m_configDlg->externalCommand();
  if (m_ctl->externalRunning())
  {
    QMessageBox question;
    question.setWindowTitle(tr("QtDMM: Launch error"));
    question.setText(tr("<font size=+2><b>Launch error</b></font><p>"
                        "Application %1 is still running!<p>"
                        "Do you want to kill it now?")
                     .arg(command));
    question.setIcon(QMessageBox::Information);
    question.setIconPixmap(QPixmap(":/Symbols/icon.xpm"));

    question.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    question.setDefaultButton(QMessageBox::Yes);

    QAbstractButton *yesButton = question.button(QMessageBox::Yes);
    if (yesButton)
      yesButton->setText(tr("Yes, kill it!"));

    QAbstractButton *noButton = question.button(QMessageBox::No);
    if (noButton)
      noButton->setText(tr("No, keep running"));

    if (question.exec() != QMessageBox::Yes)
      return;
    m_ctl->killExternal();
  }

  if (m_configDlg->disconnectExternal())
    Q_EMIT setConnect(false);

  if (!m_ctl->startExternal(command))
  {
    QMessageBox question;
    question.setWindowTitle(tr("QtDMM: Launch error"));
    question.setText(tr("<font size=+2><b>Launch error</b></font><p>"
                        "Couldn't launch %1").arg(command));
    question.setIcon(QMessageBox::Information);
    question.setIconPixmap(QPixmap(":/Symbols/icon.xpm"));

    // Nur ein "OK"-Button mit benutzerdefiniertem Text
    question.setStandardButtons(QMessageBox::Yes);
    question.setDefaultButton(QMessageBox::Yes);

    QAbstractButton *yesButton = question.button(QMessageBox::Yes);
    if (yesButton)
      yesButton->setText(tr("Bummer!"));


    question.exec();
  }
  else
    Q_EMIT error(tr("Launched %1").arg(command));
}

void MainWid::exitedSLOT(int exitStatus)
{
  Q_EMIT error(tr("%1 terminated with exit code %2.").arg(m_configDlg->externalCommand()).arg(exitStatus));
}

void MainWid::showTipsSLOT()
{
  if (!m_tipDlg)
  {
    m_tipDlg = new TipDlg(this);

    m_tipDlg->setShowTipsSLOT(m_configDlg->showTip());
    m_tipDlg->setCurrentTip(m_configDlg->currentTipId());

    connect(m_tipDlg, SIGNAL(showTips(bool)), m_configDlg, SLOT(setShowTipsSLOT(bool)));
    connect(m_configDlg, SIGNAL(showTips(bool)), m_tipDlg, SLOT(setShowTipsSLOT(bool)));
    connect(m_tipDlg, SIGNAL(currentTip(int)), m_configDlg, SLOT(setCurrentTipSLOT(int)));
  }

  m_tipDlg->show();
}

void MainWid::setGraphVisible(bool on)
{
  ui_graph->setVisible(on);
  // the graph is all this frame shows; without it collapse the frame so the
  // panels get the space and no empty strip remains
  setFrameShape(on ? QFrame::StyledPanel : QFrame::NoFrame);
  setMaximumHeight(on ? QWIDGETSIZE_MAX : 0);
  m_settings->setBool("MainWindow/show-graph", on);
  m_settings->save();
}

bool MainWid::graphVisible() const
{
  return m_settings->getBool("MainWindow/show-graph", false);
}

bool MainWid::dmmConfigured() const
{
  // DMM/configured is set when the settings dialog is confirmed with OK (or
  // by the instances dialog); the model check keeps configs from before
  // that key working. "Manual" alone proves nothing: applySLOT() writes it
  // at every exit, dialog or not.
  if (m_settings->getBool("DMM/configured", false))
    return true;
  const QString model = m_settings->getString("DMM/model");
  return !model.isEmpty() && model != "Manual";
}

QString MainWid::dmmTitle() const
{
  if (!dmmConfigured())
    return tr("no meter configured");
  const QString model = m_configDlg->dmmName().trimmed();
  if (!model.isEmpty())
    return model;
  return m_configDlg->device().trimmed();
}

void MainWid::setToolbarVisibility(bool disp, bool dmm, bool graph, bool file)
{
  m_configDlg->setToolbarVisibility(disp, dmm, graph, file);
}

void MainWid::instancesSLOT()
{
  m_instancesDlg->show();
}

void MainWid::instancesChangedSlot(QStringList& instances)
{
  m_instancesDlg->setInstancesOnline(instances);
}

// ---------------------------------------------------------------- alarms

// The controller has reported the alarm and run its program; what is left
// needs the desktop: beep, raise the window, pop up a box.
void MainWid::alarmRaised(const Alarm &alarm, const QString &shown, const QString &text)
{
  if (alarm.beep)
    QApplication::beep();
  if (alarm.raiseWindow && window())
  {
    window()->raise();
    window()->activateWindow();
    QApplication::alert(window());
  }
  if (alarm.popup)
  {
    auto *box = new QMessageBox(QMessageBox::Warning, tr("QtDMM alarm: %1").arg(alarm.name),
                                QString("%1\n%2   %3").arg(text, shown, QDateTime::currentDateTime().toString("HH:mm:ss")),
                                QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(false);
    box->show();
  }
}
