// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/devicesidebar.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDropEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QTimer>
#include <QUrl>

#include "core/devicelibrary.h"
#include "device/dmmdecoder.h"
#include "ui/devicesymbol.h"

namespace
{
// a device's picture (DeviceSymbol), kept with its item
constexpr int kSymbol = Qt::UserRole + 2;
// a value row: the value's Reading::id
constexpr int kValueId = Qt::UserRole + 3;
// a device: its model and where it is connected, the start of its tooltip
constexpr int kWhere = Qt::UserRole + 4;
// the dot, then the picture of a device: every row the same width, so the
// names line up
constexpr int kDotSize = 12;
constexpr int kSymbolSize = 20;
constexpr int kGap = 4;
const QSize kIconSize(kDotSize + kGap + kSymbolSize, kSymbolSize);

// the state of the device in use, as the status line's dot: readings
// coming in or not; the others get an empty one. After it the device's
// picture, for the entries of My devices. Selected the same, not tinted
QIcon dot(const QColor &color, qreal dpr, const QPixmap &symbol = QPixmap())
{
  QPixmap pixmap(kIconSize * dpr);
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);
  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  if (color.isValid())
  {
    const double top = (kIconSize.height() - kDotSize) / 2.0;
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QRectF(1, top + 1, kDotSize - 2, kDotSize - 2));
  }
  if (!symbol.isNull())
    p.drawPixmap(QPointF(kDotSize + kGap, 0), symbol);
  p.end();
  QIcon icon;
  icon.addPixmap(pixmap, QIcon::Normal);
  icon.addPixmap(pixmap, QIcon::Selected);
  return icon;
}

// the picture of the model an entry of My devices has; "unknown" for
// manual settings
QPixmap symbolOf(const MyDevice &d, qreal dpr)
{
  const QSize box(kSymbolSize, kSymbolSize);
  for (const DmmDecoder::DMMInfo &info : DmmDecoder::getDeviceConfigurations())
    if (DmmDecoder::sameModel(d.model(), info.name))
      return DeviceSymbol::pixmap(info.vendor, DeviceType::of(info), box, dpr, DeviceSymbol::Design::Flat);
  return DeviceSymbol::unknown(box, dpr, DeviceSymbol::Design::Flat);
}
}

