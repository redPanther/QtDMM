//======================================================================
// File:		meterprefs.cpp
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


#include <QtWidgets>

#include "ui/settings/meterprefs.h"
#include "ui/devicesettings.h"
#include "device/transports/serial.h"
#include "device/transport.h"
#include "core/devicelibrary.h"
#include "core/settings.h"

MeterPrefs::MeterPrefs(QWidget *parent) : SettingsPage(parent)
{
  m_label = tr("Multimeter");
  m_description = tr("<b>Here you can configure the serial port"
                     " and protocol for your DMM. There is"
                     " also a number of predefined models.</b>");
  m_iconName = "qtdmm-dmm";

  auto *top = new QVBoxLayout(this);

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
  top->addLayout(devices);
  connect(ui_myDevice, &QComboBox::activated, this, &MeterPrefs::onDeviceChosen);
  connect(ui_saveDevice, &QPushButton::clicked, this, &MeterPrefs::saveDevice);
  ui_myDevice->setEnabled(false);

  m_settings = new DeviceSettings(this);
  m_settings->layout()->setContentsMargins(0, 0, 0, 0);
  top->addWidget(m_settings);
  connect(m_settings, &DeviceSettings::showPortsPage, this, &MeterPrefs::showPortsPage);
}

void MeterPrefs::setStateManager(SharedStateManager *state)
{
  m_settings->setStateManager(state);
}

QStringList MeterPrefs::availablePorts(Settings *cfg)
{
  // >>> temporary solution to make rfc2217 useable
  QStringList ports = Transport::availablePorts();
  for (int i = 0; i < 10; i++)
  {
    const QString dev = cfg->getString(QString("Port settings/custom_device%1").arg(i), "");
    if (dev.size() > 0)
      ports.append(dev);
  }
  return ports;
}

void MeterPrefs::loadFields(const QVariantMap &keys)
{
  m_settings->setPorts(availablePorts(m_cfg));
  m_settings->setSigrokExe(m_cfg->getString("Port settings/sigrok_exe", "sigrok-cli"));
  m_settings->load(keys);
}

void MeterPrefs::defaultsSLOT()
{
  loadFields(m_cfg->meterKeys());
  // the settings read again (not an entry chosen here): which device is it
  if (m_devices)
    ui_myDevice->setCurrentIndex(qMax(0, ui_myDevice->findData(m_cfg->getString("DMM/my-device"))));
}

void MeterPrefs::factoryDefaultsSLOT()
{
  m_settings->factoryDefaults();
}

void MeterPrefs::applySLOT()
{
  m_cfg->setValues(m_settings->keys());
  // the entry chosen here; whether the fields still are that meter, the
  // instance decides (InstanceWidget::syncDevice())
  if (m_devices)
    m_cfg->setString("DMM/my-device", ui_myDevice->currentData().toString());
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
  // the entry's keys over the instance's: the transport's other fields keep
  // what the instance has
  QVariantMap keys = m_cfg->meterKeys();
  for (auto it = device->keys.cbegin(); it != device->keys.cend(); ++it)
    keys.insert(it.key(), it.value());
  loadFields(keys);
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
                                             m_devices->uniqueName(m_settings->dmmName()), &ok);
  if (!ok || name.trimmed().isEmpty())
    return;
  const QString id = m_devices->add(name.trimmed(), keys);
  ui_myDevice->setCurrentIndex(qMax(0, ui_myDevice->findData(id)));
}
