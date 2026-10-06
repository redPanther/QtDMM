// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/adddevicedlg.h"

#include <QtWidgets>

#include "core/devicelibrary.h"
#include "device/discovery/discovery.h"
#include "device/protocols.h"
#include "ui/devicesettings.h"
#include "ui/tilebutton.h"

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

  // page 2: the search for the chosen connection, every port found, and
  // the port field
  auto *port = new QWidget(m_pages);
  auto *portLayout = new QVBoxLayout(port);
  portLayout->setContentsMargins(0, 0, 0, 0);
  auto *searchRow = new QHBoxLayout;
  m_progress = new QProgressBar(port);
  m_progress->setRange(0, 0);
  m_progress->setTextVisible(false);
  m_searchAgain = new QPushButton(tr("Search a&gain"), port);
  searchRow->addWidget(m_progress, 1);
  searchRow->addStretch(0);
  searchRow->addWidget(m_searchAgain, 0, Qt::AlignRight);
  portLayout->addLayout(searchRow);
  connect(m_searchAgain, &QPushButton::clicked, this, &AddDeviceDlg::startSearch);
  m_cards = new QListWidget(port);
  m_cards->setObjectName("ui_cards");
  m_cards->setIconSize(QSize(32, 32));
  m_cards->setSpacing(2);
  portLayout->addWidget(m_cards, 1);
  connect(m_cards, &QListWidget::itemClicked, this, [this](QListWidgetItem *item)
  {
    chooseCandidate(item->data(Qt::UserRole).toString());
  });
  connect(m_cards, &QListWidget::itemDoubleClicked, this, [this] { next(); });
  m_hint = new QLabel(port);
  m_hint->setWordWrap(true);
  m_fix = new QPushButton(tr("How to &fix..."), port);
  m_fix->hide();
  auto *hintRow = new QHBoxLayout;
  hintRow->addWidget(m_hint, 1);
  hintRow->addWidget(m_fix);
  portLayout->addLayout(hintRow);
  connect(m_fix, &QPushButton::clicked, this, [this]
  {
    const int row = chosenCandidate();
    if (row < 0)
      return;
    QMessageBox box(QMessageBox::Information, tr("How to fix"), m_found[row].problem, QMessageBox::Close, this);
    box.setInformativeText(m_found[row].fix);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    box.exec();
  });
  auto *portRow = new QFormLayout;
  m_port = new QLineEdit(port);
  m_port->setObjectName("ui_port");
  portRow->addRow(tr("Po&rt:"), m_port);
  portLayout->addLayout(portRow);
  connect(m_port, &QLineEdit::textChanged, this, [this]
  {
    // the card of the port typed or chosen, none for another one
    const int row = chosenCandidate();
    m_cards->setCurrentRow(row);
    updateHint();
    updateButtons();
  });
  m_pages->addWidget(port);

  m_makeDiscoverers = [](Connection connection) -> QList<Discoverer *>
  {
    if (connection == Cable)
      return { new UsbDiscoverer, new SerialDiscoverer };
    if (connection == Bluetooth)
      return { new BleDiscoverer };
    if (connection == Network)
      return { new BridgeDiscoverer };
    return {};
  };

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
  // the port comes from page 2
  m_settings->setPortVisible(false);
  deviceLayout->addWidget(m_settings, 1);
  connect(m_settings, &DeviceSettings::changed, this, &AddDeviceDlg::suggestName);
  connect(m_settings, &DeviceSettings::changed, this, &AddDeviceDlg::updateButtons);
  connect(m_name, &QLineEdit::textChanged, this, &AddDeviceDlg::updateButtons);
  m_pages->addWidget(device);

  // page 4: where it goes; a click finishes
  auto *target = new QWidget(m_pages);
  auto *targets = new QHBoxLayout(target);
  m_thisWindow = tile("go-next", QString(), QString());
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
  auto *button = new TileButton(this);
  button->setIcon(QIcon::fromTheme(icon));
  button->setIconSize(QSize(48, 48));
  button->setText(text);
  button->setToolTip(toolTip);
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
  // a long name or a formula made the tile wide: the name in the tooltip only
  m_thisWindow->setText(tr("In &this window") + '\n' + tr("as the current device"));
  m_thisWindow->setToolTip(name.isEmpty() ? tr("The device takes the place of the one in this window")
                                          : tr("The device takes the place of %1 in this window").arg(name));
}