DeviceSidebar::DeviceSidebar(DeviceLibrary *library, QWidget *parent)
  : QTreeWidget(parent)
  , m_library(library)
{
  setObjectName("ui_deviceSidebar");
  setHeaderHidden(true);
  // the name, and the reading of an open device at the right
  setColumnCount(2);
  header()->setStretchLastSection(false);
  header()->setSectionResizeMode(0, QHeaderView::Stretch);
  header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  setRootIsDecorated(true);
  setIconSize(kIconSize);
  setDragDropMode(QAbstractItemView::InternalMove);
  setSelectionMode(QAbstractItemView::SingleSelection);
  // renamed with F2 (keyPressEvent) or the context menu, not by a click
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  // only the node takes entries: they cannot become children of each other
  invisibleRootItem()->setFlags(Qt::ItemIsEnabled);
  // folded at the start: one look away, not in the way of the devices
  m_workspaces = new QTreeWidgetItem(this, { tr("Workspaces") });
  m_workspaces->setFlags(Qt::ItemIsEnabled);
  QFont bold = m_workspaces->font(0);
  bold.setBold(true);
  m_workspaces->setFont(0, bold);
  m_workspaces->setFirstColumnSpanned(true);
  m_workspaces->setToolTip(0, tr("The workspaces used last: a double click loads one."));
  m_workspaces->setHidden(true);
  m_devices = new QTreeWidgetItem(this, { tr("My devices") });
  m_devices->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
  m_devices->setFont(0, bold);
  m_devices->setFirstColumnSpanned(true);
  m_devices->setExpanded(true);
  m_open = new QTreeWidgetItem(this, { tr("Open devices") });
  m_open->setFlags(Qt::ItemIsEnabled);
  m_open->setFont(0, bold);
  m_open->setFirstColumnSpanned(true);
  m_open->setToolTip(0, tr("The devices this window reads: its own and those opened beside it."));
  m_open->setExpanded(true);
  m_services = new QTreeWidgetItem(this, { tr("Services") });
  m_services->setFlags(Qt::ItemIsEnabled);
  m_services->setFont(0, bold);
  m_services->setFirstColumnSpanned(true);
  m_services->setToolTip(0, tr("What this window offers to other programs over the network."));
  m_services->setExpanded(true);

  connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item)
  {
    if (item && item == m_addItem)
    {
      Q_EMIT addDeviceRequested();
      return;
    }
    // another device: after the double-click time, a double click is Info
    // and switches nothing (the click before it would)
    const QString id = idOf(item);
    if (!id.isEmpty() && id != m_current && !m_doubleClicked)
    {
      m_pendingSwitch = id;
      m_switchDelay.start(QApplication::doubleClickInterval());
    }
  });
  m_switchDelay.setSingleShot(true);
  connect(&m_switchDelay, &QTimer::timeout, this, [this]
  {
    if (m_library->find(m_pendingSwitch) && m_pendingSwitch != m_current)
      Q_EMIT switchRequested(m_pendingSwitch);
    m_pendingSwitch.clear();
  });
  connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item)
  {
    const QString id = idOf(item);
    if (!id.isEmpty())
    {
      m_switchDelay.stop();
      m_pendingSwitch.clear();
      // the release of the second click may come as a click again
      m_doubleClicked = true;
      QTimer::singleShot(QApplication::doubleClickInterval(), this, [this] { m_doubleClicked = false; });
      Q_EMIT settingsRequested(id);
      return;
    }
    if (item && item->parent() == m_services)
    {
      Q_EMIT serviceSettingsRequested();
      return;
    }
    if (const QString open = openOf(item); !open.isEmpty())
    {
      Q_EMIT frontPanelRequested(open);
      return;
    }
    if (const QString file = workspaceOf(item); !file.isEmpty())
    {
      Q_EMIT workspaceRequested(file);
      return;
    }
  });
  // renamed in place
  connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item)
  {
    if (m_filling)
      return;
    const QString id = idOf(item);
    if (id.isEmpty())
      return;
    const std::optional<MyDevice> d = m_library->find(id);
    const QString name = item->text(0).trimmed();
    if (d && !name.isEmpty() && name != d->name)
      m_library->rename(id, name);
    else if (d)
    {
      QSignalBlocker block(this);
      item->setText(0, d->name);
    }
  });
  // later: a change in the middle of a drop must not take the items away
  connect(m_library, &DeviceLibrary::changed, this, &DeviceSidebar::fill, Qt::QueuedConnection);
  fill();
}

QString DeviceSidebar::idOf(const QTreeWidgetItem *item) const
{
  return item && item != m_addItem && item->parent() == m_devices ? item->data(0, Qt::UserRole).toString() : QString();
}

QString DeviceSidebar::openOf(const QTreeWidgetItem *item) const
{
  return item && item->parent() == m_open ? item->data(0, Qt::UserRole).toString() : QString();
}

QTreeWidgetItem *DeviceSidebar::openItem(const QString &id) const
{
  for (int i = 0; i < m_open->childCount(); ++i)
    if (m_open->child(i)->data(0, Qt::UserRole).toString() == id)
      return m_open->child(i);
  return nullptr;
}

