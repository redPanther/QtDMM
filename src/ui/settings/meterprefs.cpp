//======================================================================
// File:		dmmprefs.cpp
// Author:	Matthias Toussaint
// Created:	Sat Oct 19 15:26:51 CEST 2002
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

#include <QtGui>
#include <QtWidgets>
#include <QSerialPortInfo>
#include <iostream>
#include <algorithm>
#include <vector>

#include "ui/settings/meterprefs.h"
#include "device/transports/serial.h"
#include "core/devicelibrary.h"
#include "device/protocols.h"
#include "core/calcexpr.h"
#include "device/victronble.h"
#ifdef QTDMM_WITH_BLE
#include "device/transports/ble.h"
#include "device/transports/blegatt.h"
#endif
#include "service/sharedstatemanager.h"
#include "core/siprefix.h"
#include "core/settings.h"
#include "decoders.h"
#include "device/transport.h"

std::vector<DmmDecoder::DMMInfo> dmm_info = {};

MeterPrefs::MeterPrefs(QWidget *parent) : SettingsPage(parent)
{
  setupUi(this);
  m_portlist = new QStringListModel(this);

  m_label = tr("Multimeter");
  m_description = tr("<b>Here you can configure the serial port"
                     " and protocol for your DMM. There is"
                     " also a number of predefined models.</b>");
  m_iconName = "qtdmm-dmm";

  setupComboBoxModel();

  message2->hide();
  ui_calcGroup->hide();
  ui_bleGroup->hide();
  ui_sigrokGroup->hide();
  connect(ui_sigrokHint, &QLabel::linkActivated, this, &MeterPrefs::showPortsPage);
  connect(ui_sigrokDriver, &QLineEdit::textChanged, this, &MeterPrefs::updateSigrokHint);
  connect(ui_bleKey, &QLineEdit::textChanged, this, &MeterPrefs::updateBleHint);
  connect(ui_bleAddress, &QComboBox::currentTextChanged, this, &MeterPrefs::updateBleHint);
  ui_virtualGroup->hide();
  connect(ui_calcExpression, &QLineEdit::textChanged, this, &MeterPrefs::updateCalcHint);
  connect(ui_virtualSignal, &QComboBox::currentIndexChanged, this, &MeterPrefs::updateVirtualFormula);
  for (QLineEdit *e : {ui_virtualMin, ui_virtualMax, ui_virtualPeriod, ui_virtualNoise})
    connect(e, &QLineEdit::textChanged, this, &MeterPrefs::updateVirtualFormula);
  connect(ui_virtualFormula, &QLineEdit::textChanged, this, [this]{ if (ui_virtualSignal->currentIndex() == 7) updateVirtualFormula(); });
  m_calcHintTimer.setInterval(1000);   // live values of the input instances
  connect(&m_calcHintTimer, &QTimer::timeout, this, &MeterPrefs::updateCalcHint);

  // "My devices" at the top: a choice fills the fields, the button keeps them
  auto *devices = new QHBoxLayout;
  auto *devicesLabel = new QLabel(tr("&My devices:"), this);
  ui_myDevice = new QComboBox(this);
  ui_myDevice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  ui_myDevice->setMinimumContentsLength(16);
  ui_myDevice->setToolTip(tr("Fill in the meter and its connection from one of your devices."));
  devicesLabel->setBuddy(ui_myDevice);
  ui_saveDevice = new QPushButton(tr("Sa&ve to my devices..."), this);
  ui_saveDevice->setToolTip(tr("Keep this meter and its connection under a name, to choose it again with one click."));
  devices->addWidget(devicesLabel);
  devices->addWidget(ui_myDevice, 1);
  devices->addWidget(ui_saveDevice);
  if (auto *top = qobject_cast<QBoxLayout *>(layout()))
    top->insertLayout(0, devices);
  connect(ui_myDevice, &QComboBox::activated, this, &MeterPrefs::onDeviceChosen);
  connect(ui_saveDevice, &QPushButton::clicked, this, &MeterPrefs::saveDevice);
  ui_myDevice->setEnabled(false);

  m_path = QDir::currentPath();
}


MeterPrefs::~MeterPrefs()
{
}

// The persisted protocol: the name (FrameFormat::toString) since the protocol
// table exists, the combo index (= enum value) in older configuration files.
FrameFormat::DataFormat MeterPrefs::formatFromSetting(const QVariant &value)
{
  bool isNumber = false;
  const int number = value.toInt(&isNumber);
  if (isNumber)
    return (number >= 0 && number < FrameFormat::EndOfList) ? static_cast<FrameFormat::DataFormat>(number) : FrameFormat::Metex14;
  const FrameFormat::DataFormat df = FrameFormat::fromString(value.toString());
  return df == FrameFormat::Invalid ? FrameFormat::Metex14 : df;
}

