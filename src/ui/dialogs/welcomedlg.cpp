// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/welcomedlg.h"

#include <QCommandLinkButton>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/devicelibrary.h"

WelcomeDlg::WelcomeDlg(DeviceLibrary *library, QWidget *parent) :
  QDialog(parent)
{
  setWindowTitle(tr("Welcome to QtDMM"));
  auto *layout = new QVBoxLayout(this);
  auto *title = new QLabel(tr("<h2>Connect a meter</h2>"), this);
  layout->addWidget(title);

  // the meters already known come first: one click
  const QList<MyDevice> devices = library ? library->list() : QList<MyDevice>();
  if (!devices.isEmpty())
  {
    layout->addWidget(new QLabel(tr("My devices:"), this));
    auto *list = new QListWidget(this);
    for (const MyDevice &d : devices)
    {
      auto *item = new QListWidgetItem(QIcon::fromTheme("qtdmm-dmm"), QString("%1 - %2").arg(d.name, d.model()), list);
      item->setData(Qt::UserRole, d.id);
    }
    list->setMaximumHeight(list->sizeHintForRow(0) * qMin(int(devices.size()), 5) + 8);
    connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item)
    {
      choose(Known, item->data(Qt::UserRole).toString());
    });
    layout->addWidget(list);
  }

  auto *find = new QCommandLinkButton(tr("&Find device"), tr("Plug the meter in and switch it on: QtDMM looks for it "
                                                             "on USB, Bluetooth, serial ports and in the network."), this);
  find->setIcon(QIcon::fromTheme("qtdmm-dmm"));
  auto *virtualMeter = new QCommandLinkButton(tr("&Try without a device"),
                                              tr("The virtual meter: a signal of its own, to see the display, the "
                                                 "analog meter and the graph at work."), this);
  virtualMeter->setIcon(QIcon::fromTheme("media-playback-start"));
  layout->addWidget(find);
  layout->addWidget(virtualMeter);
  auto *byHand = new QPushButton(tr("Set up by &hand..."), this);
  byHand->setFlat(true);
  auto *row = new QHBoxLayout;
  row->addWidget(byHand);
  row->addStretch(1);
  auto *later = new QPushButton(tr("&Later"), this);
  row->addWidget(later);
  layout->addLayout(row);

  connect(find, &QPushButton::clicked, this, [this] { choose(Find); });
  connect(virtualMeter, &QPushButton::clicked, this, [this] { choose(TryVirtual); });
  connect(byHand, &QPushButton::clicked, this, [this] { choose(ByHand); });
  connect(later, &QPushButton::clicked, this, &QDialog::reject);
  find->setDefault(true);
  resize(520, sizeHint().height());
}

void WelcomeDlg::choose(Choice choice, const QString &device)
{
  m_choice = choice;
  m_device = device;
  accept();
}