void DeviceSidebar::setOpenDevices(const QList<OpenDevice> &devices, const QString &own)
{
  m_openList = devices;
  m_filling = true;
  m_own = own;
  QStringList ids, shown;
  for (const OpenDevice &device : devices)
    ids << device.id;
  for (int i = 0; i < m_open->childCount(); ++i)
    shown << m_open->child(i)->data(0, Qt::UserRole).toString();
  if (ids != shown)
  {
    const QString selected = openOf(currentItem());
    qDeleteAll(m_open->takeChildren());
    for (const OpenDevice &device : devices)
    {
      auto *item = new QTreeWidgetItem(m_open, { device.name });
      item->setData(0, Qt::UserRole, device.id);
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
      item->setExpanded(true);
      if (device.id == selected)
        setCurrentItem(item);
    }
  }
  for (int i = 0; i < devices.size(); ++i)
  {
    const OpenDevice &device = devices[i];
    QTreeWidgetItem *item = m_open->child(i);
    item->setText(0, device.name);
    item->setText(1, device.value);
    item->setToolTip(0, device.id == own ? tr("%1 - in this window").arg(device.name)
                                         : tr("%1 - beside this window's device").arg(device.name));
    QFont font = item->font(0);
    font.setBold(device.current);
    item->setFont(0, font);
    const QBrush text = device.connected ? palette().text() : palette().brush(QPalette::Disabled, QPalette::Text);
    item->setForeground(0, text);
    item->setForeground(1, text);
    item->setIcon(0, dot(device.active ? QColor(0x22, 0xaa, 0x22) : QColor(), devicePixelRatioF()));
    fillValues(item, 0, device.values);
  }
  m_filling = false;
}

void DeviceSidebar::fillValues(QTreeWidgetItem *parent, int first, const QList<Value> &values)
{
  const bool filling = m_filling;
  m_filling = true;
  const int rows = values.size() > 1 ? int(values.size()) : 0;
  while (parent->childCount() > first + rows)
    delete parent->takeChild(parent->childCount() - 1);
  for (int i = 0; i < rows; ++i)
  {
    const Value &value = values[i];
    QTreeWidgetItem *item = parent->child(first + i);
    if (!item)
    {
      item = new QTreeWidgetItem(parent);
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    }
    item->setData(0, kValueId, value.id);
    item->setText(0, QString("%1  %2").arg(value.id + 1).arg(value.shortName.isEmpty() ? value.name : value.shortName));
    item->setText(1, value.text);
    item->setToolTip(0, value.color.isValid() ? tr("%1 - in the graph").arg(value.name) : value.name);
    item->setIcon(0, dot(value.color, devicePixelRatioF()));
  }
  m_filling = filling;
}

ValueRef DeviceSidebar::valueOf(const QTreeWidgetItem *item) const
{
  const QTreeWidgetItem *row = item && item->data(0, kValueId).isValid() ? item->parent() : item;
  if (!row || row->parent() != m_open)
    return ValueRef(0, "-");
  const QString device = row->data(0, Qt::UserRole).toString();
  const QString source = device == m_own ? QString() : "device:" + device;
  if (item != row)
    return ValueRef(item->data(0, kValueId).toInt(), source);
  // a device row stands for its value only while it has no value rows
  return row->childCount() == 0 ? ValueRef(0, source) : ValueRef(0, "-");
}

QTreeWidgetItem *DeviceSidebar::valueItem(int id, const QString &device) const
{
  const QTreeWidgetItem *own = openItem(device.isEmpty() ? m_own : device);
  for (int i = 0; own && i < own->childCount(); ++i)
    if (own->child(i)->data(0, kValueId).isValid() && own->child(i)->data(0, kValueId).toInt() == id)
      return own->child(i);
  return nullptr;
}

