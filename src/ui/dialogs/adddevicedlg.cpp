// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/adddevicedlg.h"

#include <QtWidgets>

#include "core/devicelibrary.h"
#include "device/protocols.h"
#include "ui/devicesettings.h"

namespace
{
const QSize kTileSize(180, 120);
const QSize kTargetSize(230, 140);
}

AddDeviceDlg::AddDeviceDlg(DeviceLibrary *library, QWidget *parent)
  : QDialog(parent)
  , m_library(library)
{
  setWindowTitle(tr("Add device"));

  auto *top = new QVBoxLayout(this);
  m_title = new QLabel(this);
  QFont bold = m_title->font();
  bold.setBold(true);
  bold.setPointSizeF(bold.pointSizeF() * 1.2);
  m_title->setFont(bold);
  top->addWidget(m_title);
  m_pages = new QStackedWidget(this);
  top->addWidget(m_pages, 1);

  // page 1: one tile per kind of connection; a click goes on
  auto *connection = new QWidget(m_pages);
  auto *tiles = new QGridLayout(connection);
  const struct { Connection id; const char *icon; QString text; QString toolTip; } kinds[] = {
    { Cable,     "drive-removable-media-usb",    tr("&Cable") + '\n' + tr("UT61E, UT803, TFA ..."),
      tr("A meter on a USB cable, a USB-serial adapter or a serial port") },
    { Bluetooth, "preferences-system-bluetooth", tr("Bl&uetooth") + '\n' + tr("UT60BT, Victron"),
      tr("A meter or a Victron device over Bluetooth LE") },
    { Network,   "network-server",               tr("Net&work") + '\n' + tr("qtdmm-bridge"),
      tr("A meter at another computer, through qtdmm-bridge or another RFC 2217 server") },
    { Sigrok,    "qtdmm-sigrok",                 tr("sig&rok") + '\n' + tr("Bench meters"),
      tr("A bench meter read through sigrok-cli") },
    { Simulated, "code-function",                tr("&Simulated / calculated") + '\n' + tr("To try out, formulas"),
      tr("A simulated meter to try QtDMM, or a value calculated from the readings of other windows") },
  };
  int column = 0;
  for (const auto &kind : kinds)
  {
    QToolButton *button = tile(kind.icon, kind.text, kind.toolTip);
    button->setObjectName(QString("ui_connection%1").arg(int(kind.id)));
    const Connection id = kind.id;
    connect(button, &QToolButton::clicked, this, [this, id] { chooseConnection(id); });
    tiles->addWidget(button, column / 3, column % 3);
    ++column;
  }
  tiles->setRowStretch(2, 1);
  m_pages->addWidget(connection);

  // page 3: the name, then the settings of the meter
  auto *device = new QWidget(m_pages);
  auto *deviceLayout = new QVBoxLayout(device);
  deviceLayout->setContentsMargins(0, 0, 0, 0);
  auto *nameRow = new QFormLayout;
  m_name = new QLineEdit(device);
  m_name->setObjectName("ui_name");
  m_name->setToolTip(tr("The name in My devices"));
  nameRow->addRow(tr("Na&me:"), m_name);
  deviceLayout->addLayout(nameRow);
  m_settings = new DeviceSettings(device);
  m_settings->layout()->setContentsMargins(0, 0, 0, 0);
  m_settings->setDescriptionFilesVisible(false);
  deviceLayout->addWidget(m_settings, 1);
  connect(m_settings, &DeviceSettings::changed, this, &AddDeviceDlg::suggestName);
  connect(m_settings, &DeviceSettings::changed, this, &AddDeviceDlg::updateButtons);
  connect(m_name, &QLineEdit::textChanged, this, &AddDeviceDlg::updateButtons);
  m_pages->addWidget(device);

  // page 4: where it goes; a click finishes
  auto *target = new QWidget(m_pages);
  auto *targets = new QHBoxLayout(target);
  m_thisWindow = tile("go-next", QString(), tr("The device takes the place of the one in this window"));
  m_thisWindow->setObjectName("ui_thisWindow");
  m_thisWindow->setFixedSize(kTargetSize);
  m_newWindow = tile("window-new", tr("In a ne&w window") + '\n' + tr("with the settings of this one"),
                     tr("A new window of its own for the device; this one keeps its meter"));
  m_newWindow->setObjectName("ui_newWindow");
  m_newWindow->setFixedSize(kTargetSize);
  connect(m_thisWindow, &QToolButton::clicked, this, [this] { finish(ThisWindow); });
  connect(m_newWindow, &QToolButton::clicked, this, [this] { finish(NewWindow); });
  targets->addStretch();
  targets->addWidget(m_thisWindow);
  targets->addWidget(m_newWindow);
  targets->addStretch();
  m_pages->addWidget(target);
  setCurrentDevice(QString());

  auto *buttons = new QDialogButtonBox(this);
  m_back = buttons->addButton(tr("< &Back"), QDialogButtonBox::ActionRole);
  m_next = buttons->addButton(tr("&Next >"), QDialogButtonBox::ActionRole);
  m_next->setDefault(true);
  buttons->addButton(QDialogButtonBox::Cancel);
  connect(m_back, &QPushButton::clicked, this, &AddDeviceDlg::back);
  connect(m_next, &QPushButton::clicked, this, &AddDeviceDlg::next);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  top->addWidget(buttons);

  showPage(ConnectionPage);
  resize(660, 560);
}

