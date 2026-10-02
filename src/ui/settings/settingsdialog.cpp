//======================================================================
// File:		configdlg.cpp
// Author:	Matthias Toussaint
// Created:	Sat Oct 19 14:54:29 CEST 2002
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
// Copyright (c) 2002 Matthias Toussaint
//======================================================================


#include <QtWidgets>
#include <QtPrintSupport>

#include "ui/settings/settingsdialog.h"
#include "ui/settings/settingspageitem.h"
#include "ui/settings/meterprefs.h"
#include "ui/settings/executeprefs.h"
#include "ui/settings/alarmprefs.h"
#include "ui/settings/scpiprefs.h"
#include "ui/settings/graphprefs.h"
#include "ui/settings/guiprefs.h"
#include "ui/settings/integrationprefs.h"
#include "ui/settings/recorderprefs.h"
#include "ui/settings/scaleprefs.h"
#include "ui/settings/portsprefs.h"
#include "core/settings.h"

#include <iostream>


SettingsDialog::SettingsDialog(Settings* settings, QWidget *parent)
  : QDialog(parent)
  , m_settings(settings)
{
  m_buttonBox_OK = false;
  setupUi(this);

  // Ctrl+PgUp/PgDn flip through the pages, like tabs
  auto pageShortcut = [this](const QKeySequence &key, int delta)
  {
    QAction *a = new QAction(this);
    a->setShortcut(key);
    connect(a, &QAction::triggered, this, [this, delta]
    {
      const int n = ui_list->count();
      if (n > 0)
        ui_list->setCurrentRow((ui_list->currentRow() + delta + n) % n);
    });
    addAction(a);
  };
  pageShortcut(QKeySequence("Ctrl+PgUp"), -1);
  pageShortcut(QKeySequence("Ctrl+PgDown"), 1);

  // Check if configuration file exists. If not welcome user
  if (!m_settings->fileExists())
  {
    QMessageBox welcome;
    welcome.setWindowTitle(tr("QtDMM: Welcome!"));
    welcome.setText(tr("<font size=+2><b>Welcome!</b></font><p>"
                       "This seems to be your first invocation of QtDMM "
                       "(Or you have deleted its configuration file).<p>QtDMM "
                       "has created the file %1 in your home directory "
                       "to save its settings.").arg(m_settings->fileName()));
    welcome.setIcon(QMessageBox::Information);
    welcome.setStandardButtons(QMessageBox::Yes);
    welcome.setDefaultButton(QMessageBox::Yes);
    welcome.setIconPixmap(QPixmap(":/Symbols/icon.xpm"));

    QAbstractButton *yesButton = welcome.button(QMessageBox::Yes);
    if (yesButton)
      yesButton->setText(tr("Continue"));

    welcome.exec();
  }
  else
  {
    int version = m_settings->getInt("QtDMM/version");
    int revision = m_settings->getInt("QtDMM/revision");

    if ((version <= 0 && revision < 84) || version >= 7)
    {
      QMessageBox welcome;
      welcome.setWindowTitle(tr("QtDMM: Welcome!"));
      welcome.setText(tr("<font size=+2><b>Welcome!</b></font><p>"
                         "You seem to have upgraded <b>QtDMM</b> from a version prior to 0.8.4. "
                         "Please check your configuration. There are some new parameters to be "
                         "configured."
                         "<p>Thank you for choosing <b>QtDMM</b>.<p><i>Matthias Toussaint</i>"));
      welcome.setIcon(QMessageBox::Information);
      welcome.setStandardButtons(QMessageBox::Yes);
      welcome.setDefaultButton(QMessageBox::Yes);
      welcome.setIconPixmap(QPixmap(":/Symbols/icon.xpm"));

      QAbstractButton *yesButton = welcome.button(QMessageBox::Yes);
      if (yesButton)
        yesButton->setText(tr("Continue"));

      welcome.exec();
    }

    if (m_settings->fileConverted())
    {
      QMessageBox::information(nullptr,
                               tr("QtDMM: Welcome!"),
                               tr("Your config file has been converted to the new format.\n"
                                  "Please check your color settings, because they couldn't be converted automatically.\n"
                                  "Your old config ~/.qtdmmrc was renamed to ~/.qtdmmrc.old."));
    }
  }

  // CREATE PAGES, in the order the list shows them

  m_dmm = new MeterPrefs(ui_stack);
  connect(m_dmm, &MeterPrefs::showPortsPage, this, [this] { showPage(Ports); });
  m_dmm->setId(SettingsDialog::MeterConnection);
  new SettingsPageItem(m_dmm->id(),
                 m_dmm->icon(),
                 m_dmm->label(),
                 ui_list);
  m_dmm->setCfg(m_settings);
  addPage(m_dmm);

  m_gui = new GuiPrefs(ui_stack);
  m_gui->setId(SettingsDialog::GUI);
  new SettingsPageItem(m_gui->id(),
                 m_gui->icon(),
                 m_gui->label(),
                 ui_list);
  m_gui->setCfg(m_settings);
  addPage(m_gui);

  m_graph = new GraphPrefs(ui_stack);
  m_graph->setId(SettingsDialog::Graph);
  new SettingsPageItem(m_graph->id(),
                 m_graph->icon(),
                 m_graph->label(),
                 ui_list);
  m_graph->setCfg(m_settings);
  addPage(m_graph);

  m_scale = new ScalePrefs(ui_stack);
  m_scale->setId(SettingsDialog::Scale);
  new SettingsPageItem(m_scale->id(),
                 m_scale->icon(),
                 m_scale->label(),
                 ui_list);
  m_scale->setCfg(m_settings);
  addPage(m_scale);

  m_integration = new IntegrationPrefs(ui_stack);
  m_integration->setId(SettingsDialog::Integration);
  new SettingsPageItem(m_integration->id(),
                 m_integration->icon(),
                 m_integration->label(),
                 ui_list);
  m_integration->setCfg(m_settings);
  addPage(m_integration);

  m_recorder = new RecorderPrefs(ui_stack);
  m_recorder->setId(SettingsDialog::Recorder);
  new SettingsPageItem(m_recorder->id(),
                 m_recorder->icon(),
                 m_recorder->label(),
                 ui_list);
  m_recorder->setCfg(m_settings);
  addPage(m_recorder);

  m_ports = new PortsPrefs(ui_stack);
  m_ports->setId(SettingsDialog::Ports);
  new SettingsPageItem(m_ports->id(),
                 m_ports->icon(),
                 m_ports->label(),
                 ui_list);
  m_ports->setCfg(m_settings);
  addPage(m_ports);

  m_execute = new ExecutePrefs(ui_stack);
  m_execute->setId(SettingsDialog::External);
  new SettingsPageItem(m_execute->id(),
                 m_execute->icon(),
                 m_execute->label(),
                 ui_list);
  m_execute->setCfg(m_settings);
  addPage(m_execute);

  m_alarms = new AlarmPrefs(ui_stack);
  m_alarms->setId(SettingsDialog::Alarms);
  new SettingsPageItem(m_alarms->id(),
                 m_alarms->icon(),
                 m_alarms->label(),
                 ui_list);
  m_alarms->setCfg(m_settings);
  addPage(m_alarms);

  m_scpi = new ScpiPrefs(ui_stack);
  m_scpi->setId(SettingsDialog::Scpi);
  new SettingsPageItem(m_scpi->id(),
                 m_scpi->icon(),
                 m_scpi->label(),
                 ui_list);
  m_scpi->setCfg(m_settings);
  addPage(m_scpi);

  // init stuff
  //
  on_ui_buttonBox_rejected();
  showPage(MeterConnection);
  ui_undo->hide();
  adjustSize();
}

