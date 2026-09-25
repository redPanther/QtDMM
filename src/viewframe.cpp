// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewframe.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
constexpr int kIdleMs = 3000;   // no reading for this long: the dot turns grey
}

ViewFrame::ViewFrame(QWidget *view, bool showDot, QWidget *parent)
  : QWidget(parent)
  , m_view(view)
{
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(3, 2, 3, 3);
  lay->setSpacing(2);

  m_header = new QWidget(this);
  m_header->setObjectName("viewHeader");   // designs style it by name
  m_header->setAttribute(Qt::WA_StyledBackground);
  m_header->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_header, &QWidget::customContextMenuRequested, this, [this](const QPoint &p)
  {
    Q_EMIT menuRequested(m_header->mapToGlobal(p));
  });
  auto *head = new QHBoxLayout(m_header);
  head->setContentsMargins(4, 0, 4, 0);
  head->setSpacing(6);
  m_title = new QLabel(m_header);
  QFont f = m_title->font();
  f.setBold(true);
  m_title->setFont(f);
  m_detail = new QLabel(m_header);
  head->addWidget(m_title);
  head->addWidget(m_detail);
  if (showDot)
  {
    m_dot = new QLabel(m_header);
    head->addWidget(m_dot);
    m_idle = new QTimer(this);
    m_idle->setSingleShot(true);
    m_idle->setInterval(kIdleMs);
    connect(m_idle, &QTimer::timeout, this, [this]
    {
      m_active = false;
      updateDot();
    });
    updateDot();
  }
  head->addStretch();
  lay->addWidget(m_header);
  lay->addWidget(m_view, 1);
}

void ViewFrame::setTitle(const QString &title)
{
  m_title->setText(title);
}

void ViewFrame::setDetail(const QString &detail)
{
  m_detail->setText(detail.isEmpty() ? QString() : QString::fromUtf8("· ") + detail);
}

void ViewFrame::setHeaderVisible(bool on)
{
  m_header->setVisible(on);
}

bool ViewFrame::headerVisible() const
{
  return !m_header->isHidden();
}

void ViewFrame::pulse()
{
  if (!m_dot)
    return;
  m_active = true;
  m_blink = !m_blink;
  m_idle->start();
  updateDot();
}

void ViewFrame::updateDot()
{
  const QColor c = !m_active ? palette().color(QPalette::Disabled, QPalette::WindowText)
                             : m_blink ? QColor(0x22, 0xaa, 0x22) : QColor(0x66, 0xcc, 0x66);
  m_dot->setText(QString("<span style='color:%1'>&#9679;</span>").arg(c.name()));
}

void ViewFrame::paintEvent(QPaintEvent *)
{
  // the window background; a design may paint more (see Designs)
  const QVariant brush = property("frameBrush");
  if (!brush.isValid())
    return;
  QPainter p(this);
  p.fillRect(rect(), brush.value<QBrush>());
}
