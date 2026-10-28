//======================================================================
// File:		mainwid.cpp
// Author:	Matthias Toussaint
// Created:	Tue Apr 10 17:29:01 CEST 2001
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
#include <QPrinter>
#include <iostream>
#include <cmath>

#include "ui/dialogs/recordlengthdlg.h"
#include "ui/alarmsound.h"
#include "ui/instancewidget.h"
#include "device/transports/serial.h"
#include "core/devicelibrary.h"
#include "device/dmmdecoder.h"
#include "device/protocols.h"
#include "ui/controlbar.h"
#include "ui/frontpanel.h"
#include "ui/views/graphwidget.h"
#include "ui/settings/settingsdialog.h"
#include "ui/devicesettings.h"
#include "ui/settings/alarmprefs.h"
#include "ui/settings/generalprefs.h"
#include "ui/settings/graphprefs.h"
#include "ui/settings/guiprefs.h"
#include "ui/settings/keyboardprefs.h"
#include "ui/settings/recorderprefs.h"
#include "ui/settings/scpiprefs.h"
#include "device/meterconnection.h"
#include "service/metercontroller.h"
#include "ui/views/lcdwidget.h"
#include "ui/views/analogmeter.h"
#include "ui/views/poincareplot.h"
#include "recording/recordingstore.h"
#include "recording/readingsmodel.h"
#include "core/alarm.h"
#include "core/siprefix.h"
#include "core/settings.h"
#include "service/devicesession.h"
#include "service/valuefeed.h"



