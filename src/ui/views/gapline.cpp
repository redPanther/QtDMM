// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/views/gapline.h"

#include <QAbstractAxis>
#include <QChart>
#include <QLineSeries>

#include <cmath>

GapLine::GapLine(QLineSeries *first)
{
  m_segments << first;
  m_pen = first->pen();
  m_visible = first->isVisible();
}

QLineSeries *GapLine::segment(int i)
{
  while (m_segments.size() <= i)
  {
    QLineSeries *first = m_segments.first();
    auto *s = new QLineSeries();
    first->chart()->addSeries(s);
    for (QAbstractAxis *axis : first->attachedAxes())
      s->attachAxis(axis);
    s->setPen(m_pen);
    s->setVisible(m_visible);
    m_segments << s;
  }
  return m_segments[i];
}

void GapLine::setPen(const QPen &pen)
{
  m_pen = pen;
  for (QLineSeries *s : std::as_const(m_segments))
    s->setPen(pen);
}

void GapLine::setVisible(bool on)
{
  m_visible = on;
  for (QLineSeries *s : std::as_const(m_segments))
    s->setVisible(on);
}

void GapLine::add(const QPointF &p)
{
  Step step;
  step.pendingBefore = m_pending;
  if (std::isnan(p.y()))
  {
    step.gap = true;
    m_pending = true;
  }
  else
  {
    QLineSeries *open = m_segments[m_used - 1];
    // a gap before the first point of a segment splits nothing
    if (m_pending && open->count() > 0)
    {
      open = segment(m_used++);
      step.newSegment = true;
    }
    m_pending = false;
    open->append(p);
  }
  m_steps << step;
  if (m_steps.size() > kUndo)
    m_steps.removeFirst();
}

void GapLine::append(const QList<QPointF> &points)
{
  for (const QPointF &p : points)
    add(p);
}

void GapLine::removeLast(int count)
{
  for (int i = 0; i < count && !m_steps.isEmpty(); ++i)
  {
    const Step step = m_steps.takeLast();
    if (!step.gap)
    {
      QLineSeries *open = m_segments[m_used - 1];
      open->removePoints(open->count() - 1, 1);
      if (step.newSegment)
        m_used--;   // empty again, back in the pool
    }
    m_pending = step.pendingBefore;
  }
}

void GapLine::replace(const QList<QPointF> &points)
{
  // split first, then one replace() per segment: much faster than append()
  // point by point
  QList<QList<QPointF>> runs(1);
  m_steps.clear();
  m_pending = false;
  for (const QPointF &p : points)
  {
    Step step;
    step.pendingBefore = m_pending;
    if (std::isnan(p.y()))
    {
      step.gap = true;
      m_pending = true;
    }
    else
    {
      if (m_pending && !runs.last().isEmpty())
      {
        runs.append(QList<QPointF>());
        step.newSegment = true;
      }
      m_pending = false;
      runs.last().append(p);
    }
    m_steps << step;
    if (m_steps.size() > kUndo)
      m_steps.removeFirst();
  }

  m_used = int(runs.size());
  for (int i = 0; i < runs.size(); ++i)
    segment(i)->replace(runs[i]);
  for (int i = m_used; i < m_segments.size(); ++i)
    if (m_segments[i]->count() > 0)
      m_segments[i]->clear();
}

void GapLine::clear()
{
  replace(QList<QPointF>());
}

int GapLine::segments() const
{
  int n = 0;
  for (const QLineSeries *s : m_segments)
    if (s->count() > 0)
      n++;
  return n;
}