void SettingsDialog::addPage(SettingsPage *page)
{
  auto *scroll = new QScrollArea(ui_stack);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(page);
  ui_stack->insertWidget(page->id(), scroll);
}

SettingsPage *SettingsDialog::page(int index) const
{
  auto *scroll = qobject_cast<QScrollArea *>(ui_stack->widget(index));
  return scroll ? qobject_cast<SettingsPage *>(scroll->widget()) : nullptr;
}

void SettingsDialog::showEvent(QShowEvent *event)
{
  QDialog::showEvent(event);
  // the pages scroll, so the dialog may be smaller than its content; leave
  // room for the window frame, which frameGeometry() only knows once shown
  const QRect avail = screen()->availableGeometry();
  const QSize frame = frameGeometry().size() - size();
  const QSize max = avail.size() - frame.expandedTo(QSize(0, 40));
  if (width() > max.width() || height() > max.height())
    resize(size().boundedTo(max));
  QRect r = frameGeometry();
  r.moveTop(qBound(avail.top(), r.top(), qMax(avail.top(), avail.bottom() - r.height())));
  r.moveLeft(qBound(avail.left(), r.left(), qMax(avail.left(), avail.right() - r.width())));
  if (r.topLeft() != frameGeometry().topLeft())
    move(r.topLeft());
}