InstanceWidget::InstanceWidget(QString instanceId, QString configPath, DeviceSession *session, QWidget *parent) :  QFrame(parent),
  m_display(nullptr),
  m_meter(nullptr)
{
  setupUi(this);
  setWindowIcon(QPixmap(":/Symbols/icon.xpm"));

  // the meter session: connection, min/max, alarms, SCPI
  m_session = session;
  m_ctl = new MeterController(this, session->host());
  session->adopt(m_ctl);
  // the views' values: of the window's meter or of another one open here
  m_meterFeed = new ValueFeed(session, this);
  m_poincareFeed = new ValueFeed(session, this);
  m_meterFeed->setValue(ValueRef());
  m_poincareFeed->setValue(ValueRef());
  connect(session, &DeviceSession::error, this, &InstanceWidget::error);
  connect(session, &DeviceSession::info, this, &InstanceWidget::info);
  m_ctl->setInstanceId(instanceId.isEmpty() ? QString("default") : instanceId);

  m_instanceId = instanceId;
  m_configPath = configPath;
  m_settings  = new Settings(instanceId, configPath, this);
  // the integral is over time since 26.2: its scale once per settings file
  GraphWidget::migrateIntegralScale(m_settings);
  // "My devices", one file for all instances next to their settings. What
  // an entry keeps depends on how its model connects; the model table is
  // not in QtCore
  DeviceLibrary::setModelTransport([](const QString &model) -> QString
  {
    for (const DmmDecoder::DMMInfo &cfg : DmmDecoder::getDeviceConfigurations())
      if (DmmDecoder::sameModel(model, cfg.name))
      {
        // baud 0: the Bluetooth entry of a protocol that also has a cable
        const ProtocolInfo *info = protocolInfo(cfg.protocol);
        if (cfg.baud != 0 || !info || QLatin1String(info->transport) != QLatin1String("Bluetooth LE"))
          return QString();
        return cfg.protocol == FrameFormat::VictronBLE ? QStringLiteral("ble") : QStringLiteral("blegatt");
      }
    return QString();
  });
  m_devices = new DeviceLibrary(m_settings->configDir(), this);
  m_configDlg = new SettingsDialog(m_settings, this);
  m_configDlg->hide();
  m_configDlg->readPrinter(&m_printer);

  m_printDlg = new qtdmm::PrintDlg(this);
  m_printDlg->hide();

  connect(m_ctl, &MeterController::error, this, &InstanceWidget::error);
  connect(m_ctl, &MeterController::info, this, &InstanceWidget::info);
  // the recorder belongs to the controller; the graph shows it, and the
  // values of meters open beside it from their stores
  ui_graph->setStore(m_ctl->recorder());
  ui_graph->setOtherStores([this](const QString &source) -> RecordingStore *
  {
    MeterController *ctl = m_session->controller(source);
    return ctl && ctl != m_ctl ? ctl->recorder() : nullptr;
  },
  [this](const QString &source)
  {
    const MeterController *ctl = m_session->controller(source);
    return ctl ? ctl->deviceName() : QString();
  });
  connect(m_session, &DeviceSession::opened, ui_graph, &GraphWidget::otherStoresChanged);
  connect(m_session, &DeviceSession::closed, ui_graph, &GraphWidget::otherStoresChanged);
  connect(m_ctl, &MeterController::unitChanged, ui_graph, &GraphWidget::setUnit);
  // what each value measures, for the views' choice of value
  connect(m_ctl, &MeterController::reading, this, [this](const Reading &r)
  {
    if (r.id >= m_seenValues)
    {
      const int before = numValues();
      m_seenValues = r.id + 1;
      if (numValues() != before)
        applyValueCount();
    }
    m_lastText[r.id] = r.overload ? QStringLiteral("OL")
                       : r.noNumber() ? r.text.trimmed()
                                      : QString("%1 %2").arg(r.text, SiPrefix::displayText(r.unit)).trimmed();
    if (r.text.isEmpty() || r.overload || r.noNumber() || r.port.quantity == Quantity::Unknown)
      return;
    const bool news = m_lastReading.value(r.id).baseUnit != r.baseUnit || m_lastReading.value(r.id).name != r.name
                      || !m_lastReading.contains(r.id);
    m_lastReading[r.id] = r;
    // the alarms on another value: their thresholds in its unit
    if (news && r.id > 0)
      m_configDlg->alarmPage()->setValueUnit(r.id, r.baseUnit);
    if (news)
      updateValueNames();
  });
  // the alarm thresholds are in the reading's base unit, also while the
  // (modeless) dialog is open
  connect(m_ctl, &MeterController::baseUnitChanged, m_configDlg->alarmPage(), &AlarmPrefs::setUnit);
  connect(ui_graph, &GraphWidget::info, this, &InstanceWidget::info);
  connect(ui_graph, &GraphWidget::error, this, &InstanceWidget::error);
  connect(ui_graph, &GraphWidget::running, this, &InstanceWidget::runningSLOT);
  connect(ui_graph, &GraphWidget::stateChanged, this, &InstanceWidget::recorderState);
  connect(ui_graph, &GraphWidget::liveRequested, this, &InstanceWidget::liveSLOT);
  // the graph runs from the start: Live, until a recording
  m_ctl->recorder()->live();
  // OK and Apply take the settings over; the meter stays as it is - the
  // dialog has no meter page, a meter is changed through takeOver()
  connect(m_configDlg, &SettingsDialog::accepted, this, [this]() { applySLOT(); });
  connect(m_configDlg, &SettingsDialog::applied, this, [this]() { applySLOT(); });
  connect(m_configDlg, &SettingsDialog::zoomed, this, &InstanceWidget::zoomedSLOT);
  connect(ui_graph, &GraphWidget::windowRequested, m_configDlg, &SettingsDialog::setWindowSecondsSLOT);
  connect(ui_graph, &GraphWidget::sampleTime, m_configDlg, &SettingsDialog::setSampleTimeSLOT);
  // this graph's own colours from its context menu (empty = the default
  // from the settings page, applied in readConfig())
  connect(ui_graph, &GraphWidget::colorVariantChanged, this, [this](int v)
  {
    m_settings->setString("Windows/graph-variant",
                          v < 0 ? QString() : GraphWidget::variantName(static_cast<GraphWidget::ColorVariant>(v)));
  });
  connect(ui_graph, &GraphWidget::configure, this, [this] { openConfig(SettingsDialog::Graph); });
  connect(ui_graph, &GraphWidget::clearRequested, this, &InstanceWidget::clearSLOT);
  connect(ui_graph, &GraphWidget::exportData, this, &InstanceWidget::exportSLOT);
  connect(ui_graph, &GraphWidget::importData, this, &InstanceWidget::importSLOT);

  connect(ui_graph, &GraphWidget::connectDMM, this, &InstanceWidget::connectDMM);

  connect(ui_graph, &GraphWidget::zoomOut, m_configDlg, &SettingsDialog::zoomOutSLOT);
  connect(ui_graph, &GraphWidget::zoomIn, m_configDlg, &SettingsDialog::zoomInSLOT);
  connect(ui_graph, &GraphWidget::thresholdChanged, m_configDlg, &SettingsDialog::thresholdChangedSLOT);

  ui_graph->setSettings(m_settings);

  m_settings->save();

  // alarms: the controller judges every reading and runs the program; sound,
  // popup and raising the window are UI (the banner is MainWindow's). A
  // sound goes on while its alarm is raised and not acknowledged.
  m_alarmSound = new AlarmSound([this](int index) { return m_ctl->alarms()->state(index) == AlarmManager::Raised; },
                                this);
  connect(m_ctl, &MeterController::alarmRaised, this, &InstanceWidget::alarmRaised);
  connect(m_ctl, &MeterController::alarmCleared, this, [this](int, const Alarm &alarm)
  {
    if (alarm.clearSound)
      m_alarmSound->play(AlarmSound::clearedSound());
  });
  // the limit test: the verdict to the display, green or red under an
  // alarm's colour, a sound when it turns
  m_limitSound = new AlarmSound([this](int verdict) { return m_ctl->verdict() == verdict; }, this);
  connect(m_ctl, &MeterController::limitJudged, this, [this](int id, LimitTest::Verdict verdict, double deviation)
  {
    // the display only for the value it shows big
    if (m_display)
      m_display->setVerdict(id == m_display->primaryShown() ? verdict : LimitTest::NoVerdict, deviation);
    const LimitTest &test = m_ctl->limitTest();
    const QColor color = !test.tint || verdict == LimitTest::NoVerdict ? QColor()
                         : verdict == LimitTest::Pass ? QColor(0x3c, 0xb0, 0x4a) : QColor(0xd8, 0x22, 0x22);
    if (color != m_limitColor)
    {
      m_limitColor = color;
      showAlertColor();
    }
    // a new verdict: the old one's sound stops, the new one's starts
    // (repeated while the verdict stays, if wanted)
    if (verdict != m_lastVerdict)
    {
      m_limitSound->stopAll();
      Alarm sound;
      sound.sound = verdict == LimitTest::Pass ? test.passSound : verdict == LimitTest::High ? test.highSound
                    : verdict == LimitTest::Low ? test.lowSound : QString();
      const bool repeat = verdict == LimitTest::High ? test.highRepeat : verdict == LimitTest::Low ? test.lowRepeat
                          : verdict == LimitTest::Pass && test.passRepeat;
      sound.repeat = repeat ? Alarm::WhileRaised : Alarm::Once;
      m_limitSound->start(verdict, sound);
    }
    m_lastVerdict = verdict;
  });
  m_ctl->setLimitTest(LimitTest::fromJsonText(m_settings->getString("Limits/test")));
  applyLimitBands();
  // a new limit test, from the dialog or a SCPI client: kept, and shown
  connect(m_ctl, &MeterController::limitTestChanged, this, [this](const LimitTest &test)
  {
    m_settings->setString("Limits/test", test.toJsonText());
    m_lastVerdict = LimitTest::NoVerdict;
    if (m_display)
      m_display->setLimitTest(test.enabled);
    applyLimitBands();
  });
  // the display and the meter in the alarm's colour, blinking if wanted;
  // the banner and the beep stay, the colour is never the only sign
  m_alertTimer = new QTimer(this);
  m_alertTimer->setInterval(500);
  connect(m_alertTimer, &QTimer::timeout, this, [this]
  {
    m_alertPhase = !m_alertPhase;
    showAlertColor();
  });
  connect(m_ctl, &MeterController::alertColorChanged, this, [this](const QColor &color)
  {
    m_alertColor = color;
    m_alertPhase = true;
    showAlertColor();
  });
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
  m_ctl->setAlarms(m_configDlg->alarmPage()->alarms());

  // SCPI server: the meter as a network instrument (applyScpi() starts it)
  connect(m_ctl, &MeterController::connectRequested, this, [this](bool on)
  {
    Q_EMIT setConnect(on);
    Q_EMIT connectDMM(on);
    connectSLOT(on);
  });
  // an alarm's program has the port for a while: the recording runs on
  connect(m_ctl, &MeterController::portReleased, this, [this](bool released)
  {
    Q_EMIT setConnect(!released);
    Q_EMIT connectDMM(!released);
    connectPort(!released);
  });
  connect(m_ctl, &MeterController::scpiStatusChanged, this, [this](const QString &detail)
  {
    m_configDlg->scpiPage()->setStatus(detail);
    Q_EMIT servicesChanged();
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
}

void InstanceWidget::setConsoleLogging(bool on)
{
  m_ctl->dmm()->setConsoleLogging(on);
}

void InstanceWidget::setPanel(FrontPanel *panel)
{
  m_panel = panel;
  m_display = panel->display();
  // the LCD's colours: backlight blue unless chosen otherwise; a config
  // from before the variants with a tint of its own lands on Custom
  QString lcd = m_settings->getString("Display/lcd");
  if (lcd.isEmpty())
    lcd = m_settings->getColor("Display/display-background", LcdWidget::classicFace()) == LcdWidget::classicFace()
            ? "blue" : "custom";
  m_display->setLcdVariant(LcdWidget::lcdVariantFromName(lcd));
  m_display->setLimitTest(m_ctl->limitTest().enabled);
  applyValueChoice();
}

void InstanceWidget::setMeter(AnalogMeter *meter)
{
  m_meter = meter;
  connect(m_meterFeed, &ValueFeed::reading, meter, &AnalogMeter::showReading);
  connect(m_meterFeed, &ValueFeed::staleChanged, meter, &AnalogMeter::setStale);
  // the memory of the value it shows, whichever the LCD follows
  connect(m_meterFeed, &ValueFeed::valueMinMaxChanged, meter, &AnalogMeter::showValueMinMax);
  connect(m_meterFeed, &ValueFeed::controllerChanged, meter, [meter]
  {
    meter->reset();
    meter->clearMinMax();
  });
  applyValueChoice();
  applyLimitBands();
}

void InstanceWidget::setPoincare(PoincarePlot *plot)
{
  m_poincare = plot;
  connect(m_poincareFeed, &ValueFeed::reading, plot, &PoincarePlot::addReading);
  connect(m_poincareFeed, &ValueFeed::staleChanged, plot, &PoincarePlot::setStale);
  connect(m_poincareFeed, &ValueFeed::controllerChanged, plot, &PoincarePlot::clear);
  applyValueChoice();
}

void InstanceWidget::setLcdValue(const ValueRef &value)
{
  m_lcdValue = value;
  applyValueChoice();
}

void InstanceWidget::setLcdSecondaries(const std::optional<QList<int>> &ids)
{
  m_lcdSecondaries = ids;
  if (m_display)
    m_display->setSecondaries(ids);
}

QList<int> InstanceWidget::lcdSecondaries() const
{
  return m_display ? m_display->secondaries() : m_lcdSecondaries.value_or(QList<int>());
}

void InstanceWidget::setMeterValue(const ValueRef &value)
{
  m_meterValue = value;
  applyValueChoice();
}

void InstanceWidget::setPoincareValue(const ValueRef &value)
{
  m_poincareValue = value;
  applyValueChoice();
}

bool InstanceWidget::ownMeter(const ValueRef &value) const
{
  return value.source.isEmpty() || value.source == m_ctl->source();
}

int InstanceWidget::ownValue(const ValueRef &value) const
{
  return ownMeter(value) && value.id >= 0 && value.id < numValues() ? value.id : 0;
}

int InstanceWidget::numValues() const
{
  // a value the table does not know of counts as well (a meter set up by hand)
  return qBound(1, qMax(m_configDlg->meterSettings()->valueCount(), m_seenValues), kMaxValues);
}

QString InstanceWidget::valueName(int id) const
{
  const auto it = m_lastReading.constFind(id);
  if (it == m_lastReading.cend())
    return tr("Value %1").arg(id + 1);
  if (!it->name.isEmpty())
    return it->name;
  return RecordingStore::describe(it->port, it->baseUnit);
}

QString InstanceWidget::valueShortName(int id) const
{
  const auto it = m_lastReading.constFind(id);
  if (it == m_lastReading.cend() || !it->name.isEmpty())
    return valueName(id);
  const QString unit = SiPrefix::displayText(it->baseUnit);
  if (unit.isEmpty())
    return valueName(id);
  return QString("%1 %2").arg(unit, couplingText(it->port.defining)).trimmed();
}

void InstanceWidget::applyValueCount()
{
  if (m_display)
    m_display->setValueCount(numValues());
  m_ctl->setValueCount(numValues());
  applyValueChoice();
}

void InstanceWidget::applyValueChoice()
{
  updateValueNames();
  // the window's meter by an empty source: it stays the window's when
  // the window switches to another device; another meter's value as it is
  auto feedValue = [this](const ValueRef &value)
  {
    return ownMeter(value) ? ValueRef(ownValue(value)) : value;
  };
  if (m_panel)
    m_panel->setValue(feedValue(m_lcdValue));
  m_meterFeed->setValue(feedValue(m_meterValue));
  m_poincareFeed->setValue(feedValue(m_poincareValue));
  m_ctl->setMinMaxValue(ownValue(m_lcdValue));
  if (m_meter)
  {
    const int id = m_meterFeed->value().id;
    m_meter->setValueId(id);
    if (const MeterController *ctl = m_meterFeed->controller(); ctl && !ctl->valueMinMax(id).isEmpty())
      m_meter->showValueMinMax(id, ctl->valueMinMax(id).minimum(), ctl->valueMinMax(id).maximum());
  }
  if (m_poincare)
    m_poincare->setValueId(m_poincareFeed->value().id);
}

void InstanceWidget::setReadingLog(ReadingsModel *log)
{
  // the table shows the recorder's readings series: the controller feeds it
  // whether or not a table exists
  m_readingLog = log;
  log->setStore(m_ctl->recorder());
  updateValueNames();
  connect(m_ctl, &MeterController::markRequested, log, [log](const QColor &color, const QString &name, bool, bool table)
  {
    if (table)
      log->markLast(color, name);
  });
}

bool InstanceWidget::closeWin()
{
  m_ctl->connectMeter(false);
  m_configDlg->setWinRect(parentRect());
  m_configDlg->on_ui_buttonBox_accepted();

  Q_EMIT setConnect(false);

  return keepUnsavedData(tr("If you quit now it will be lost."), tr("Quit without saving"));
}

bool InstanceWidget::keepUnsavedData(const QString &text, const QString &discard)
{
  if (!ui_graph->dirty() || !m_configDlg->generalPage()->alertUnsavedData())
    return true;

  QMessageBox question;
  question.setWindowTitle(tr("QtDMM: Unsaved data"));
  question.setText(tr("<font size=+2><b>Unsaved data</b></font><p>"
                      "You still have unsaved measured data in memory. %1"
                      "<p>Do you want to export your unsaved data first?").arg(text));
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
    noButton->setText(discard);

  switch (question.exec())
  {
    case QMessageBox::Yes:
      return ui_graph->exportDataSLOT();

    case QMessageBox::No:
      break;

    case QMessageBox::Cancel:
      return false;
  }

  return true;
}

QRect InstanceWidget::parentRect() const
{
  // the main window: InstanceWidget itself sits inside an MDI window now
  QRect fRect = window()->frameGeometry();
  QRect rect  = window()->rect();

  return QRect(fRect.x(), fRect.y(), rect.width(), rect.height());
}

void InstanceWidget::resetSLOT()
{
  m_ctl->resetMinMax();
}

void InstanceWidget::connectSLOT(bool on)
{
  connectPort(on);
  if (!on)
    ui_graph->stopSLOT();
}

void InstanceWidget::connectPort(bool on)
{
  // the recording stays: the next start clears it (a reconnect after the
  // settings or an alarm program must not lose it)
  if (on && !m_ctl->connectMeter(true))
    Q_EMIT setConnect(false);   // the port could not be opened: button back to "off"
  else if (!on)
    m_ctl->connectMeter(false);

  ui_graph->connectSLOT(on);
}

void InstanceWidget::quitSLOT()
{
  if (closeWin()) qApp->quit();
}

void InstanceWidget::helpSLOT()
{
  QWhatsThis::enterWhatsThisMode();
}

void InstanceWidget::configSLOT()
{
  // opened plainly it starts on General, not on the page left last time;
  // already open, it stays where it is
  if (!m_configDlg->isVisible())
    m_configDlg->showPage(SettingsDialog::General);
  openConfig();
}

void InstanceWidget::updateValueNames()
{
  QStringList names, plain;
  for (int id = 0; numValues() > 1 && id < numValues(); ++id)
  {
    names << QString("%1: %2").arg(id + 1).arg(valueName(id));
    plain << valueName(id);
  }
  m_configDlg->alarmPage()->setValueNames(names);
  m_configDlg->recorderPage()->setValueNames(names);
  if (m_readingLog)
    m_readingLog->setValueNames(plain);
}

bool InstanceWidget::tableValueShown(int id) const
{
  return !m_readingLog || !m_readingLog->hiddenValues().contains(id);
}

void InstanceWidget::setTableValueShown(int id, bool shown)
{
  if (m_readingLog)
    m_readingLog->setValueHidden(id, !shown);
}

void InstanceWidget::openConfig(int page)
{
  updateValueNames();
  // the meter goes on reading, and a recording on recording: the dialog
  // has no meter page any more (it disconnected for that, and stopped a
  // recording)
  if (page >= 0)
    m_configDlg->showPage(SettingsDialog::PageType(page));
  m_configDlg->show();
  m_configDlg->raise();
}

QString InstanceWidget::currentDevice() const
{
  const QString id = m_settings->getString("DMM/my-device");
  return !id.isEmpty() && m_devices->find(id) ? id : QString();
}

bool InstanceWidget::switchDevice(const QString &id, bool ask)
{
  const std::optional<MyDevice> device = m_devices->find(id);
  if (!device || (ask && !confirmSwitch(device->name)))
    return false;
  takeOver(device->keys, device->name, id);
  return true;
}

bool InstanceWidget::confirmSwitch(const QString &name)
{
  // the readings of the meter before are another function: export or drop them
  return keepUnsavedData(tr("Switching to %1 clears it.").arg(name), tr("Switch without saving"));
}

void InstanceWidget::takeOver(const QVariantMap &keys, const QString &name, const QString &id)
{
  // another meter is another function: one recording does not mix them
  if (m_ctl->recorder()->isRunning())
  {
    ui_graph->stopSLOT();
    Q_EMIT error(tr("Recording stopped: switched to %1").arg(name));
  }
  Q_EMIT setConnect(false);
  Q_EMIT connectDMM(false);
  connectSLOT(false);

  m_settings->setValues(keys);
  m_settings->setString("DMM/my-device", id);
  m_settings->setBool("DMM/configured", true);
  m_settings->save();
  m_configDlg->reloadMeter();
  applySLOT();
  // another meter: its own minimum and maximum, even at the same port, and
  // the window starts without the last value of the one before
  m_ctl->resetMinMax();
  m_ctl->recorder()->forgetReading();
  forgetValues();
  ui_graph->liveSLOT();
  Q_EMIT meterChanged();

  Q_EMIT setConnect(true);
  Q_EMIT connectDMM(true);
  connectSLOT(true);
}

void InstanceWidget::syncDevice()
{
  // another instance may have changed a formula here (renaming): the stale
  // cache must not go back into the entry
  m_settings->sync();
  QVariantMap keys = m_settings->meterKeys();
  keys.insert("Port settings/device", SerialDevice::stableDevice(keys.value("Port settings/device").toString()));
  const QString model = keys.value("DMM/model").toString();
  const QString stored = m_settings->getString("DMM/my-device");
  std::optional<MyDevice> device = m_devices->find(stored);
  if (!device || !DmmDecoder::sameModel(device->model(), model))
  {
    // another meter: one of My devices when it is at its place
    device = m_devices->find(m_devices->findByPlace(keys));
    if (device && !DmmDecoder::sameModel(device->model(), model))
      device.reset();
  }
  const QString id = device ? device->id : QString();
  if (id != stored)
  {
    m_settings->setString("DMM/my-device", id);
    m_settings->save();
  }
  // the same meter: a port or a key changed here changes its entry
  if (device && DeviceLibrary::entryKeys(keys) != device->keys)
    m_devices->update(id, keys);
}

void InstanceWidget::configRecorderSLOT()
{
  openConfig(SettingsDialog::Recorder);
}

void InstanceWidget::configScpiSLOT()
{
  openConfig(SettingsDialog::Scpi);
}

void InstanceWidget::forgetValues()
{
  m_ctl->forgetUnit();   // the axis title and the alarm unit come with the first reading
  m_lastReading.clear();   // and what the values measure
  m_lastText.clear();
  m_seenValues = 0;
  for (int id = 1; id < kMaxValues; ++id)
    m_configDlg->alarmPage()->setValueUnit(id, QString());
  applyValueCount();
}

void InstanceWidget::applySLOT()
{
  readConfig();
  // edited alarms: their places in the list may have moved, the sounds stop
  const QList<Alarm> alarms = m_configDlg->alarmPage()->alarms();
  if (Alarm::listToJson(alarms) != Alarm::listToJson(m_ctl->alarms()->alarms()))
    m_alarmSound->stopAll();
  m_ctl->setAlarms(alarms);
  m_alertBlink = m_configDlg->alarmPage()->blink();
  showAlertColor();
  ui_graph->setAlertUnsaved(m_configDlg->generalPage()->alertUnsavedData());
  const QString model = m_configDlg->meterSettings()->dmmName();
  const bool otherMeter = !m_ctl->model().isEmpty() && model != m_ctl->model();
  m_ctl->setModel(model);
  if (otherMeter)   // chosen on the Device page; the sidebar's takeOver() does the same
    forgetValues();
  ScpiConfig scpi;
  scpi.enabled = m_configDlg->scpiPage()->enabled();
  scpi.allInterfaces = m_configDlg->scpiPage()->allInterfaces();
  scpi.port = quint16(m_configDlg->scpiPage()->port());
  scpi.mdns = m_configDlg->scpiPage()->mdns();
  scpi.hislip = m_configDlg->scpiPage()->hislip();
  scpi.hislipPort = quint16(m_configDlg->scpiPage()->hislipPort());
  scpi.vxi11 = m_configDlg->scpiPage()->vxi11();
  scpi.vxi11Rpc = Vxi11Server::modeFromName(m_configDlg->scpiPage()->vxi11Rpc());
  m_ctl->applyScpi(scpi);
  syncDevice();
  const std::optional<MyDevice> device = m_devices->find(m_settings->getString("DMM/my-device"));
  m_ctl->setDevice(device ? device->id : QString(), device ? device->name : QString());
  Q_EMIT configChanged();
}

void InstanceWidget::zoomedSLOT()
{
  ui_graph->setGraphSize(m_configDlg->graphPage()->windowSeconds());
}

void InstanceWidget::exportSLOT()
{
  ui_graph->exportDataSLOT();
}

void InstanceWidget::importSLOT()
{
  if (!keepUnsavedData(tr("Loading a file replaces it."), tr("Load without saving")))
    return;
  ui_graph->importDataSLOT();
}

void InstanceWidget::printSLOT()
{
  m_printDlg->setPrinter(&m_printer);

  if (m_printDlg->exec())
  {
    m_configDlg->writePrinter(&m_printer);
    ui_graph->print(&m_printer, m_printDlg->title(), m_printDlg->comment());
  }
}

void InstanceWidget::clearSLOT()
{
  if (!keepUnsavedData(tr("Clear deletes it."), tr("Clear without saving")))
    return;
  // an empty view is nothing to look at: Live again
  if (m_ctl->recorder()->state() == RecordingStore::View)
    ui_graph->liveSLOT();
  else
    ui_graph->clearSLOT();
}

void InstanceWidget::liveSLOT()
{
  if (m_ctl->recorder()->state() != RecordingStore::View)
  {
    Q_EMIT recorderState(int(m_ctl->recorder()->state()));   // the button shows the state again
    return;
  }
  if (keepUnsavedData(tr("Live clears it."), tr("Live without saving")))
    ui_graph->liveSLOT();
  else
    Q_EMIT recorderState(int(m_ctl->recorder()->state()));
}

bool InstanceWidget::confirmRecording()
{
  if (m_ctl->recorder()->state() != RecordingStore::View)
    return true;
  return keepUnsavedData(tr("A new recording clears it."), tr("Record without saving"));
}

bool InstanceWidget::askRecordingLength()
{
  // a start the settings set up waits in Live; this one starts now
  QString hint;
  if (m_configDlg->recorderPage()->sampleMode() == GraphWidget::Time)
    hint = tr("It starts now. A recording at %1 starts by itself from Live.").arg(m_configDlg->recorderPage()->startTime().toString());
  else if (m_configDlg->recorderPage()->sampleMode() == GraphWidget::Raising || m_configDlg->recorderPage()->sampleMode() == GraphWidget::Falling)
    hint = tr("It starts now. A recording at the threshold starts by itself from Live.");
  RecordLengthDlg dlg(m_configDlg->recorderPage()->lengthValue(), m_configDlg->recorderPage()->lengthUnit(), hint, this);
  if (dlg.exec() != QDialog::Accepted)
    return false;
  m_configDlg->setRecordingLength(dlg.value(), dlg.unit());
  ui_graph->setSampleLength(m_configDlg->recorderPage()->sampleLength());
  return true;
}

void InstanceWidget::startSLOT()
{
  ui_graph->startSLOT();
}

void InstanceWidget::stopSLOT()
{
  ui_graph->stopSLOT();
}

void InstanceWidget::readConfig()
{
  MeterConnection *dmm = m_ctl->dmm();
  bool reopen = false;
  const GraphPrefs *graph = m_configDlg->graphPage();
  const RecorderPrefs *recorder = m_configDlg->recorderPage();
  const GuiPrefs *gui = m_configDlg->guiPage();

  // the port only when the meter changed: a reopen costs a Bluetooth meter
  // seconds, and the readings and a recording a gap
  const MeterSetup setup = m_configDlg->meterSetup();
  QVariantMap keys = m_settings->meterKeys();
  keys.insert("sigrok", setup.info.sigrokExe);
  if (!dmm->isOpen() || keys != m_appliedMeter)
  {
    m_appliedMeter = keys;
    if (dmm->isOpen())
    {
      dmm->close();
      reopen = true;
    }

    // a meter the session has open in the background gives up the port
    m_session->release(m_settings->getString("DMM/my-device"));
    m_ctl->configure(setup);
  }

  // the sample time first: setGraphSize() counts the window in samples, and
  // the x axis converts them back with the sample time
  ui_graph->setSampleTime(recorder->sampleStep());
  ui_graph->setSampleLength(recorder->sampleLength());
  ui_graph->store()->setPreTrigger(recorder->preTrigger());
  ui_graph->setGraphSize(graph->windowSeconds());
  ui_graph->setStartTime(recorder->startTime());
  ui_graph->setMode(recorder->sampleMode());

  ui_graph->setCrosshair(graph->crosshair());

  // the value first: the graph shows the trigger line only for the main value
  ui_graph->store()->setTriggerValue(recorder->triggerValue());
  ui_graph->setThresholds(recorder->fallingThreshold(), recorder->raisingThreshold());

  // the reference line snaps to the alarms' thresholds
  {
    QList<double> thresholds;
    for (const Alarm &a : m_configDlg->alarmPage()->alarms())
    {
      // the graph's axis is the main value's
      if (!a.enabled || a.value != ValueRef() || a.condition == Alarm::Overload || a.condition == Alarm::NoReadings)
        continue;
      thresholds << a.a;
      if (a.condition == Alarm::Outside || a.condition == Alarm::Inside)
        thresholds << a.b;
    }
    ui_graph->setAlarmThresholds(thresholds);
  }
  ui_graph->setScale(graph->automaticScale(), graph->includeZero(), graph->scaleMin(), graph->scaleMax());

  {
    const QString own = m_settings->getString("Windows/graph-variant");
    ui_graph->setColorVariant(GraphWidget::variantFromName(graph->variant()),
                              own.isEmpty() ? -1 : int(GraphWidget::variantFromName(own)));
  }
  ui_graph->setColors(graph->bgColor(),
                      graph->gridColor(),
                      graph->dataColor(),
                      graph->cursorColor(),
                      graph->startColor(),
                      graph->intColor(),
                      graph->intThresholdColor());

  ui_graph->setLineStyle(graph->lineMode(), graph->pointMode(), graph->intLineMode(), graph->intPointMode());

  // a tint changed on the Appearance page is meant to be seen: Custom
  const QColor tint = gui->displayBgColor();
  if (m_lcdTint.isValid() && tint != m_lcdTint && m_display->lcdVariant() != LcdWidget::Custom)
    setLcdVariant(LcdWidget::Custom);
  m_lcdTint = tint;
  m_display->setFaceColor(tint);
  m_display->setDisplayMode(setup.info.display, gui->showMinMax(), gui->showBar(), numValues());
  applyValueCount();

  if (m_meter)
  {
    m_meter->setDisplayCounts(setup.info.display);
    applyMeterStyle();
    m_meter->setScaleMode(static_cast<AnalogMeter::ScaleMode>(
      gui->meterScaleMode() == 1 ? AnalogMeter::Unipolar :
      gui->meterScaleMode() == 2 ? AnalogMeter::Bipolar : AnalogMeter::Auto));
  }

  ui_graph->setLine(graph->lineWidth(), graph->intLineWidth());

  ui_graph->setIntegration(graph->showIntegration(), graph->intScale(), graph->intThreshold(), graph->intOffset());
  ui_graph->setIntegrationValue(graph->intValue());

  if (recorder->sampleMode() == GraphWidget::Time)
    Q_EMIT info(tr("Automatic start at %1").arg(recorder->startTime().toString()));
  else if (recorder->sampleMode() == GraphWidget::Raising)
    Q_EMIT info(tr("Raising threshold %1").arg(recorder->raisingThreshold()));
  else if (recorder->sampleMode() == GraphWidget::Falling)
    Q_EMIT info(tr("Falling threshold %1").arg(recorder->fallingThreshold()));
  Q_EMIT useTextLabel(gui->useTextLabel());
  Q_EMIT iconSet(gui->iconSet());
  Q_EMIT remoteControl(ControlBar::supported(setup.info));
  Q_EMIT showDisplay(gui->showDisplay());

  if (reopen)
    dmm->open();
}

void InstanceWidget::runningSLOT(bool on)
{
  Q_EMIT running(on);
}

bool InstanceWidget::dmmConfigured() const
{
  // DMM/configured is set when a device is chosen (takeOver(), a new window
  // with a device); the model check keeps configs from before that key
  // working. "Manual" alone proves nothing: applySLOT() writes it at every
  // exit, dialog or not.
  if (m_settings->getBool("DMM/configured", false))
    return true;
  const QString model = m_settings->getString("DMM/model");
  return !model.isEmpty() && model != "Manual";
}

QString InstanceWidget::portName() const
{
  // a Bluetooth port string carries the encryption key: never show it
  const QString device = m_configDlg->meterSettings()->device().simplified();
  const QString type = device.section(' ', 0, 0).toLower();
  if (type == "ble" || type == "blegatt")
    return "BLE " + device.section(' ', 1, 1);
  if (type == "calc")
    return tr("calculated");
  if (type == "serial")
    return device.section(' ', 1);   // "SERIAL /dev/ttyUSB0": the port alone
  return device;
}

QString InstanceWidget::dmmTitle() const
{
  if (!dmmConfigured())
    return tr("no meter configured");
  const QString model = m_configDlg->meterSettings()->dmmName().trimmed();
  if (!model.isEmpty())
    return model;
  return m_configDlg->meterSettings()->device().trimmed();
}

// ---------------------------------------------------------------- alarms

// The controller has reported the alarm and run its program; what is left
// needs the desktop: the sound, raise the window, pop up a box.
void InstanceWidget::alarmRaised(int index, const Alarm &alarm, const QString &shown, const QString &text)
{
  m_alarmSound->start(index, alarm);
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

void InstanceWidget::showAlertColor()
{
  const bool blinking = m_alertColor.isValid() && m_alertBlink;
  if (blinking && !m_alertTimer->isActive())
    m_alertTimer->start();
  else if (!blinking)
  {
    m_alertTimer->stop();
    m_alertPhase = true;
  }
  // an alarm's colour (blinking if wanted) over the limit test's; the
  // analog meter takes the alarm's alone, its needle stays easy to follow
  // against the green band
  const QColor alarm = m_alertPhase ? m_alertColor : QColor();
  if (m_display)
    m_display->setAlertColor(m_alertColor.isValid() ? alarm : m_limitColor);
  if (m_meter)
    m_meter->setAlertColor(alarm);
}

void InstanceWidget::setLimitTest(const LimitTest &test)
{
  m_ctl->setLimitTest(test);
}

void InstanceWidget::applyLimitBands()
{
  const LimitTest &test = m_ctl->limitTest();
  const int id = test.enabled && ownMeter(test.value) ? test.value.id : -1;
  ui_graph->setLimits(id, test.low(), test.high());
  if (m_meter)
    m_meter->setLimits(id, test.low(), test.high());
}

const LimitTest &InstanceWidget::limitTest() const
{
  return m_ctl->limitTest();
}

void InstanceWidget::setMeterStyle(int style)
{
  m_configDlg->guiPage()->setMeterStyle(style);
  m_settings->setInt("Meter/style", style);
  applyMeterStyle();
}

void InstanceWidget::applyMeterStyle()
{
  if (!m_meter)
    return;
  AnalogMeterStyle style = m_configDlg->guiPage()->meterStyle() == 1 ? AnalogMeterStyle::ivory() : AnalogMeterStyle::dark();
  style.ballistics = m_configDlg->guiPage()->meterBallistics();
  style.redZoneFrom = m_configDlg->guiPage()->meterRedZone() / 100.0;
  m_meter->setStyle(style);
}

void InstanceWidget::setLcdVariant(int variant)
{
  const auto v = static_cast<LcdWidget::LcdVariant>(variant);
  m_display->setLcdVariant(v);
  m_settings->setString("Display/lcd", LcdWidget::lcdVariantName(v));
}
