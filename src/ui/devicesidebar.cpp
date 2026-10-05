// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/devicesidebar.h"

#include <QContextMenuEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>

#include "core/devicelibrary.h"

namespace
{
// the state of the device in use, as the status line's dot: readings
// coming in or not; the others get an empty one, so the names line up
QIcon dot(const QColor &color)
{
  QPixmap pixmap(12, 12);
  pixmap.fill(Qt::transparent);
  if (color.isValid())
  {
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QRectF(1, 1, 10, 10));
  }
  return QIcon(pixmap);
}
}

DeviceSidebar::DeviceSidebar(DeviceLibrary *library, QWidget *parent)
  : QTreeWidget(parent)
  , m_library(library)
{
  setObjectName("ui_deviceSidebar");
  setHeaderHidden(true);
  setRootIsDecorated(true);
  setDragDropMode(QAbstractItemView::InternalMove);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setEditTriggers(QAbstractItemView::EditKeyPressed);
  // only the node takes entries: they cannot become children of each other
  invisibleRootItem()->setFlags(Qt::ItemIsEnabled);
  m_devices = new QTreeWidgetItem(this, { tr("My devices") });
  m_devices->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
  QFont bold = m_devices->font(0);
  bold.setBold(true);
  m_devices->setFont(0, bold);
  m_devices->setExpanded(true);

  connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item)
  {
    const QString id = idOf(item);
    if (!id.isEmpty() && id != m_current)
      Q_EMIT switchRequested(id);
  });
  // renamed in place
  connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item)
  {
    const QString id = idOf(item);
    if (m_filling || id.isEmpty())
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
  return item && item->parent() == m_devices ? item->data(0, Qt::UserRole).toString() : QString();
}

void DeviceSidebar::fill()
{
  m_filling = true;
  const QString selected = idOf(currentItem());
  qDeleteAll(m_devices->takeChildren());
  for (const MyDevice &d : m_library->list())
  {
    auto *item = new QTreeWidgetItem(m_devices, { d.name });
    item->setData(0, Qt::UserRole, d.id);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsEditable);
    item->setToolTip(0, d.where().isEmpty() ? d.model() : QString("%1 · %2").arg(d.model(), d.where()));
    if (d.id == selected)
      setCurrentItem(item);
  }
  m_devices->setExpanded(true);
  updateMarks();
  m_filling = false;
}

void DeviceSidebar::setCurrent(const QString &id, bool active)
{
  if (id == m_current && active == m_active)
    return;
  m_current = id;
  m_active = active;
  m_filling = true;
  updateMarks();
  m_filling = false;
}

void DeviceSidebar::updateMarks()
{
  for (int i = 0; i < m_devices->childCount(); ++i)
  {
    QTreeWidgetItem *item = m_devices->child(i);
    const bool current = idOf(item) == m_current;
    QFont font = item->font(0);
    font.setBold(current);
    item->setFont(0, font);
    item->setIcon(0, dot(!current  ? QColor()
                         : m_active ? QColor(0x22, 0xaa, 0x22)
                                    : palette().color(QPalette::Disabled, QPalette::WindowText)));
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

void DeviceSidebar::contextMenuEvent(QContextMenuEvent *event)
{
  QTreeWidgetItem *item = itemAt(event->pos());
  const QString id = idOf(item);
  const std::optional<MyDevice> d = m_library->find(id);
  if (!d)
    return;
  QMenu menu(this);
  QAction *settings = menu.addAction(QIcon::fromTheme("configure"), tr("&Settings..."));
  QAction *rename = menu.addAction(tr("&Rename"));
  QAction *window = menu.addAction(QIcon::fromTheme("window-new"), tr("Open in a &new window"));
  menu.addSeparator();
  QAction *remove = menu.addAction(QIcon::fromTheme("list-remove"), tr("Re&move from My devices"));
  QAction *chosen = menu.exec(event->globalPos());
  if (chosen == settings)
    Q_EMIT settingsRequested(id);
  else if (chosen == rename)
    editItem(item);
  else if (chosen == window)
    Q_EMIT newWindowRequested(id);
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
