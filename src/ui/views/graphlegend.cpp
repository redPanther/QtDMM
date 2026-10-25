// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/views/graphlegend.h"

#include <QPainter>

GraphLegend::GraphLegend(QWidget *parent)
  : QWidget(parent)
{
  setAttribute(Qt::WA_TransparentForMouseEvents);
}

void GraphLegend::setEntries(const QList<Entry> &entries, const QColor &ink)
{
  m_entries = entries;
  m_ink = ink;
  resize(sizeHint());
  update();
}

QString GraphLegend::text() const
{
  QStringList names;
  for (const Entry &e : m_entries)
    names << e.name;
  return names.join("  ");
}

QSize GraphLegend::sizeHint() const
{
  const QFontMetrics fm(font());
  int w = 0;
  for (const Entry &e : m_entries)
    w += (w ? kSpace : 0) + kSample + kGap + fm.horizontalAdvance(e.name);
  return QSize(w + 2, fm.height() + 2);
}

void GraphLegend::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QFontMetrics fm(font());
  const double mid = height() / 2.0;
  int x = 1;
  for (const Entry &e : std::as_const(m_entries))
  {
    // the line as drawn, a little stronger, so a thin dotted one shows
    QPen pen = e.pen;
    pen.setWidthF(qBound(1.5, pen.widthF(), 3.0));
    p.setPen(pen);
    p.drawLine(QPointF(x + 1, mid), QPointF(x + kSample - 1, mid));
    x += kSample + kGap;
    p.setPen(m_ink);
    p.drawText(QRect(x, 0, fm.horizontalAdvance(e.name) + 1, height()), Qt::AlignVCenter | Qt::AlignLeft, e.name);
    x += fm.horizontalAdvance(e.name) + kSpace;
  }
}
