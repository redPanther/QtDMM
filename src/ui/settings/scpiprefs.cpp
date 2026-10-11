// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/settings/scpiprefs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHostInfo>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/settings.h"

ScpiPrefs::ScpiPrefs(QWidget *parent) : SettingsPage(parent)
{
  m_label = tr("SCPI server");
  m_description = tr("Lets other programs read the meter over the network, like a bench instrument.");
  m_iconName = "network-server";

  QFormLayout *form = createForm();
  addSection(form, tr("Server"));
  QWidget *group = this;
  m_enabled = new QCheckBox(tr("&Enable the SCPI server"), group);
  m_enabled->setToolTip(tr("QtDMM answers SCPI queries (*IDN?, READ?, ...) on a TCP port. With the server on, "
                           "lxi-tools, LabVIEW, PyVISA or a few lines of Python can read the current value as "
                           "if the multimeter were a bench instrument with a LAN port. The server only reports; "
                           "it never sends anything to the meter. INITiate/ABORt start and stop a recording, "
                           "INPut ON/OFF connect and disconnect."));
  form->addRow(QString(), m_enabled);
  m_port = new QSpinBox(group);
  m_port->setRange(1, 65535);
  m_port->setValue(5025);
  m_port->setToolTip(tr("5025 is the usual raw-socket SCPI port. When it is taken (another QtDMM "
                        "instance), the next free one is used and shown in the status bar."));
  auto *portLabel = new QLabel(tr("&Port:"), group);
  portLabel->setBuddy(m_port);
  form->addRow(portLabel, m_port);
  m_bind = new QComboBox(group);
  m_bind->addItem(tr("This computer only (localhost)"));
  m_bind->addItem(tr("All network interfaces"));
  m_bind->setToolTip(tr("There is no authentication: anyone who can reach the port can read the "
                        "meter and start a recording. Open it to the network only where you trust it."));
  auto *bindLabel = new QLabel(tr("&Listen on:"), group);
  bindLabel->setBuddy(m_bind);
  form->addRow(bindLabel, m_bind);
  m_hislip = new QCheckBox(tr("Also &HiSLIP, for VISA (NI-VISA, LabVIEW, PyVISA)"), group);
  m_hislip->setToolTip(tr("HiSLIP is the LAN protocol VISA libraries prefer: the same SCPI commands, "
                          "addressed as TCPIP::host::hislip0::INSTR. It needs no extra rights."));
  form->addRow(QString(), m_hislip);
  m_hislipPort = new QSpinBox(group);
  m_hislipPort->setRange(1, 65535);
  m_hislipPort->setValue(4880);
  m_hislipPort->setToolTip(tr("4880 is the HiSLIP port VISA tries by default. When it is taken (another QtDMM "
                              "instance), the next free one is used; the address then names it: "
                              "TCPIP::host::hislip0,4881::INSTR."));
  auto *hislipLabel = new QLabel(tr("HiSLIP p&ort:"), group);
  hislipLabel->setBuddy(m_hislipPort);
  form->addRow(hislipLabel, m_hislipPort);
  m_mdns = new QCheckBox(tr("&Announce by mDNS (_scpi-raw._tcp, _hislip._tcp), so lxi discover finds it"), group);
  form->addRow(QString(), m_mdns);

  m_hint = new QLabel(this);
  m_hint->setWordWrap(true);
  m_hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
  form->addRow(QString(), m_hint);

  m_status = new QLabel(this);
  m_status->setWordWrap(true);
  form->addRow(QString(), m_status);

  connect(m_enabled, &QCheckBox::toggled, this, &ScpiPrefs::updateHint);
  connect(m_bind, &QComboBox::currentIndexChanged, this, &ScpiPrefs::updateHint);
  connect(m_port, &QSpinBox::valueChanged, this, &ScpiPrefs::updateHint);
  connect(m_hislip, &QCheckBox::toggled, this, &ScpiPrefs::updateHint);
  connect(m_hislipPort, &QSpinBox::valueChanged, this, &ScpiPrefs::updateHint);
  connect(m_hislip, &QCheckBox::toggled, this, [this](bool on) { m_hislipPort->setEnabled(on && m_enabled->isChecked()); });
  connect(m_enabled, &QCheckBox::toggled, this, [this](bool on)
  {
    m_port->setEnabled(on);
    m_bind->setEnabled(on);
    m_hislip->setEnabled(on);
    m_hislipPort->setEnabled(on && m_hislip->isChecked());
    m_mdns->setEnabled(on && m_bind->currentIndex() == 1);
  });
  connect(m_bind, &QComboBox::currentIndexChanged, this, [this](int idx)
  {
    m_mdns->setEnabled(m_enabled->isChecked() && idx == 1);
  });
  updateHint();
}

bool ScpiPrefs::enabled() const { return m_enabled->isChecked(); }
int ScpiPrefs::port() const { return m_port->value(); }
bool ScpiPrefs::allInterfaces() const { return m_bind->currentIndex() == 1; }
bool ScpiPrefs::mdns() const { return m_mdns->isChecked() && allInterfaces(); }
bool ScpiPrefs::hislip() const { return m_hislip->isChecked(); }
int ScpiPrefs::hislipPort() const { return m_hislipPort->value(); }

void ScpiPrefs::setStatus(const QString &text)
{
  m_status->setText(text);
}

void ScpiPrefs::updateHint()
{
  if (!m_enabled->isChecked())
  {
    m_hint->clear();
    return;
  }
  const QString host = m_bind->currentIndex() == 1 ? QHostInfo::localHostName() : QString("localhost");
  QString text = tr("Try it: <code>lxi scpi -a %1 -p %2 \"*IDN?\"</code> or "
                    "<code>printf 'READ?\\n' | nc %1 %2</code>").arg(host).arg(m_port->value());
  if (m_hislip->isChecked())
  {
    const QString port = m_hislipPort->value() == 4880 ? QString() : QString(",%1").arg(m_hislipPort->value());
    text += "<br>" + tr("VISA address: <code>%1</code>").arg(QString("TCPIP::%1::hislip0%2::INSTR").arg(host, port));
  }
  m_hint->setText(text);
}

void ScpiPrefs::defaultsSLOT()
{
  m_enabled->setChecked(m_cfg->getBool("Scpi/enabled", false));
  m_port->setValue(m_cfg->getInt("Scpi/port", 5025));
  m_bind->setCurrentIndex(m_cfg->getBool("Scpi/all-interfaces", false) ? 1 : 0);
  m_mdns->setChecked(m_cfg->getBool("Scpi/mdns", true));
  m_hislip->setChecked(m_cfg->getBool("Scpi/hislip", false));
  m_hislipPort->setValue(m_cfg->getInt("Scpi/hislip-port", 4880));
  m_enabled->toggled(m_enabled->isChecked());
  updateHint();
}

void ScpiPrefs::factoryDefaultsSLOT()
{
  m_enabled->setChecked(false);
  m_port->setValue(5025);
  m_bind->setCurrentIndex(0);
  m_mdns->setChecked(true);
  m_hislip->setChecked(false);
  m_hislipPort->setValue(4880);
  updateHint();
}

void ScpiPrefs::applySLOT()
{
  m_cfg->setBool("Scpi/enabled", m_enabled->isChecked());
  m_cfg->setInt("Scpi/port", m_port->value());
  m_cfg->setBool("Scpi/all-interfaces", m_bind->currentIndex() == 1);
  m_cfg->setBool("Scpi/mdns", m_mdns->isChecked());
  m_cfg->setBool("Scpi/hislip", m_hislip->isChecked());
  m_cfg->setInt("Scpi/hislip-port", m_hislipPort->value());
}