void DeviceSidebar::setServices(const QList<Service> &services)
{
  m_serviceList = services;
  m_filling = true;
  while (m_services->childCount() > services.size())
    delete m_services->takeChild(m_services->childCount() - 1);
  const QColor grey = palette().color(QPalette::Disabled, QPalette::WindowText);
  for (int i = 0; i < services.size(); ++i)
  {
    const Service &service = services[i];
    QTreeWidgetItem *item = m_services->child(i);
    if (!item)
    {
      item = new QTreeWidgetItem(m_services);
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    }
    item->setData(0, Qt::UserRole, service.id);
    item->setData(1, Qt::UserRole, service.address);
    item->setText(0, service.name);
    item->setText(1, service.value);
    item->setToolTip(0, service.tooltip);
    item->setToolTip(1, service.tooltip);
    item->setForeground(0, service.running ? palette().color(QPalette::Text) : grey);
    item->setForeground(1, service.running ? palette().color(QPalette::Text) : grey);
    // green while a client is connected, dark green while it runs and waits
    // for one; red: switched on but not running (port taken)
    item->setIcon(0, dot(service.active    ? QColor(0x22, 0xaa, 0x22)
                         : service.failed  ? QColor(0xcc, 0x22, 0x22)
                         : service.running ? QColor(0x1a, 0x5c, 0x1a)
                                           : QColor(), devicePixelRatioF()));
  }
  m_filling = false;
}

QString DeviceSidebar::workspaceOf(const QTreeWidgetItem *item) const
{
  if (!item || item->parent() != m_workspaces)
    return QString();
  const QString file = item->data(0, Qt::UserRole).toString();
  return QFileInfo::exists(file) ? file : QString();
}

void DeviceSidebar::setWorkspaces(const QStringList &files)
{
  m_workspaceList = files;
  m_filling = true;
  qDeleteAll(m_workspaces->takeChildren());
  const QColor grey = palette().color(QPalette::Disabled, QPalette::WindowText);
  for (const QString &file : files)
  {
    const QFileInfo info(file);
    auto *item = new QTreeWidgetItem(m_workspaces, { info.completeBaseName() });
    item->setData(0, Qt::UserRole, file);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setFirstColumnSpanned(true);
    const QString path = QDir::toNativeSeparators(file);
    if (info.exists())
      item->setToolTip(0, path);
    else
    {
      item->setForeground(0, grey);
      item->setToolTip(0, tr("%1 (not found)").arg(path));
    }
  }
  m_workspaces->setHidden(files.isEmpty());
  m_filling = false;
}

void DeviceSidebar::workspaceMenu(QTreeWidgetItem *item, const QPoint &pos)
{
  const QString file = item->data(0, Qt::UserRole).toString();
  const QString dir = QFileInfo(file).absolutePath();
  QMenu menu(this);
  QAction *load = menu.addAction(QIcon::fromTheme("document-open"), tr("&Load"));
  load->setEnabled(!workspaceOf(item).isEmpty());
  QAction *folder = menu.addAction(QIcon::fromTheme("folder-open"), tr("Open &folder"));
  folder->setEnabled(QFileInfo(dir).isDir());
  menu.addSeparator();
  QAction *remove = menu.addAction(QIcon::fromTheme("list-remove"), tr("&Remove from list"));
  QAction *chosen = menu.exec(pos);
  if (chosen == load)
    Q_EMIT workspaceRequested(file);
  else if (chosen == folder)
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
  else if (chosen == remove)
    Q_EMIT removeWorkspaceRequested(file);
}

void DeviceSidebar::serviceMenu(QTreeWidgetItem *item, const QPoint &pos)
{
  const QString address = item->data(1, Qt::UserRole).toString();
  QMenu menu(this);
  QAction *settings = menu.addAction(QIcon::fromTheme("configure"), tr("&Settings..."));
  QAction *copy = menu.addAction(QIcon::fromTheme("edit-copy"), tr("&Copy address"));
  copy->setEnabled(!address.isEmpty());
  QAction *chosen = menu.exec(pos);
  if (chosen == settings)
    Q_EMIT serviceSettingsRequested();
  else if (chosen == copy)
    QApplication::clipboard()->setText(address);
}