void SettingsDialog::setStateManager(SharedStateManager *state)
{
  m_dmm->setStateManager(state);
}

void SettingsDialog::showPage(SettingsDialog::PageType page)
{
  SettingsPage *wid = Q_NULLPTR;
  for (int entry = 0; entry < ui_list->count(); entry++)
  {
    SettingsPageItem *item = dynamic_cast<SettingsPageItem *>(ui_list->item(entry));
    if (item && item->id() == page)
    {
      ui_list->setCurrentRow(entry);
      ui_stack->setCurrentIndex(page);
      wid = this->page(page);
      break;
    }
  }
  if (wid)
  {
    ui_helpText->setText(wid->description());
    ui_helpPixmap->setPixmap(wid->icon().pixmap(32));
  }
}

void SettingsDialog::on_ui_factoryDefaults_clicked()
{
  page(ui_stack->currentIndex())->factoryDefaultsSLOT();
}

void SettingsDialog::zoomInSLOT(double fac)
{
  m_scale->zoomInSLOT(fac);
  Q_EMIT zoomed();
}

void SettingsDialog::zoomOutSLOT(double fac)
{
  m_scale->zoomOutSLOT(fac);
  Q_EMIT zoomed();
}

void SettingsDialog::zoomFitSLOT()
{
  m_scale->zoomFitSLOT();
  Q_EMIT zoomed();
}

void SettingsDialog::setSampleTimeSLOT(int sampleTime)
{
  m_recorder->setSampleTimeSLOT(sampleTime);
  on_ui_buttonBox_accepted();
}

void SettingsDialog::setWindowSecondsSLOT(int seconds)
{
  m_scale->setWindowSecondsSLOT(seconds);
  Q_EMIT zoomed();
}

void SettingsDialog::setGraphSizeSLOT(int size, int length)
{
  m_scale->setGraphSizeSLOT(size, length);
  on_ui_buttonBox_accepted();
}

void SettingsDialog::connectSLOT(bool /*connected*/)
{

}

QRect SettingsDialog::winRect() const
{
  QRect rect(m_settings->getInt("Position/x"),
             m_settings->getInt("Position/y"),
             m_settings->getInt("Position/width", 500),
             m_settings->getInt("Position/height", 350));

  return rect;
}

void SettingsDialog::setWinRect(const QRect &rect)
{
  m_winRect = rect;
}

void SettingsDialog::reloadSettings()
{
  m_settings->clear();
  int count = m_settings->getInt("Custom colors/count");
  for (int i = 0; i < count; i++)
    QColorDialog::setCustomColor(i, m_settings->getColor(QString("Custom colors/color_%1").arg(i)));
  for (int i = 0; i < NumItems; ++i)
    page(i)->defaultsSLOT();
}

void SettingsDialog::on_ui_buttonBox_rejected()
{
  reloadSettings();
  hide();
}