void AddDeviceDlg::setTargetChoice(bool choice)
{
  m_targetChoice = choice;
  updateButtons();
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

bool AddDeviceDlg::hasPortPage(Connection connection)
{
  return connection == Cable || connection == Bluetooth || connection == Network;
}

void AddDeviceDlg::chooseConnection(Connection connection)
{
  m_connection = connection;
  const bool manual = connection == Cable || connection == Network;
  m_settings->setModelFilter([connection](const DmmDecoder::DMMInfo &info) { return offers(connection, info); }, manual);
  QVariantMap keys;
  if (connection == Simulated)
    keys.insert("DMM/model", "QtDMM Simulated meter");
  else if (!manual && !m_settings->models().isEmpty())
    keys.insert("DMM/model", m_settings->models().first());
  m_settings->load(keys);
  m_name->clear();
  m_name->setModified(false);
  m_model.clear();
  m_known.clear();
  suggestName();
  if (hasPortPage(connection))
  {
    m_port->clear();
    m_port->setPlaceholderText(connection == Bluetooth ? tr("Bluetooth address, e.g. AA:BB:CC:DD:EE:FF")
                               : connection == Network ? tr("host:port, e.g. 192.168.1.20:4711")
                                                       : tr("e.g. /dev/ttyUSB0 or COM3"));
    showPage(PortPage);
    startSearch();
  }
  else
    showPage(DevicePage);
}

AddDeviceDlg::~AddDeviceDlg()
{
  stopSearch();
}

void AddDeviceDlg::startSearch()
{
  stopSearch();
  m_found.clear();
  m_cards->clear();
  for (Discoverer *d : m_makeDiscoverers ? m_makeDiscoverers(m_connection) : QList<Discoverer *>())
  {
    d->setParent(this);
    connect(d, &Discoverer::found, this, &AddDeviceDlg::addCandidate);
    connect(d, &Discoverer::finished, this, [this, d]
    {
      m_running.removeAll(d);
      d->deleteLater();
      updateHint();
    });
    m_running << d;
  }
  for (Discoverer *d : QList<Discoverer *>(m_running))
    d->start();
  updateHint();
}

void AddDeviceDlg::stopSearch()
{
  for (Discoverer *d : QList<Discoverer *>(m_running))
    d->stop();
}

// what the port field shows for a find: the port without "SERIAL", the
// Bluetooth address, host:port without "RFC2217"
static QString portText(const Candidate &c)
{
  if (c.kind == Candidate::Bluetooth)
    return c.keys.value("Port settings/ble-address").toString().section(' ', 0, 0);
  const QString device = c.keys.value("Port settings/device").toString();
  return device.startsWith(QLatin1String("SERIAL ")) || device.startsWith(QLatin1String("RFC2217 "))
           ? device.section(' ', 1) : device;
}

void AddDeviceDlg::addCandidate(const Candidate &c)
{
  for (const Candidate &f : m_found)
    if (f.key == c.key && f.kind == c.kind)
      return;
  // recognised meters first, then the ports nothing is known of, each in
  // the order they came
  const bool recognised = !c.models.isEmpty();
  int at = int(m_found.size());
  if (recognised)
    for (int i = 0; i < m_found.size(); ++i)
      if (m_found[i].models.isEmpty())
      {
        at = i;
        break;
      }
  m_found.insert(at, c);

  // symbols of the sets QtDMM brings (assets/icons/sets)
  static const char *icons[] = { "qtdmm-dmm", "network-wired", "qtdmm-dmm", "network-server" };
  QString text = c.title + '\n' + c.detail;
  const QString known = m_library ? m_library->findByPlace(c.keys) : QString();
  if (const std::optional<MyDevice> d = m_library ? m_library->find(known) : std::nullopt)
    text += '\n' + tr("Already in My devices: %1").arg(d->name);
  const QString inUse = m_inUse.value(DeviceLibrary::place(c.keys));
  if (!inUse.isEmpty())
    text += '\n' + inUse;
  if (!c.problem.isEmpty())
    text += '\n' + c.problem;
  auto *item = new QListWidgetItem(QIcon::fromTheme(icons[c.kind]), text);
  item->setData(Qt::UserRole, c.key);
  if (!c.problem.isEmpty())
    item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
  m_cards->insertItem(at, item);
  m_cards->setCurrentRow(chosenCandidate());
  updateHint();
}

QStringList AddDeviceDlg::candidates() const
{
  QStringList keys;
  for (const Candidate &c : m_found)
    keys << c.key;
  return keys;
}

void AddDeviceDlg::chooseCandidate(const QString &key)
{
  for (const Candidate &c : m_found)
    if (c.key == key)
      m_port->setText(portText(c));
}

void AddDeviceDlg::setPort(const QString &port)
{
  m_port->setText(port);
}

int AddDeviceDlg::chosenCandidate() const
{
  const QString port = m_port->text().trimmed();
  if (port.isEmpty())
    return -1;
  for (int i = 0; i < m_found.size(); ++i)
    if (portText(m_found[i]).compare(port, Qt::CaseInsensitive) == 0)
      return i;
  return -1;
}

void AddDeviceDlg::updateHint()
{
  m_progress->setVisible(!m_running.isEmpty());
  m_searchAgain->setEnabled(m_running.isEmpty());
  const int row = chosenCandidate();
  m_fix->setVisible(row >= 0 && !m_found[row].problem.isEmpty());
  if (row >= 0)
  {
    const Candidate &c = m_found[row];
    const QString known = m_library ? m_library->findByPlace(c.keys) : QString();
    const std::optional<MyDevice> d = m_library ? m_library->find(known) : std::nullopt;
    m_hint->setText(!c.problem.isEmpty() ? c.problem
                    : d                  ? tr("This is \"%1\" of My devices: the next page changes it, "
                                              "no second entry.").arg(d->name)
                                         : c.hint);
  }
  else if (!m_running.isEmpty())
    m_hint->setText(tr("Searching ..."));
  else if (m_found.isEmpty())
    m_hint->setText(m_connection == Bluetooth
                    ? tr("Nothing found. Is the meter's Bluetooth on and no other program connected to it? "
                         "Or type its address.")
                    : m_connection == Network
                    ? tr("No bridge found. Is qtdmm-bridge running with --mdns in this network? "
                         "Or type host:port of the bridge or another RFC 2217 server.")
                    : tr("Nothing found. Is the meter switched on and its cable plugged in? Or type the port."));
  else
    m_hint->setText(m_port->text().trimmed().isEmpty() ? tr("Choose where the meter is, or type the port.") : QString());
}

void AddDeviceDlg::takePort()
{
  stopSearch();
  const int row = chosenCandidate();
  const QString port = m_port->text().trimmed();
  QVariantMap found;
  QString model;
  if (row >= 0)
  {
    found = m_found[row].keys;
    if (!m_found[row].models.isEmpty())
      model = m_found[row].models.first();
  }
  else if (m_connection == Bluetooth)
    found.insert("Port settings/ble-address", port);
  else
    found.insert("Port settings/device", port.contains(' ') ? port
                                         : (m_connection == Network ? "RFC2217 " : "SERIAL ") + port);

  m_known = m_library ? m_library->findByPlace(found) : QString();
  const std::optional<MyDevice> entry = m_library ? m_library->find(m_known) : std::nullopt;
  QVariantMap keys = found;
  if (entry)
  {
    // the entry as it is, at the place found now
    keys = entry->keys;
    for (const char *key : { "Port settings/device", "Port settings/ble-address" })
      if (!found.value(key).toString().isEmpty())
        keys.insert(key, found.value(key));
  }
  else if (!model.isEmpty())
    keys.insert("DMM/model", model);
  else if (m_connection == Bluetooth && !m_settings->models().isEmpty())
    keys.insert("DMM/model", m_settings->models().first());
  m_settings->load(keys);
  m_model.clear();
  if (entry)
  {
    m_name->setText(entry->name);
    m_name->setModified(true);   // its name, not one after the model
    m_model = keys.value("DMM/model").toString();
  }
  else
  {
    m_name->clear();
    m_name->setModified(false);
    suggestName();
  }
}

QString AddDeviceDlg::knownDevice() const
{
  const std::optional<MyDevice> entry = m_library ? m_library->find(m_known) : std::nullopt;
  if (!entry)
    return QString();
  // another model than the entry's at this place: another meter, a new entry
  const QString model = m_settings->keys().value("DMM/model").toString();
  return QString(entry->model()).remove(" *") == QString(model).remove(" *") ? entry->id : QString();
}

AddDeviceDlg::Page AddDeviceDlg::page() const
{
  return static_cast<Page>(m_pages->currentIndex());
}

bool AddDeviceDlg::canGoNext() const
{
  if (page() == PortPage)
  {
    const int row = chosenCandidate();
    return !m_port->text().trimmed().isEmpty() && (row < 0 || m_found[row].problem.isEmpty());
  }
  return page() == DevicePage && m_settings->isComplete() && !m_name->text().trimmed().isEmpty();
}

QVariantMap AddDeviceDlg::keys() const
{
  return m_settings->keys();
}

QString AddDeviceDlg::name() const
{
  return m_name->text().trimmed();
}

void AddDeviceDlg::back()
{
  if (page() == TargetPage)
    showPage(DevicePage);
  else if (page() == DevicePage && hasPortPage(m_connection))
    showPage(PortPage);
  else if (page() == DevicePage || page() == PortPage)
  {
    stopSearch();
    showPage(ConnectionPage);
  }
}

void AddDeviceDlg::next()
{
  if (!canGoNext())
    return;
  if (page() == PortPage)
  {
    takePort();
    showPage(DevicePage);
  }
  else if (!m_targetChoice)
    finish(ThisWindow);
  else
    showPage(TargetPage);
}

void AddDeviceDlg::showPage(Page page)
{
  m_pages->setCurrentIndex(page);
  switch (page)
  {
    case ConnectionPage: m_title->setText(tr("How is the meter connected?")); break;
    case PortPage:       m_title->setText(tr("Where is it connected?")); break;
    case DevicePage:     m_title->setText(tr("Which meter is it?")); break;
    case TargetPage:     m_title->setText(tr("Where should it go?")); break;
  }
  if (page == DevicePage)
    m_name->setFocus();
  if (page == PortPage)
    m_port->setFocus();
  updateButtons();
}

void AddDeviceDlg::updateButtons()
{
  m_back->setEnabled(page() != ConnectionPage);
  m_next->setVisible(page() == PortPage || page() == DevicePage);
  // without page 4 the last page finishes
  m_next->setText(page() == DevicePage && !m_targetChoice ? tr("A&dd") : tr("&Next >"));
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
  const QString base = model == QLatin1String("Manual") ? tr("Meter")
                       : DmmDecoder::sameModel(model, QStringLiteral("QtDMM Simulated meter")) ? tr("Simulated meter")
                                                                                              : QString(model).remove(" *");
  m_name->setText(m_library ? m_library->uniqueName(base) : base);
}

void AddDeviceDlg::finish(Target target)
{
  stopSearch();
  m_target = target;
  accept();
}