void DeviceSidebar::fill()
{
  // a name being typed (Rename) would go: later, once it is done
  if (state() == QAbstractItemView::EditingState)
  {
    QTimer::singleShot(250, this, &DeviceSidebar::fill);
    return;
  }
  m_filling = true;
  const QString selected = idOf(currentItem());
  qDeleteAll(m_devices->takeChildren());
  m_addItem = nullptr;
  for (const MyDevice &d : m_library->list())
  {
    auto *item = new QTreeWidgetItem(m_devices, { d.name });
    item->setData(0, Qt::UserRole, d.id);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsEditable);
    item->setFirstColumnSpanned(true);
    item->setData(0, kWhere, d.where().isEmpty() ? d.model() : QString("%1 · %2").arg(d.model(), d.where()));
    item->setData(0, kSymbol, symbolOf(d, devicePixelRatioF()));
    if (d.id == selected)
      setCurrentItem(item);
  }
  // a device more: the assistant, from where the devices are
  m_addItem = new QTreeWidgetItem(m_devices, { tr("+ Add device...") });
  m_addItem->setFlags(Qt::ItemIsEnabled);
  m_addItem->setFirstColumnSpanned(true);
  m_addItem->setForeground(0, palette().color(QPalette::Disabled, QPalette::WindowText));
  m_addItem->setToolTip(0, tr("Set up another meter (the assistant)."));
  m_addItem->setIcon(0, dot(QColor(), devicePixelRatioF()));   // its text where the names are
  m_devices->setExpanded(true);
  updateMarks();
  m_filling = false;
}

void DeviceSidebar::setCurrent(const QString &id, bool active, bool blink)
{
  if (id == m_current && active == m_active && blink == m_blink)
    return;
  m_current = id;
  m_active = active;
  m_blink = blink;
  m_filling = true;
  updateMarks();
  m_filling = false;
}

void DeviceSidebar::updateMarks()
{
  for (int i = 0; i < m_devices->childCount(); ++i)
  {
    QTreeWidgetItem *item = m_devices->child(i);
    if (item == m_addItem)
      continue;
    const bool current = idOf(item) == m_current;
    // model and port, whether it is in use here, how to read more
    QString tip = item->data(0, kWhere).toString();
    if (current)
      tip += '\n' + (m_active ? tr("In use in this window, readings are coming in") : tr("In use in this window"));
    item->setToolTip(0, tip + '\n' + tr("Double-click: info and settings"));
    QFont font = item->font(0);
    font.setBold(current);
    item->setFont(0, font);
    item->setIcon(0, dot(!current  ? QColor()
                         : m_active ? (m_blink ? QColor(0x66, 0xcc, 0x66) : QColor(0x22, 0xaa, 0x22))
                                    : palette().color(QPalette::Disabled, QPalette::WindowText),
                         devicePixelRatioF(), item->data(0, kSymbol).value<QPixmap>()));
  }
}

QTreeWidgetItem *DeviceSidebar::deviceItem(const QString &id) const
{
  for (int i = 0; i < m_devices->childCount(); ++i)
    if (idOf(m_devices->child(i)) == id)
      return m_devices->child(i);
  return nullptr;
}

QStringList DeviceSidebar::deviceIds() const
{
  QStringList ids;
  for (int i = 0; i < m_devices->childCount(); ++i)
    if (m_devices->child(i) != m_addItem)
      ids << idOf(m_devices->child(i));
  return ids;
}

void DeviceSidebar::dropEvent(QDropEvent *event)
{
  const QString id = idOf(currentItem());
  QTreeWidget::dropEvent(event);
  // the entry's new place in the node is its place in the list
  const int index = int(deviceIds().indexOf(id));
  if (!id.isEmpty() && index >= 0)
    m_library->move(id, index);
}

void DeviceSidebar::changeEvent(QEvent *event)
{
  QTreeWidget::changeEvent(event);
  // the greys are set on the entries: those of the new design
  if (event->type() != QEvent::PaletteChange || m_filling || !m_addItem)
    return;
  m_addItem->setForeground(0, palette().color(QPalette::Disabled, QPalette::WindowText));
  updateMarks();
  setOpenDevices(m_openList, m_own);
  setServices(m_serviceList);
  setWorkspaces(m_workspaceList);
}

