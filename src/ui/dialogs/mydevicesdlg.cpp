// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/mydevicesdlg.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/devicelibrary.h"

MyDevicesDlg::MyDevicesDlg(DeviceLibrary *library, QWidget *parent) :
  QDialog(parent),
  m_library(library)
{
  setWindowTitle(tr("My devices"));
  auto *layout = new QVBoxLayout(this);
  auto *hint = new QLabel(tr("Your meters with their connections. Drag an entry to change the order."), this);
  hint->setWordWrap(true);
  layout->addWidget(hint);

  auto *row = new QHBoxLayout;
  m_list = new QListWidget(this);
  m_list->setDragDropMode(QAbstractItemView::InternalMove);
  m_list->setSelectionMode(QAbstractItemView::SingleSelection);
  row->addWidget(m_list, 1);

  auto *buttons = new QVBoxLayout;
  m_use = new QPushButton(tr("&Use"), this);
  m_edit = new QPushButton(tr("&Edit..."), this);
  m_rename = new QPushButton(tr("&Rename..."), this);
  m_duplicate = new QPushButton(tr("D&uplicate"), this);
  m_delete = new QPushButton(tr("De&lete"), this);
  for (QPushButton *b : { m_use, m_edit, m_rename, m_duplicate, m_delete })
    buttons->addWidget(b);
  buttons->addStretch(1);
  row->addLayout(buttons);
  layout->addLayout(row, 1);

  auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(box);

  connect(m_use, &QPushButton::clicked, this, [this] { Q_EMIT useRequested(currentId()); });
  connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { Q_EMIT useRequested(currentId()); });
  connect(m_edit, &QPushButton::clicked, this, [this]
  {
    const QString id = currentId();
    accept();
    Q_EMIT editRequested(id);
  });
  connect(m_rename, &QPushButton::clicked, this, [this]
  {
    const std::optional<MyDevice> d = m_library->find(currentId());
    if (!d)
      return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename device"), tr("Name:"), QLineEdit::Normal, d->name, &ok);
    if (ok && !name.trimmed().isEmpty())
      m_library->rename(d->id, name);
  });
  connect(m_duplicate, &QPushButton::clicked, this, [this] { m_library->duplicate(currentId()); });
  connect(m_delete, &QPushButton::clicked, this, [this]
  {
    const std::optional<MyDevice> d = m_library->find(currentId());
    if (d && QMessageBox::question(this, tr("Delete device"), tr("Remove \"%1\" from your devices?").arg(d->name))
               == QMessageBox::Yes)
      m_library->remove(d->id);
  });
  connect(m_list, &QListWidget::currentRowChanged, this, &MyDevicesDlg::updateButtons);
  // an entry dropped elsewhere: its new place
  connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this]
  {
    if (m_filling)
      return;
    const QString id = currentId();
    for (int i = 0; i < m_list->count(); ++i)
      if (m_list->item(i)->data(Qt::UserRole).toString() == id)
        m_library->move(id, i);
  });
  connect(m_library, &DeviceLibrary::changed, this, &MyDevicesDlg::fill);
  fill();
  resize(520, 320);
}

void MyDevicesDlg::fill()
{
  m_filling = true;
  const QString keep = currentId();
  m_list->clear();
  for (const MyDevice &d : m_library->list())
  {
    QString detail = d.model();
    if (!d.where().isEmpty())
      detail += QString(" · %1").arg(d.where());
    auto *item = new QListWidgetItem(QString("%1\n    %2").arg(d.name, detail), m_list);
    item->setData(Qt::UserRole, d.id);
    if (d.id == keep)
      m_list->setCurrentItem(item);
  }
  if (!m_list->currentItem() && m_list->count() > 0)
    m_list->setCurrentRow(0);
  m_filling = false;
  updateButtons();
}

QString MyDevicesDlg::currentId() const
{
  return m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString();
}

void MyDevicesDlg::updateButtons()
{
  const bool any = m_list->currentItem() != nullptr;
  for (QPushButton *b : { m_use, m_edit, m_rename, m_duplicate, m_delete })
    b->setEnabled(any);
}