void SettingsDialog::setCurrentTipSLOT(int id)
{
  m_settings->setInt("QtDMM/tip-id", id);
  m_settings->save();
}

int SettingsDialog::currentTipId() const
{
  return m_settings->getInt("QtDMM/tip-id");
}

void SettingsDialog::on_ui_buttonBox_accepted()
{
  m_settings->setInt("Custom colors/count", QColorDialog::customCount());

  for (int i = 0; i < QColorDialog::customCount(); i++)
    m_settings->setColor(QString("Custom colors/color_%1").arg(i), QColorDialog::customColor(i));
  m_settings->setInt("Position/x", m_winRect.x());
  m_settings->setInt("Position/y", m_winRect.y());
  m_settings->setInt("Position/width", m_winRect.width());
  m_settings->setInt("Position/height", m_winRect.height());

  m_settings->setInt("Printer/page-size", static_cast<int>(m_printer->pageLayout().pageSize().id()));
  m_settings->setInt("Printer/page-orientation", static_cast<int>(m_printer->pageLayout().orientation()));
  m_settings->setInt("Printer/color", static_cast<int>(m_printer->colorMode()));
  m_settings->setString("Printer/name", m_printer->printerName());
  m_settings->setString("Printer/filename", m_printer->outputFileName());
  m_settings->setBool("Printer/print-file", (m_printer->outputFormat() == QPrinter::PdfFormat) ? true : false);

  for (int i = 0; i < NumItems; ++i)
    page(i)->applySLOT();

  m_settings->save();
  reloadSettings();

  if ((sender() == ui_buttonBox) && m_buttonBox_OK)
  {
    // the user confirmed the dialog - only now does a meter count as chosen
    // (applySLOT() also runs at exit, which must not turn on auto-connect)
    m_settings->setBool("DMM/configured", true);
    m_settings->save();
    Q_EMIT accepted();
    hide();
  }
  else
    Q_EMIT applied();
}

void SettingsDialog::on_ui_buttonBox_clicked(QAbstractButton *button)
{
  m_buttonBox_OK = (ui_buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole);

  if (ui_buttonBox->buttonRole(button) == QDialogButtonBox::ApplyRole)
    on_ui_buttonBox_accepted();

  if (ui_buttonBox->buttonRole(button) == QDialogButtonBox::RejectRole)
    Q_EMIT rejected();
}

void SettingsDialog::writePrinter(QPrinter *printer)
{
  m_printer = printer;
  on_ui_buttonBox_accepted();
}

void SettingsDialog::readPrinter(QPrinter *printer)
{
  m_printer = printer;

  m_printer->setPageSize(QPageSize(static_cast<QPageSize::PageSizeId>(m_settings->getInt("Printer/page-size"))));
  m_printer->setPageOrientation(static_cast<QPageLayout::Orientation>(m_settings->getInt("Printer/page-orientation")));
  m_printer->setColorMode(static_cast<QPrinter::ColorMode>(m_settings->getInt("Printer/color", 1)));
  m_printer->setPrinterName(m_settings->getString("Printer/name", "lp"));
  m_printer->setOutputFileName(m_settings->getString("Printer/filename"));
  m_printer->setOutputFormat((m_settings->getBool("Printer/print-file")) ? QPrinter::PdfFormat : QPrinter::NativeFormat);
}

void SettingsDialog::on_ui_list_currentItemChanged(QListWidgetItem *current, QListWidgetItem *)
{
  int id = dynamic_cast<SettingsPageItem *>(current)->id();
  SettingsPage *wid = page(id);
  ui_stack->setCurrentIndex(id);

  ui_helpText->setText(wid->description());
  ui_helpPixmap->setPixmap(wid->icon().pixmap(32));
}

void SettingsDialog::thresholdChangedSLOT(GraphWidget::CursorMode mode, double value)
{
  switch (mode)
  {
    case GraphWidget::Trigger:
      m_recorder->setThreshold(value);
      break;
    case GraphWidget::External:
      m_execute->setThreshold(value);
      break;
    case GraphWidget::Integration:
      m_integration->setThreshold(value);
      break;
    default:
      std::cerr << "Unexpected CursorMode in configdlg.cpp:418" << std::endl;
      break;
  }
}