void DeviceSidebar::keyPressEvent(QKeyEvent *event)
{
  // F2 renames the device
  QTreeWidgetItem *item = currentItem();
  if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !workspaceOf(item).isEmpty())
  {
    Q_EMIT workspaceRequested(workspaceOf(item));
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_F2 && event->modifiers() == Qt::NoModifier && item
      && (item->flags() & Qt::ItemIsEditable))
  {
    editItem(item, 0);
    event->accept();
    return;
  }
  QTreeWidget::keyPressEvent(event);
}

void DeviceSidebar::openMenu(QTreeWidgetItem *item, const QPoint &pos)
{
  const QString id = openOf(item);
  bool connected = true;
  for (const OpenDevice &device : std::as_const(m_openList))
    if (device.id == id)
      connected = device.connected;
  QMenu menu(this);
  QAction *panel = menu.addAction(QIcon::fromTheme("qtdmm-display"), tr("Show &front panel"));
  // a device with one value: where it is shown, here
  if (const ValueRef value = valueOf(item); value.source != "-" && m_valueFiller)
    m_valueFiller(menu.addMenu(tr("&Value")), value);
  QAction *link = menu.addAction(connected ? tr("&Disconnect") : tr("Co&nnect"));
  QAction *close = nullptr;
  if (id != m_own)
  {
    menu.addSeparator();
    close = menu.addAction(QIcon::fromTheme("window-close"), tr("&Close"));
  }
  QAction *chosen = menu.exec(pos);
  if (chosen == panel)
    Q_EMIT frontPanelRequested(id);
  else if (chosen == link)
    Q_EMIT connectRequested(id, !connected);
  else if (chosen && chosen == close)
    Q_EMIT closeRequested(id);
}

void DeviceSidebar::contextMenuEvent(QContextMenuEvent *event)
{
  QTreeWidgetItem *item = itemAt(event->pos());
  if (item && item->parent() == m_workspaces)
  {
    workspaceMenu(item, event->globalPos());
    return;
  }
  if (item && (item == m_services || item->parent() == m_services))
  {
    if (item->parent() == m_services)
      serviceMenu(item, event->globalPos());
    return;
  }
  if (item && item->data(0, kValueId).isValid())
  {
    Q_EMIT valueMenuRequested(valueOf(item), event->globalPos());
    return;
  }
  if (item && !openOf(item).isEmpty())
  {
    openMenu(item, event->globalPos());
    return;
  }
  const QString id = idOf(item);
  const std::optional<MyDevice> d = m_library->find(id);
  if (!d)
    return;
  QMenu menu(this);
  QAction *settings = menu.addAction(QIcon::fromTheme("configure"), tr("&Info and settings..."));
  QAction *rename = menu.addAction(tr("&Rename"));
  QAction *beside = menu.addAction(QIcon::fromTheme("window-new"), tr("Open &beside"));
  beside->setToolTip(tr("Reads it beside this window's device: recorded with it, in formulas and over SCPI"));
  beside->setEnabled(id != m_current);
  menu.addSeparator();
  QAction *remove = menu.addAction(QIcon::fromTheme("list-remove"), tr("Re&move from My devices"));
  menu.setToolTipsVisible(true);
  QAction *chosen = menu.exec(event->globalPos());
  if (chosen == settings)
    Q_EMIT settingsRequested(id);
  else if (chosen == rename)
    editItem(item);
  else if (chosen == beside)
    Q_EMIT openBesideRequested(id);
  else if (chosen == remove
           && QMessageBox::question(this, tr("Remove device"), tr("Remove \"%1\" from My devices?").arg(d->name))
                == QMessageBox::Yes)
    m_library->remove(id);
}

bool DeviceSidebar::shownAtStart(const QVariant &stored, int devices, bool configured)
{
  if (!configured)
    return devices > 0;
  return stored.isValid() ? stored.toBool() : devices >= 2;
}

bool DeviceSidebar::opensAfterAdd(int devicesBefore, int devicesAfter)
{
  return devicesBefore < 2 && devicesAfter >= 2;
}