QToolButton *AddDeviceDlg::tile(const QString &icon, const QString &text, const QString &toolTip)
{
  auto *button = new QToolButton(this);
  button->setIcon(QIcon::fromTheme(icon));
  button->setIconSize(QSize(48, 48));
  button->setText(text);
  button->setToolTip(toolTip);
  button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  button->setFixedSize(kTileSize);
  return button;
}

void AddDeviceDlg::setPorts(const QStringList &ports)
{
  m_settings->setPorts(ports);
}

void AddDeviceDlg::setSigrokExe(const QString &exe)
{
  m_settings->setSigrokExe(exe);
}

void AddDeviceDlg::setStateManager(SharedStateManager *state)
{
  m_settings->setStateManager(state);
}

void AddDeviceDlg::setCurrentDevice(const QString &name)
{
  m_currentDevice = name;
  m_thisWindow->setText(tr("In &this window") + '\n'
                        + (name.isEmpty() ? tr("instead of the meter now") : tr("instead of %1").arg(name)));
}

bool AddDeviceDlg::offers(Connection connection, const DmmDecoder::DMMInfo &info)
{
  const ProtocolInfo *protocol = protocolInfo(info.protocol);
  const bool bluetooth = info.baud == 0 && protocol && QLatin1String(protocol->transport) == QLatin1String("Bluetooth LE");
  const bool sigrok = info.protocol == FrameFormat::Sigrok && !info.sigrokDriver.isEmpty();
  const bool simulated = info.vendor == QLatin1String("QtDMM");
  switch (connection)
  {
    case Bluetooth: return bluetooth;
    case Sigrok:    return sigrok;
    case Simulated: return simulated;
    case Cable:
    case Network:   return !bluetooth && !sigrok && !simulated;   // the bridge passes a cable on
  }
  return false;
}

void AddDeviceDlg::chooseConnection(Connection connection)
{
  m_connection = connection;
  const bool manual = connection == Cable || connection == Network;
  m_settings->setModelFilter([connection](const DmmDecoder::DMMInfo &info) { return offers(connection, info); }, manual);
  QVariantMap keys;
  if (connection == Simulated)
    keys.insert("DMM/model", "QtDMM Virtual meter");
  else if (!manual && !m_settings->models().isEmpty())
    keys.insert("DMM/model", m_settings->models().first());
  m_settings->setPortEditable(connection == Network, tr("RFC2217 host:port"));
  m_settings->load(keys);
  m_name->clear();
  m_name->setModified(false);
  m_model.clear();
  suggestName();
  showPage(DevicePage);
}

AddDeviceDlg::Page AddDeviceDlg::page() const
{
  return static_cast<Page>(m_pages->currentIndex());
}

bool AddDeviceDlg::canGoNext() const
{
  return page() == DevicePage && m_settings->isComplete() && !m_name->text().trimmed().isEmpty();
}

QVariantMap AddDeviceDlg::keys() const
{
  QVariantMap keys = m_settings->keys();
  // a network address typed without the port type
  const QString device = keys.value("Port settings/device").toString().trimmed();
  if (m_connection == Network && !device.isEmpty() && !device.contains(' '))
    keys.insert("Port settings/device", "RFC2217 " + device);
  return keys;
}

QString AddDeviceDlg::name() const
{
  return m_name->text().trimmed();
}

void AddDeviceDlg::back()
{
  if (page() == TargetPage)
    showPage(DevicePage);
  else if (page() == DevicePage)
    showPage(ConnectionPage);
}

void AddDeviceDlg::next()
{
  if (canGoNext())
    showPage(TargetPage);
}

void AddDeviceDlg::showPage(Page page)
{
  m_pages->setCurrentIndex(page);
  switch (page)
  {
    case ConnectionPage: m_title->setText(tr("How is the meter connected?")); break;
    case DevicePage:     m_title->setText(tr("Which meter is it?")); break;
    case TargetPage:     m_title->setText(tr("Where should it go?")); break;
  }
  if (page == DevicePage)
    m_name->setFocus();
  updateButtons();
}

void AddDeviceDlg::updateButtons()
{
  m_back->setEnabled(page() != ConnectionPage);
  m_next->setVisible(page() == DevicePage);
  m_next->setEnabled(canGoNext());
}

void AddDeviceDlg::suggestName()
{
  if (m_name->isModified())
    return;
  const QString model = m_settings->keys().value("DMM/model").toString();
  if (model == m_model)
    return;
  m_model = model;
  const QString base = model == QLatin1String("Manual") ? tr("Meter") : QString(model).remove(" *");
  m_name->setText(m_library ? m_library->uniqueName(base) : base);
}

void AddDeviceDlg::finish(Target target)
{
  m_target = target;
  accept();
}
