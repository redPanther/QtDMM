// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/views/graphflags.h"

#include <QPainter>
#include <QPainterPath>

namespace GraphFlags
{
void layout(QList<Flag> &flags, const QRectF &plot, double height, const std::function<double(const QString &)> &widthOf)
{
  double prevRight = -qInf();
  for (Flag &f : flags)
  {
    f.rect = QRectF();
    const double width = widthOf(f.label) + 2 * kPad;
    double left = qMax(plot.left(), qMin(f.x - width / 2, plot.right() - width));
    left = qMax(left, prevRight + kAir);
    // the point has to leave from the tag
    if (f.x < left || f.x > left + width || left + width > plot.right())
      continue;
    f.rect = QRectF(left, plot.bottom() + kPoint, width, height);
    prevRight = f.rect.right();
  }
}
}

GraphFlagItem::GraphFlagItem(QGraphicsItem *parent) :
  QGraphicsItem(parent)
{
}

void GraphFlagItem::setPlace(const QRectF &rect, double x)
{
  prepareGeometryChange();
  m_rect = rect;
  m_x = x;
}

void GraphFlagItem::setText(const QString &text)
{
  m_text = text;
  update();
}

void GraphFlagItem::setColor(const QColor &color)
{
  m_color = color;
  update();
}

void GraphFlagItem::setFont(const QFont &font)
{
  m_font = font;
  update();
}

QRectF GraphFlagItem::boundingRect() const
{
  return m_rect.adjusted(0, -GraphFlags::kPoint, 0, 0);
}

void GraphFlagItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
  if (m_rect.isEmpty())
    return;
  p->setRenderHint(QPainter::Antialiasing);
  QPainterPath tag;
  tag.addRoundedRect(m_rect, 3, 3);
  const double k = GraphFlags::kPoint;
  QPolygonF point;
  point << QPointF(m_x - k, m_rect.top() + 1) << QPointF(m_x, m_rect.top() - k) << QPointF(m_x + k, m_rect.top() + 1);
  tag.addPolygon(point);
  p->setPen(Qt::NoPen);
  p->setBrush(m_color);
  p->drawPath(tag.simplified());
  // black or white, whichever stands out from the fill
  p->setPen(qGray(m_color.rgb()) > 140 ? Qt::black : Qt::white);
  p->setFont(m_font);
  p->drawText(m_rect, Qt::AlignCenter, m_text);
}