/////////////////////////////////////////////////////////////////
// RECORDER
//
GraphWidget::SampleMode SettingsDialog::sampleMode() const
{
  return m_recorder->sampleMode();
}

int SettingsDialog::sampleStep() const
{
  return m_recorder->sampleStep();
}

int SettingsDialog::sampleLength() const
{
  return m_recorder->sampleLength();
}

double SettingsDialog::fallingThreshold() const
{
  return m_recorder->fallingThreshold();
}

double SettingsDialog::raisingThreshold() const
{
  return m_recorder->raisingThreshold();
}

QTime SettingsDialog::startTime() const
{
  return m_recorder->startTime();
}

/////////////////////////////////////////////////////////////////
// EXECUTE
//
bool SettingsDialog::startExternal() const
{
  return m_execute->startExternal();
}

bool SettingsDialog::externalFalling() const
{
  return m_execute->externalFalling();
}

double SettingsDialog::externalThreshold() const
{
  return m_execute->externalThreshold();
}

bool SettingsDialog::disconnectExternal() const
{
  return m_execute->disconnectExternal();
}

QString SettingsDialog::externalCommand() const
{
  return m_execute->externalCommand();
}

/////////////////////////////////////////////////////////////////
// GUI
//
bool SettingsDialog::showTip() const
{
  return m_gui->showTip();
}

bool SettingsDialog::showBar() const
{
  return m_gui->showBar();
}

bool SettingsDialog::showMinMax() const
{
  return m_gui->showMinMax();
}

bool SettingsDialog::alertUnsavedData() const
{
  return m_gui->alertUnsavedData();
}

bool SettingsDialog::useTextLabel() const
{
  return m_gui->useTextLabel();
}

bool SettingsDialog::systemIcons() const
{
  return m_gui->systemIcons();
}

QColor SettingsDialog::displayBgColor() const
{
  return m_gui->displayBgColor();
}

bool SettingsDialog::saveWindowPosition() const
{
  return m_gui->saveWindowPosition();
}

bool SettingsDialog::saveWindowSize() const
{
  return m_gui->saveWindowSize();
}

void SettingsDialog::setShowTipsSLOT(bool on)
{
  m_gui->on_ui_tipOfTheDay_toggled(on);
}

bool SettingsDialog::showDmmToolbar() const
{
  return m_gui->showDmmToolbar();
}

bool SettingsDialog::showGraphToolbar() const
{
  return m_gui->showGraphToolbar();
}

bool SettingsDialog::showFileToolbar() const
{
  return m_gui->showFileToolbar();
}

bool SettingsDialog::showDisplay() const
{
  return m_gui->showDisplay();
}

int SettingsDialog::meterScaleMode() const
{
  return m_gui->meterScaleMode();
}

void SettingsDialog::setMeterStyle(int style)
{
  m_gui->setMeterStyle(style);
}

int SettingsDialog::meterStyle() const
{
  return m_gui->meterStyle();
}

bool SettingsDialog::meterBallistics() const
{
  return m_gui->meterBallistics();
}

int SettingsDialog::meterRedZone() const
{
  return m_gui->meterRedZone();
}

void SettingsDialog::setToolbarVisibility(bool disp, bool dmm, bool graph, bool file)
{
  m_gui->setToolbarVisibility(disp, dmm, graph, file);
}

/////////////////////////////////////////////////////////////////
// SCALE
//
bool SettingsDialog::automaticScale() const
{
  return m_scale->automaticScale();
}

bool SettingsDialog::includeZero() const
{
  return m_scale->includeZero();
}

double SettingsDialog::scaleMin() const
{
  return m_scale->scaleMin();
}

double SettingsDialog::scaleMax() const
{
  return m_scale->scaleMax();
}

int SettingsDialog::windowSeconds() const
{
  return m_scale->windowSeconds();
}