void MeterPrefs::selectFormat(FrameFormat::DataFormat df)
{
  const int idx = protocolCombo->findData(int(df));
  protocolCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void MeterPrefs::setupComboBoxModel()
{
  // one combo entry per row of the protocol table, the enum value as data
  protocolCombo->clear();
  for (const ProtocolInfo &p : protocols())
    protocolCombo->addItem(QCoreApplication::translate("Protocols", p.description), int(p.id));

  ui_vendor->clear();
  ui_vendor->insertItem(-1, tr("Manual settings"));
  ui_vendor->addItem(tr("All vendors"));

  std::vector<DmmDecoder::DMMInfo> configs = DmmDecoder::getDeviceConfigurations();

  // Sortieren nach dem Namen
  std::sort(configs.begin(), configs.end(), [](const auto& a, const auto& b) {
    return a.name < b.name;
  });

  dmm_info.clear();
  QStringList vendors;
  for (const auto& cfg : configs) {
    dmm_info.push_back(cfg);
    if (!vendors.contains(cfg.vendor))
      vendors.append(cfg.vendor);
  }

  vendors.sort(Qt::CaseInsensitive);
  // the virtual meters of QtDMM itself go first, real vendors alphabetically
  if (vendors.removeOne("QtDMM"))
    vendors.prepend("QtDMM");
  ui_vendor->addItems(vendors);

  // The completer searches the full, unfiltered device list (all vendors),
  // so typing a known model name still finds it directly - on selection the
  // editingFinished handler below switches the vendor combo to match.
  QStringList allNames;
  for (const auto& cfg : dmm_info)
    allNames.append(cfg.name);

  QCompleter *completer = new QCompleter(allNames, this);
  completer->setCaseSensitivity(Qt::CaseInsensitive);
  completer->setFilterMode(Qt::MatchContains);
  completer->setCompletionMode(QCompleter::PopupCompletion);

  ui_model->setCompleter(completer);

  connect(ui_model->lineEdit(), &QLineEdit::editingFinished, this, [this]()
  {
    QString text = ui_model->currentText();

    // already the current selection within the current vendor's list?
    for (int i = 0; i < ui_model->count(); ++i)
    {
      if (ui_model->itemText(i).compare(text, Qt::CaseInsensitive) == 0)
      {
        ui_model->setCurrentIndex(i);
        on_ui_model_activated(i);
        return;
      }
    }

    // otherwise search the full device list and switch vendor if needed
    for (size_t i = 0; i < dmm_info.size(); ++i)
    {
      if (dmm_info[i].name.compare(text, Qt::CaseInsensitive) == 0)
      {
        int vendorIdx = ui_vendor->findText(dmm_info[i].vendor);
        if (vendorIdx >= 0)
        {
          ui_vendor->setCurrentIndex(vendorIdx);
          populateModelsForVendor(dmm_info[i].vendor);
          int modelIdx = ui_model->findText(dmm_info[i].name);
          ui_model->setCurrentIndex(modelIdx);
          on_ui_model_activated(modelIdx);
        }
        return;
      }
    }

    ui_model->setCurrentIndex(-1);
  });
}

void MeterPrefs::populateModelsForVendor(const QString &vendor)
{
  ui_model->clear();

  m_currentVendorModels.clear();
  for (const auto& cfg : dmm_info)
    if (cfg.vendor == vendor)
      m_currentVendorModels.push_back(cfg);

  std::sort(m_currentVendorModels.begin(), m_currentVendorModels.end(), [](const auto& a, const auto& b) {
    return a.model < b.model;
  });

  for (const auto& cfg : m_currentVendorModels)
    ui_model->addItem(cfg.name);
}

void MeterPrefs::populateAllModels()
{
  ui_model->clear();

  // dmm_info is already sorted by name (vendor+model) in setupComboBoxModel().
  m_currentVendorModels = dmm_info;

  for (const auto& cfg : m_currentVendorModels)
    ui_model->addItem(cfg.name);
}

void MeterPrefs::on_ui_vendor_activated(int id)
{
  if (id == 0)
  {
    ui_model->clear();
    m_currentVendorModels.clear();
    enterManualMode();
  }
  else if (id == 1)
  {
    populateAllModels();
    if (!m_currentVendorModels.empty())
    {
      ui_model->setCurrentIndex(0);
      on_ui_model_activated(0);
    }
  }
  else
  {
    populateModelsForVendor(ui_vendor->itemText(id));
    if (!m_currentVendorModels.empty())
    {
      ui_model->setCurrentIndex(0);
      on_ui_model_activated(0);
    }
  }
}


void MeterPrefs::defaultsSLOT()
{
  QStringList portlist = Transport::availablePorts();
  m_portlist->setStringList(portlist);
  port->setModel(m_portlist);

  // >>> temporary solution to make rfc2217 useable
  QStringList list = m_portlist->stringList();
  for(int i=0; i<10; i++)
  {
    QString dev = cfgString(QString("Port settings/custom_device%1").arg(i), "");
    if (dev.size()>0)
      list.append(dev);
  }
  m_portlist->setStringList(list);

  // a port that is not there now (unplugged, a /dev/serial/by-id name
  // from "My devices") stays the choice
  QString device = cfgString("Port settings/device");
  if (device.section(' ', 0, 0).startsWith("ble", Qt::CaseInsensitive))
    device.clear();   // a Bluetooth meter: no serial port to keep
  if (!device.isEmpty() && !list.contains(device))
  {
    list.append(device);
    m_portlist->setStringList(list);
  }
  if (!device.isEmpty())
    port->setCurrentText(device);
  ui_sigrokConn->setCurrentText(cfgString("Port settings/sigrok-conn"));
  ui_sigrokOptions->setText   (cfgString("Port settings/sigrok-options"));
  ui_bleAddress->setCurrentText(cfgString("Port settings/ble-address"));
  ui_bleKey->setText          (cfgString("Port settings/ble-key"));
  updateBleFields();
  ui_calcUnit->setText        (cfgString("DMM/calc-unit", "W"));
  ui_calcExpression->setText  (cfgString("DMM/calc-expression"));
  ui_virtualSignal->setCurrentIndex(cfgInt("DMM/virtual-waveform", 2));
  ui_virtualUnit->setText     (cfgString("DMM/virtual-unit", "V"));
  ui_virtualCoupling->setCurrentText(cfgString("DMM/virtual-coupling", "DC"));
  ui_virtualMin->setText      (cfgString("DMM/virtual-min", "0"));
  ui_virtualMax->setText      (cfgString("DMM/virtual-max", "10"));
  ui_virtualPeriod->setText   (cfgString("DMM/virtual-period", "20"));
  ui_virtualNoise->setText    (cfgString("DMM/virtual-noise", "0"));
  if (ui_virtualSignal->currentIndex() == 7)
    ui_virtualFormula->setText(cfgString("DMM/virtual-formula"));
  baudRate->setCurrentText    (cfgString("Port settings/baud"));
  bitsCombo->setCurrentText   (cfgString("Port settings/bits", "7"));
  stopBitsCombo->setCurrentText(cfgString("Port settings/stop-bits", "1"));
  parityCombo->setCurrentIndex(cfgInt("Port settings/parity"));   // stored as index by applySLOT()
  selectDisplay(cfgString("DMM/display", "4000"));
  ui_externalSetup->setChecked(cfgBool("DMM/external-setup", false));

  uirts->setChecked(cfgBool("DMM/rts", true));
  uidtr->setChecked(cfgBool("DMM/dtr", false));

  selectFormat(formatFromSetting(cfgString("DMM/data-format", "Metex14")));
  ui_numValues->setValue(cfgInt("DMM/number-of-values", 1));

  QString model = cfgString("DMM/model");

  ui_vendor->setCurrentIndex(0);
  ui_model->clear();
  m_currentVendorModels.clear();

  for (const auto& cfg : dmm_info)
  {
    if (DmmDecoder::sameModel(model, cfg.name))
    {
      int vendorIdx = ui_vendor->findText(cfg.vendor);
      if (vendorIdx >= 0)
      {
        ui_vendor->setCurrentIndex(vendorIdx);
        populateModelsForVendor(cfg.vendor);
        int modelIdx = ui_model->findText(cfg.name);
        ui_model->setCurrentIndex(modelIdx);
      }
      break;
    }
  }

  if (ui_vendor->currentIndex() == 0)
    enterManualMode();
  else
    on_ui_model_activated(ui_model->currentIndex());

  // the settings read again (not an entry chosen here): which device is it
  if (m_override.isEmpty() && m_devices)
    ui_myDevice->setCurrentIndex(qMax(0, ui_myDevice->findData(m_cfg->getString("DMM/my-device"))));
}

void MeterPrefs::factoryDefaultsSLOT()
{
  port->setCurrentIndex(0);
  baudRate->setCurrentIndex(0);
  bitsCombo->setCurrentIndex(2);
  stopBitsCombo->setCurrentIndex(1);
  parityCombo->setCurrentIndex(0);
  displayCombo->setCurrentIndex(1);
  ui_externalSetup->setChecked(false);

  selectFormat(FrameFormat::Metex14);
  ui_numValues->setValue(1);
  ui_vendor->setCurrentIndex(0);
  ui_model->clear();
  m_currentVendorModels.clear();

  enterManualMode();
}

void MeterPrefs::applySLOT()
{
  // a Bluetooth meter has no port: the port field still held the one of the
  // meter before, and My devices showed it (the key stays in its own entry)
  const QString address = ui_bleAddress->currentText().section(' ', 0, 0).trimmed();
  m_cfg->setString("Port settings/device", isGatt()        ? "blegatt " + address
                                           : isBluetooth() ? "ble " + address
                                                           : port->currentText());
  m_cfg->setString("Port settings/sigrok-conn", ui_sigrokConn->currentText().trimmed());
  m_cfg->setString("Port settings/sigrok-options", ui_sigrokOptions->text().trimmed());
  m_cfg->setString("Port settings/ble-address", ui_bleAddress->currentText().trimmed());
  // only a Victron device has a key: another meter would carry it on into
  // its own entry of My devices
  m_cfg->setString("Port settings/ble-key", isBluetooth() && !isGatt() ? ui_bleKey->text().trimmed() : QString());
  m_cfg->setString("Port settings/ble-main", ui_bleMain->currentData().toString());
  m_cfg->setString("Port settings/ble-second", ui_bleSecond->currentData().toString());
  m_cfg->setString("DMM/calc-unit", ui_calcUnit->text().trimmed());
  m_cfg->setString("DMM/calc-expression", ui_calcExpression->text().trimmed());
  m_cfg->setInt("DMM/virtual-waveform", ui_virtualSignal->currentIndex());
  m_cfg->setString("DMM/virtual-unit", ui_virtualUnit->text().trimmed());
  m_cfg->setString("DMM/virtual-coupling", ui_virtualCoupling->currentText());
  m_cfg->setString("DMM/virtual-min", ui_virtualMin->text().trimmed());
  m_cfg->setString("DMM/virtual-max", ui_virtualMax->text().trimmed());
  m_cfg->setString("DMM/virtual-period", ui_virtualPeriod->text().trimmed());
  m_cfg->setString("DMM/virtual-noise", ui_virtualNoise->text().trimmed());
  m_cfg->setString("DMM/virtual-formula", ui_virtualFormula->text().trimmed());
  m_cfg->setString("Port settings/baud", baudRate->currentText());
  m_cfg->setString("Port settings/bits", bitsCombo->currentText());
  m_cfg->setString("Port settings/stop-bits", stopBitsCombo->currentText());
  m_cfg->setInt("Port settings/parity", parityCombo->currentIndex());

  m_cfg->setString("DMM/display", displayCombo->currentText());
  m_cfg->setBool("DMM/external-setup", ui_externalSetup->isChecked());

  m_cfg->setString("DMM/data-format", FrameFormat::toString(format()));
  m_cfg->setInt("DMM/number-of-values", ui_numValues->value());
  const int modelIdx = ui_model->currentIndex();
  const bool haveModel = ui_vendor->currentIndex() != 0
                         && modelIdx >= 0
                         && modelIdx < static_cast<int>(m_currentVendorModels.size());
  m_cfg->setString("DMM/model", haveModel ? m_currentVendorModels[modelIdx].name : "Manual");

  m_cfg->setBool("DMM/rts", uirts->isChecked());
  m_cfg->setBool("DMM/dtr", uidtr->isChecked());
  // the entry chosen here; whether the fields still are that meter, the
  // instance decides (InstanceWidget::syncDevice())
  if (m_devices)
    m_cfg->setString("DMM/my-device", ui_myDevice->currentData().toString());
}

void MeterPrefs::on_ui_externalSetup_toggled()
{
  if (ui_vendor->currentIndex() == 0)
  {
    baudRate->setDisabled(ui_externalSetup->isChecked());
    bitsCombo->setDisabled(ui_externalSetup->isChecked());
    stopBitsCombo->setDisabled(ui_externalSetup->isChecked());
    parityCombo->setDisabled(ui_externalSetup->isChecked());
  }
}

bool MeterPrefs::isSigrokMeter() const
{
  return ui_vendor->currentIndex() != 0 && m_dmmInfo.protocol == FrameFormat::Sigrok && !m_dmmInfo.sigrokDriver.isEmpty();
}

bool MeterPrefs::isBluetooth() const
{
#ifdef QTDMM_WITH_BLE
  return ui_vendor->currentIndex() != 0 && (m_dmmInfo.protocol == FrameFormat::VictronBLE || isGatt());
#else
  // no Bluetooth port in this build: still show the Bluetooth group for these
  // models, with a hint instead of the serial settings they don't have
  const ProtocolInfo *info = protocolInfo(m_dmmInfo.protocol);
  return ui_vendor->currentIndex() != 0 && m_dmmInfo.baud == 0 && info
         && QLatin1String(info->transport) == QLatin1String("Bluetooth LE");
#endif
}

// A meter QtDMM connects to over GATT (UT60BT): address only, no key and no
// choice of values - unlike the Victron broadcasts
bool MeterPrefs::isGatt() const
{
#ifdef QTDMM_WITH_BLE
  // baud 0: the Bluetooth entry of a protocol that also has a cable (UT61E+
  // with the UT-D07B adapter vs. the UT-D09 USB cable)
  return ui_vendor->currentIndex() != 0 && m_dmmInfo.baud == 0
         && BleGattDevice::profile(m_dmmInfo.protocol).has_value();
#else
  return false;
#endif
}

bool MeterPrefs::isCalculated() const
{
  return ui_vendor->currentIndex() != 0 && m_dmmInfo.vendor == "QtDMM" && m_dmmInfo.model == "Calculated value";
}

bool MeterPrefs::isVirtual() const
{
  return ui_vendor->currentIndex() != 0 && m_dmmInfo.vendor == "QtDMM" && m_dmmInfo.model == "Virtual meter";
}

void MeterPrefs::updateVirtualFormula()
{
  const int wave = ui_virtualSignal->currentIndex();
  const bool custom = wave == 7;
  ui_virtualFormula->setReadOnly(!custom);
  for (QWidget *w : std::initializer_list<QWidget *>{ui_virtualMin, ui_virtualMax, ui_virtualPeriod, ui_virtualNoise})
    w->setEnabled(!custom);
  ui_virtualPeriod->setEnabled(!custom && wave >= 2);
  if (!custom)
    ui_virtualFormula->setText(CalcExpr::waveformFormula(static_cast<CalcExpr::Waveform>(wave), ui_virtualMin->text(), ui_virtualMax->text(),
                                              ui_virtualPeriod->text(), ui_virtualNoise->text()));
  QString error;
  int pos = -1;
  const bool ok = CalcExpr::parse(ui_virtualFormula->text(), &error, &pos).has_value();
  ui_virtualFormula->setStyleSheet(ok ? QString() : QStringLiteral("QLineEdit { color: #b00; }"));
  ui_virtualFormula->setToolTip(ok ? QString() : tr("Position %1: %2").arg(pos + 1).arg(error));
}

void MeterPrefs::setStateManager(SharedStateManager *state)
{
  m_state = state;
}

// A calculated value has no port and no protocol to set up; the formula
// group takes their place. The port field then carries "calc <unit> <formula>"
// (see device()), so MeterConnection and Transport need no special settings keys.
void MeterPrefs::updateCalcMode()
{
  const bool calc = isCalculated();
  const bool virt = isVirtual();
  const bool ble = isBluetooth();
  const bool sigrok = isSigrokMeter();
  ButtonGroup11->setVisible(!calc && !virt && !ble && !sigrok);
  ui_protocol->setVisible(!calc && !virt);
  ui_calcGroup->setVisible(calc);
  ui_virtualGroup->setVisible(virt);
  ui_bleGroup->setVisible(ble);
  const bool gatt = isGatt();
  for (QWidget *w : std::initializer_list<QWidget *>{ ui_bleKeyLabel, ui_bleKey, ui_bleMainLabel, ui_bleMain,
                                                      ui_bleSecondLabel, ui_bleSecond })
    w->setVisible(ble && !gatt);
  if (ble)
    updateBleHint();
  ui_sigrokGroup->setVisible(sigrok);
  if (sigrok)
  {
    // the model's driver, unless the user typed another one for this model
    if (ui_sigrokDriver->property("model").toString() != m_dmmInfo.model)
    {
      ui_sigrokDriver->setProperty("model", m_dmmInfo.model);
      ui_sigrokDriver->setText(m_dmmInfo.sigrokDriver);
    }
    if (ui_sigrokConn->count() == 0)
    {
      // serial ports first; USB-TMC and LAN are typed in
      QStringList ports;
      for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
        ports << info.systemLocation();
      const QString current = ui_sigrokConn->currentText();
      ui_sigrokConn->addItems(ports);
      ui_sigrokConn->setCurrentText(current);
    }
    updateSigrokHint();
  }
  if (ble)
  {
    updateBleFields();
    updateBleHint();
  }
  if (virt)
    updateVirtualFormula();
  if (calc)
  {
    updateCalcHint();
    m_calcHintTimer.start();
  }
  else
    m_calcHintTimer.stop();
}

void MeterPrefs::updateCalcHint()
{
  QString error;
  int pos = -1;
  const auto expr = CalcExpr::parse(ui_calcExpression->text(), &error, &pos);
  QStringList lines;

  if (!expr)
  {
    if (!ui_calcExpression->text().trimmed().isEmpty())
      lines << QString("<span style=\"color:#b00\">%1</span>").arg(tr("Position %1: %2").arg(pos + 1).arg(error.toHtmlEscaped()));
  }
  else if (m_state)
  {
    const auto readings = m_state->readings();
    for (const QString &var : expr->variables())
    {
      QString id = var;
      for (auto it = readings.constBegin(); it != readings.constEnd(); ++it)
        if (it.key() == var || QString(it.key()).replace('-', '_') == var)
          id = it.key();
      if (readings.contains(id) && readings[id].valid)
        lines << QString("%1 = %2 %3").arg(var.toHtmlEscaped(), SiPrefix::format(readings[id].value), readings[id].unit.toHtmlEscaped());
      else if (readings.contains(id))
        lines << QString("%1 = OL").arg(var.toHtmlEscaped());
      else
        lines << QString("<span style=\"color:#b00\">%1</span>").arg(tr("%1: no such instance running").arg(var.toHtmlEscaped()));
    }
  }

  QStringList others;
  if (m_state)
    for (const QString &id : m_state->instances())
      if (id != m_state->id())
        others << id;
  if (!others.isEmpty())
    lines << tr("Running instances: %1").arg(others.join(", ").toHtmlEscaped());
  else
    lines << tr("No other instance is running.");

  ui_calcHint->setText(lines.join("<br>"));
}

void MeterPrefs::enterManualMode()
{
  ui_filename->setDisabled(false);
  ui_save->setDisabled(false);
  ui_load->setDisabled(false);

  baudRate->setDisabled(false);
  ui_protocol->setDisabled(false);
  ui_baudLabel->setDisabled(false);
  ui_bitsLabel->setDisabled(false);
  ui_stopLabel->setDisabled(false);
  ui_displayLabel->setDisabled(false);
  ui_parityLabel->setDisabled(false);
  bitsCombo->setDisabled(false);
  displayCombo->setDisabled(false);
  stopBitsCombo->setDisabled(false);
  parityCombo->setDisabled(false);
  ui_numValues->setDisabled(false);
  ui_externalSetup->setDisabled(false);
  uirts->setDisabled(false);
  uidtr->setDisabled(false);

  message->show();
  message2->hide();

  m_dmmInfo.name = "custom";
  m_dmmInfo.baud = baudRate->currentText().toInt();
  m_dmmInfo.protocol = format();
  m_dmmInfo.bits =  bitsCombo->currentText().toInt();
  m_dmmInfo.stopBits = stopBitsCombo->currentText().toInt();
  m_dmmInfo.parity = parityCombo->currentIndex();
  m_dmmInfo.display = displayCombo->currentText().toInt();
  m_dmmInfo.numValues = ui_numValues->value();
  m_dmmInfo.externalSetup = ui_externalSetup->isChecked();
  m_dmmInfo.rts = uirts->isChecked();
  m_dmmInfo.dtr = uidtr->isChecked();

  updateCalcMode();
}

void MeterPrefs::on_ui_model_activated(int id)
{
  if (id < 0 || id >= static_cast<int>(m_currentVendorModels.size()))
    return;

  ui_filename->setDisabled(true);
  ui_save->setDisabled(true);
  ui_load->setDisabled(true);

  baudRate->setDisabled(true);
  ui_protocol->setDisabled(true);
  ui_baudLabel->setDisabled(true);
  ui_bitsLabel->setDisabled(true);
  ui_stopLabel->setDisabled(true);
  ui_displayLabel->setDisabled(true);
  ui_parityLabel->setDisabled(true);
  bitsCombo->setDisabled(true);
  displayCombo->setDisabled(true);
  stopBitsCombo->setDisabled(true);
  parityCombo->setDisabled(true);
  ui_numValues->setDisabled(true);
  ui_externalSetup->setDisabled(true);
  uirts->setDisabled(true);
  uidtr->setDisabled(true);

  message->hide();
  if (ui_model->itemText(id).contains('*'))   // "Model *": settings from chip data, unconfirmed
    message2->show();
  else
    message2->hide();

  m_dmmInfo = m_currentVendorModels[id];

  baudRate->setCurrentText(QString::number(m_currentVendorModels[id].baud));
  selectFormat(m_currentVendorModels[id].protocol);
  bitsCombo->setCurrentText(QString::number(m_currentVendorModels[id].bits));
  stopBitsCombo->setCurrentText(QString::number(m_currentVendorModels[id].stopBits));
  parityCombo->setCurrentIndex(m_currentVendorModels[id].parity);
  selectDisplay(QString::number(m_currentVendorModels[id].display));
  ui_numValues->setValue(m_currentVendorModels[id].numValues);
  ui_externalSetup->setChecked(m_currentVendorModels[id].externalSetup);
  uirts->setChecked(m_currentVendorModels[id].rts);
  uidtr->setChecked(m_currentVendorModels[id].dtr);

  ui_filename->setText("");
  updateCalcMode();
}

bool MeterPrefs::rts() const
{
  return uirts->isChecked();
}

bool MeterPrefs::dtr() const
{
  return uidtr->isChecked();
}

QSerialPort::Parity MeterPrefs::parity() const
{
  QSerialPort::Parity rValue;
  switch (parityCombo->currentIndex())
  {
    case 0:
      rValue = QSerialPort::NoParity;
      break;
    case 1:
      rValue = QSerialPort::EvenParity;
      break;
    case 2:
      rValue = QSerialPort::OddParity;
      break;
    default:
      qWarning() << "Wrong parity value. Using None";
      rValue = QSerialPort::NoParity;
      break;
  }
  return rValue;
}

QSerialPort::DataBits MeterPrefs::bits() const
{
  return static_cast<QSerialPort::DataBits>(5 + bitsCombo->currentIndex());
}

QSerialPort::StopBits MeterPrefs::stopBits() const
{
  return static_cast<QSerialPort::StopBits>(1 + stopBitsCombo->currentIndex());
}

int MeterPrefs::speed() const
{
  return baudRate->currentText().toInt();
}

bool MeterPrefs::externalSetup() const
{
  return ui_externalSetup->isChecked();
}

int MeterPrefs::numValues() const
{
  return ui_numValues->value();
}

FrameFormat::DataFormat MeterPrefs::format() const
{
  return static_cast<FrameFormat::DataFormat>(protocolCombo->currentData().toInt());
}

// The counts combo is not editable; a value it does not list (a new model's
// counts, or a hand-edited config) would silently leave the previous entry
// selected - so unknown values are added instead.
void MeterPrefs::selectDisplay(const QString &counts)
{
  int idx = displayCombo->findText(counts);
  if (idx < 0)
  {
    displayCombo->addItem(counts);
    idx = displayCombo->count() - 1;
  }
  displayCombo->setCurrentIndex(idx);
}

int MeterPrefs::display() const
{
  return displayCombo->currentText().toInt();
}

QString MeterPrefs::dmmName() const
{
  return ui_model->currentText();
}

// The values a Victron model offers; the selection survives a model change
// when the new model has the same field, otherwise the defaults are taken.
void MeterPrefs::updateBleFields()
{
  const quint8 type = VictronBle::readoutTypeForModel(m_dmmInfo.model);
  // the combos are empty until the model is known (defaultsSLOT runs
  // before it), so the saved choice is the fallback
  QString main = ui_bleMain->currentData().toString();
  QString second = ui_bleSecond->currentData().toString();
  if (main.isEmpty())
  {
    main = m_cfg->getString("Port settings/ble-main");
    second = m_cfg->getString("Port settings/ble-second");
  }
  const QList<VictronBle::Field> fields = VictronBle::fields(type);
  if (ui_bleMain->property("readoutType").toInt() == type && !fields.isEmpty())
    return;
  ui_bleMain->setProperty("readoutType", type);
  ui_bleMain->clear();
  ui_bleSecond->clear();
  ui_bleSecond->addItem(tr("none"), "-");
  for (const VictronBle::Field &f : fields)
  {
    const QString label = QCoreApplication::translate("VictronBle", f.label);
    ui_bleMain->addItem(label, QString::fromLatin1(f.id));
    ui_bleSecond->addItem(label, QString::fromLatin1(f.id));
  }
  ui_bleMain->setCurrentIndex(qMax(0, ui_bleMain->findData(main)));
  const int secondIndex = ui_bleSecond->findData(second);
  ui_bleSecond->setCurrentIndex(secondIndex > 0 ? secondIndex : qMin(2, ui_bleSecond->count() - 1));
}

namespace
{
// What this sigrok-cli offers, asked once per executable path: its version
// line and the drivers of --list-supported. An empty version means it did
// not run.
struct SigrokCli
{
  QString version;
  QStringList drivers;
};

const SigrokCli &sigrokCli(const QString &exe)
{
  static QMap<QString, SigrokCli> cache;
  if (cache.contains(exe))
    return cache[exe];
  SigrokCli info;
  QProcess p;
  p.start(exe, {"--version"});
  if (p.waitForFinished(3000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)
  {
    info.version = QString::fromUtf8(p.readAllStandardOutput()).section('\n', 0, 0).trimmed();
    QProcess l;
    l.start(exe, {"--list-supported"});
    if (l.waitForFinished(5000))
    {
      // "  scpi-dmm             SCPI DMM" lines under "Supported hardware drivers:"
      bool inDrivers = false;
      for (const QString &line : QString::fromUtf8(l.readAllStandardOutput()).split('\n'))
      {
        if (line.startsWith("Supported hardware drivers"))
          inDrivers = true;
        else if (!line.startsWith(' '))
          inDrivers = false;
        else if (inDrivers)
          info.drivers << line.trimmed().section(' ', 0, 0);
      }
    }
  }
  cache[exe] = info;
  return cache[exe];
}
}

void MeterPrefs::updateSigrokHint()
{
  const QString exe = m_cfg->getString("Port settings/sigrok_exe", "sigrok-cli");
  const SigrokCli &cli = sigrokCli(exe);
  const QString driver = ui_sigrokDriver->text().trimmed();
  QString hint;
  if (cli.version.isEmpty())
    hint = tr("%1 was not found or does not run. Install sigrok-cli, or set its path under "
              "<a href=\"ports\">Special ports</a>.").arg(exe.toHtmlEscaped());
  else if (!driver.isEmpty() && !cli.drivers.isEmpty() && !cli.drivers.contains(driver))
    hint = tr("%1 has no driver \"%2\"; see sigrok-cli --list-supported.").arg(cli.version.toHtmlEscaped(), driver.toHtmlEscaped());
  else
    hint = tr("%1 found.").arg(cli.version.toHtmlEscaped());
  ui_sigrokHint->setText(hint);
  ui_sigrokTest->setEnabled(!cli.version.isEmpty());
}

void MeterPrefs::on_ui_sigrokTest_clicked()
{
  const QString exe = m_cfg->getString("Port settings/sigrok_exe", "sigrok-cli");
  const QString spec = device().section(' ', 1);   // "<driver>:conn=..."
  ui_sigrokTest->setEnabled(false);
  ui_sigrokHint->setText(tr("Running %1 --driver %2 --scan ...").arg(exe.toHtmlEscaped(), spec.toHtmlEscaped()));
  QCoreApplication::processEvents();
  QProcess p;
  p.start(exe, {"--driver", spec, "--scan"});
  // a serial SCPI scan runs through several timeouts before giving up;
  // keep the dialog alive meanwhile
  QElapsedTimer t;
  t.start();
  bool finished = false;
  while (!(finished = p.waitForFinished(100)) && t.elapsed() < 30000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  if (!finished)
  {
    p.kill();
    p.waitForFinished(1000);
  }
  const QString out = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError()).trimmed();
  QString found;
  for (const QString &line : out.split('\n'))
    if (line.contains(spec.section(':', 0, 0) + ':') || line.contains(" - "))
      found = line.trimmed();
  if (!finished)
    ui_sigrokHint->setText(tr("No meter answered within 30 s (a serial port without a SCPI meter keeps sigrok-cli waiting)."));
  else if (p.exitCode() == 0 && !found.isEmpty())
    ui_sigrokHint->setText(tr("Found: %1").arg(found.toHtmlEscaped()));
  else
    ui_sigrokHint->setText(tr("No meter answered.") + (out.isEmpty() ? QString() : "<br><tt>" + out.toHtmlEscaped().replace('\n', "<br>") + "</tt>"));
  ui_sigrokTest->setEnabled(true);
}

// "ble <address> <key> <main> <second>", see BleAdvertisementDevice
void MeterPrefs::updateBleHint()
{
#ifndef QTDMM_WITH_BLE
  ui_bleScan->setEnabled(false);
  ui_bleHint->setText(tr("This QtDMM was built without Bluetooth support, so it cannot connect to this meter."));
  return;
#endif
  const QString address = ui_bleAddress->currentText().section(' ', 0, 0).trimmed();
  const bool keyOk = VictronBle::keyFromHex(ui_bleKey->text()).size() == 16;
  QStringList hints;
  if (address.isEmpty())
    hints << tr("Pick the device or type its Bluetooth address.");
  if (isGatt())
  {
    hints << tr("Switch the meter's Bluetooth on (it shows the Bluetooth symbol) before scanning or connecting.");
    ui_bleHint->setText(hints.join(' '));
    return;
  }
  if (!keyOk)
    hints << tr("The key is the 32-digit \"Encryption key\" VictronConnect shows under Product info, Instant readout via Bluetooth.");
  ui_bleHint->setText(hints.join(' '));
  ui_bleKey->setStyleSheet(keyOk || ui_bleKey->text().trimmed().isEmpty() ? QString() : QStringLiteral("color: red"));
}

void MeterPrefs::on_ui_bleScan_clicked()
{
#ifdef QTDMM_WITH_BLE
  ui_bleScan->setEnabled(false);
  const bool gatt = isGatt();
  ui_bleHint->setText(gatt ? tr("Scanning for %1 (6 s)...").arg(m_dmmInfo.model.section(' ', 0, 0))
                           : tr("Scanning for Victron devices (5 s)..."));
  QCoreApplication::processEvents();
  const QStringList found = gatt ? BleGattDevice::scan(m_dmmInfo.protocol, 6000) : BleAdvertisementDevice::scan(5000);
  const QString current = ui_bleAddress->currentText();
  ui_bleAddress->clear();
  ui_bleAddress->addItems(found);
  if (!current.isEmpty())
    ui_bleAddress->setCurrentText(current);   // keep what was configured, found or not
  ui_bleScan->setEnabled(true);
  if (found.isEmpty())
  {
    ui_bleHint->setText(gatt ? tr("No meter found. Is its Bluetooth switched on, and no other program connected to it?")
                             : tr("No Victron device found. Is Bluetooth on, and Instant readout enabled on the device?"));
    return;
  }
#endif
  updateBleHint();
}

QString MeterPrefs::device() const
{
  if (isSigrokMeter())
  {
    // "SIGROK <driver>:conn=<connection>[:<options>]" - what the Special
    // ports page would take as a custom entry
    QString spec = ui_sigrokDriver->text().trimmed();
    const QString conn = ui_sigrokConn->currentText().trimmed();
    if (!conn.isEmpty())
      spec += ":conn=" + conn;
    const QString options = ui_sigrokOptions->text().simplified().remove(' ');
    if (!options.isEmpty())
      spec += ":" + options;
    return "SIGROK " + spec;
  }
  if (isGatt())
    return QString("blegatt %1").arg(ui_bleAddress->currentText().section(' ', 0, 0).trimmed());
  if (isBluetooth())
    return QString("ble %1 %2 %3 %4").arg(ui_bleAddress->currentText().section(' ', 0, 0).trimmed(),
                                          ui_bleKey->text().simplified().remove(' '),
                                          ui_bleMain->currentData().toString(),
                                          ui_bleSecond->currentData().toString());
  if (isCalculated())
    return QString("calc %1 %2").arg(ui_calcUnit->text().trimmed(), ui_calcExpression->text().trimmed());
  if (isVirtual())
    return QString("calc %1/%2 %3").arg(ui_virtualUnit->text().trimmed(), ui_virtualCoupling->currentText(),
                                        ui_virtualFormula->text().trimmed());
  return port->currentText();
}

void MeterPrefs::on_ui_load_clicked()
{
  QString filename = QFileDialog::getOpenFileName(this, tr("Load DMM description"), m_path,	tr("DMM description (*.cfg)"));

  if (!filename.isNull())
  {
    QFileInfo info(filename);
    m_path = info.filePath();
    ui_filename->setText(info.fileName());

    QSettings cfg(filename, QSettings::IniFormat);

    port->setCurrentText(cfg.value("Port settings/device", "").toString());
    baudRate->setCurrentText(cfg.value("Port settings/baud", "9600").toString());
    bitsCombo->setCurrentText(cfg.value("Port settings/bits", "7").toString());
    stopBitsCombo->setCurrentText(cfg.value("Port settings/stop-bits", "2").toString());
    parityCombo->setCurrentIndex(cfg.value("Port settings/parity", 0).toInt());
    selectDisplay(cfg.value("DMM/display", "4000").toString());
    ui_externalSetup->setChecked(cfg.value("DMM/external-setup", false).toBool());
    selectFormat(formatFromSetting(cfg.value("DMM/data-format", "Metex14")));
    ui_numValues->setValue(cfg.value("DMM/number-of-values", 1).toInt());
    uirts->setChecked(cfg.value("DMM/rts", true).toBool());
    uidtr->setChecked(cfg.value("DMM/dtr", false).toBool());
  }
}

void MeterPrefs::on_ui_save_clicked()
{
  QString filename = QFileDialog::getSaveFileName(this, tr("Save DMM description"), m_path, tr("DMM description (*.cfg)"));
  if (!filename.isNull())
  {
    QFileInfo info(filename);
    m_path = info.filePath();
    ui_filename->setText(info.fileName());

    QSettings cfg(filename, QSettings::IniFormat);
    cfg.setValue("Port settings/device", port->currentText());
    cfg.setValue("Port settings/baud", baudRate->currentText());
    cfg.setValue("Port settings/bits", bitsCombo->currentText());
    cfg.setValue("Port settings/stop-bits", stopBitsCombo->currentText());
    cfg.setValue("Port settings/parity", parityCombo->currentIndex());

    cfg.setValue("DMM/display", displayCombo->currentText());
    cfg.setValue("DMM/external-setup", ui_externalSetup->isChecked());
    cfg.setValue("DMM/data-format", FrameFormat::toString(format()));
    cfg.setValue("DMM/number-of-values", ui_numValues->value());

    cfg.setValue("DMM/rts", uirts->isChecked());
    cfg.setValue("DMM/dtr", uidtr->isChecked());
  }
}

QString MeterPrefs::cfgString(const QString &key, const QString &def) const
{
  return m_override.contains(key) ? m_override.value(key).toString() : m_cfg->getString(key, def);
}

int MeterPrefs::cfgInt(const QString &key, int def) const
{
  return m_override.contains(key) ? m_override.value(key).toInt() : m_cfg->getInt(key, def);
}

bool MeterPrefs::cfgBool(const QString &key, bool def) const
{
  return m_override.contains(key) ? m_override.value(key).toBool() : m_cfg->getBool(key, def);
}

void MeterPrefs::setDeviceLibrary(DeviceLibrary *library)
{
  m_devices = library;
  connect(m_devices, &DeviceLibrary::changed, this, &MeterPrefs::fillDevices);
  fillDevices();
}

void MeterPrefs::fillDevices()
{
  if (!m_devices || !m_cfg)
    return;
  const QString keep = ui_myDevice->currentData().toString();
  const QList<MyDevice> all = m_devices->list();
  ui_myDevice->clear();
  ui_myDevice->addItem(all.isEmpty() ? tr("(none saved yet)") : tr("(choose)"), QString());
  for (const MyDevice &d : all)
  {
    // the name only: a formula or a by-id path would make the page wide
    ui_myDevice->addItem(d.name, d.id);
    QString detail = d.model();
    if (!d.where().isEmpty())
      detail += QString(" · %1").arg(d.where());
    ui_myDevice->setItemData(ui_myDevice->count() - 1, detail, Qt::ToolTipRole);
  }
  ui_myDevice->setEnabled(!all.isEmpty());
  // the one chosen on the page, else the one this instance uses
  int index = ui_myDevice->findData(keep.isEmpty() ? m_cfg->getString("DMM/my-device") : keep);
  ui_myDevice->setCurrentIndex(qMax(0, index));
}

void MeterPrefs::onDeviceChosen(int index)
{
  const std::optional<MyDevice> device = m_devices ? m_devices->find(ui_myDevice->itemData(index).toString())
                                                   : std::nullopt;
  if (!device)
    return;
  m_override = device->keys;
  defaultsSLOT();
  m_override.clear();
}

void MeterPrefs::saveDevice()
{
  if (!m_devices)
    return;
  // the fields as they are, staged like OK would (Cancel drops them again)
  applySLOT();
  QVariantMap keys = m_cfg->meterKeys(true);
  keys.remove("DMM/configured");
  keys.insert("Port settings/device", SerialDevice::stableDevice(keys.value("Port settings/device").toString()));

  const std::optional<MyDevice> chosen = m_devices->find(ui_myDevice->currentData().toString());
  if (chosen)
  {
    QMessageBox box(QMessageBox::Question, tr("My devices"),
                    tr("Update \"%1\" with these settings, or keep them as a new device?").arg(chosen->name),
                    QMessageBox::Cancel, this);
    QPushButton *update = box.addButton(tr("&Update"), QMessageBox::AcceptRole);
    QPushButton *asNew = box.addButton(tr("&New device..."), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == update)
    {
      m_devices->update(chosen->id, keys);
      return;
    }
    if (box.clickedButton() != asNew)
      return;
  }
  bool ok = false;
  const QString name = QInputDialog::getText(this, tr("Save to my devices"), tr("Name:"), QLineEdit::Normal,
                                             m_devices->uniqueName(dmmName()), &ok);
  if (!ok || name.trimmed().isEmpty())
    return;
  const QString id = m_devices->add(name.trimmed(), keys);
  ui_myDevice->setCurrentIndex(qMax(0, ui_myDevice->findData(id)));
}