int SettingsDialog::totalSeconds() const
{
  return m_scale->totalSeconds();
}

/////////////////////////////////////////////////////////////////
// INTEGRATION
//
double SettingsDialog::intScale() const
{
  return m_integration->intScale();
}

double SettingsDialog::intThreshold() const
{
  return m_integration->intThreshold();
}

double SettingsDialog::intOffset() const
{
  return m_integration->intOffset();
}

bool SettingsDialog::showIntegration() const
{
  return m_integration->showIntegration();
}

QColor SettingsDialog::intColor() const
{
  return m_integration->intColor();
}

QColor SettingsDialog::intThresholdColor() const
{
  return m_integration->intThresholdColor();
}

int SettingsDialog::intLineWidth() const
{
  return m_integration->intLineWidth();
}

int SettingsDialog::intLineMode() const
{
  return m_integration->intLineMode();
}

int SettingsDialog::intPointMode() const
{
  return m_integration->intPointMode();
}

/////////////////////////////////////////////////////////////////
// DMM
//

DmmDecoder::DMMInfo SettingsDialog::dmmInfo() const
{
  DmmDecoder::DMMInfo info = m_dmm->dmmInfo();
  info.sigrokExe = m_ports->sigrokExecutable();
  return info;
}

bool SettingsDialog::rts() const
{
  return m_dmm->rts();
}

bool SettingsDialog::dtr() const
{
  return m_dmm->dtr();
}

QSerialPort::Parity SettingsDialog::parity() const
{
  return m_dmm->parity();
}

bool SettingsDialog::externalSetup() const
{
  return m_dmm->externalSetup();
}

int SettingsDialog::bits() const
{
  return m_dmm->bits();
}

int SettingsDialog::stopBits() const
{
  return m_dmm->stopBits();
}

int SettingsDialog::speed() const
{
  return m_dmm->speed();
}

int SettingsDialog::numValues() const
{
  return m_dmm->numValues();
}

FrameFormat::DataFormat SettingsDialog::format() const
{
  return m_dmm->format();
}

int SettingsDialog::display() const
{
  return m_dmm->display();
}

QString SettingsDialog::dmmName() const
{
  return m_dmm->dmmName();
}

QString SettingsDialog::device() const
{
  return m_dmm->device();
}

/////////////////////////////////////////////////////////////////
// GRAPH
//
bool SettingsDialog::crosshair() const
{
  return m_graph->crosshair();
}

QString SettingsDialog::graphVariant() const
{
  return m_graph->variant();
}

QColor SettingsDialog::bgColor() const
{
  return m_graph->bgColor();
}

QColor SettingsDialog::gridColor() const
{
  return m_graph->gridColor();
}

QColor SettingsDialog::dataColor() const
{
  return m_graph->dataColor();
}

QColor SettingsDialog::startColor() const
{
  return m_graph->startColor();
}

QColor SettingsDialog::externalColor() const
{
  return m_graph->externalColor();
}

QColor SettingsDialog::cursorColor() const
{
  return m_graph->cursorColor();
}

int SettingsDialog::lineWidth() const
{
  return m_graph->lineWidth();
}

int SettingsDialog::lineMode() const
{
  return m_graph->lineMode();
}

int SettingsDialog::pointMode() const
{
  return m_graph->pointMode();
}

QList<Alarm> SettingsDialog::alarms() const
{
  return m_alarms->alarms();
}

void SettingsDialog::setAlarmUnit(const QString &unit)
{
  m_alarms->setUnit(unit);
}

bool SettingsDialog::scpiEnabled() const { return m_scpi->enabled(); }
int SettingsDialog::scpiPort() const { return m_scpi->port(); }
bool SettingsDialog::scpiAllInterfaces() const { return m_scpi->allInterfaces(); }
bool SettingsDialog::scpiMdns() const { return m_scpi->mdns(); }

void SettingsDialog::setScpiStatus(const QString &text)
{
  m_scpi->setStatus(text);
}
