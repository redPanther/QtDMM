// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/views/gapline.h"

#include <QAbstractAxis>
#include <QChart>
#include <QLineSeries>
#include <QScatterSeries>

#include <algorithm>
#include <cmath>

GapLine::GapLine(QLineSeries *first)
{
  m_segments << first;
  m_pen = first->pen();
  m_visible = first->isVisible();
}

QLineSeries *GapLine::newSeries(const QPen &pen)
{
  QLineSeries *first = m_segments.first();
  auto *s = new QLineSeries();
  first->chart()->addSeries(s);
  for (QAbstractAxis *axis : first->attachedAxes())
    s->attachAxis(axis);
  s->setPen(pen);
  s->setVisible(m_visible);
  return s;
}

QLineSeries *GapLine::segment(int i)
{
  while (m_segments.size() <= i)
    m_segments << newSeries(m_pen);
  return m_segments[i];
}

// the hold: the line's colour, faint
static QPen holdPen(QPen pen)
{
  QColor c = pen.color();
  c.setAlphaF(c.alphaF() * 0.4);
  pen.setColor(c);
  return pen;
}

void GapLine::setPen(const QPen &pen)
{
  m_pen = pen;
  for (QLineSeries *s : std::as_const(m_segments))
    s->setPen(pen);
  if (m_hold)
    m_hold->setPen(holdPen(pen));
}

void GapLine::setAxes(const QList<QAbstractAxis *> &axes)
{
  QList<QLineSeries *> all = m_segments;
  if (m_hold)
    all << m_hold;
  for (QLineSeries *s : std::as_const(all))
  {
    for (QAbstractAxis *axis : s->attachedAxes())
      if (!axes.contains(axis))
        s->detachAxis(axis);
    for (QAbstractAxis *axis : axes)
      if (!s->attachedAxes().contains(axis))
        s->attachAxis(axis);
  }
}

void GapLine::setMarkers(QScatterSeries *markers)
{
  if (m_markers && m_markers != markers && m_markers->count() > 0)
    m_markers->clear();
  m_markers = markers;
}

void GapLine::setVisible(bool on)
{
  m_visible = on;
  for (QLineSeries *s : std::as_const(m_segments))
    s->setVisible(on);
  if (m_hold)
    m_hold->setVisible(on);
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
  QList<QPointF> marks;
  for (const QPointF &p : points)
  {
    m_all << p;
    if (!m_toEnd)
      continue;
    add(p);
    if (!std::isnan(p.y()))
      marks << p;
  }
  // in one change: Qt Charts lays a scatter series out anew with each
  if (m_markers && !marks.isEmpty())
    m_markers->append(marks);
  updateHold();
}

void GapLine::removeLast(int count)
{
  count = int(qMin<qsizetype>(count, m_all.size()));
  m_all.resize(m_all.size() - count);
  if (!m_toEnd)
    return;
  if (m_steps.size() < count || m_all.size() < m_from)
  {
    load();   // more than the chart's newest points can undo
    return;
  }
  int marks = 0;
  for (int i = 0; i < count; ++i)
  {
    const Step step = m_steps.takeLast();
    if (!step.gap)
    {
      QLineSeries *open = m_segments[m_used - 1];
      open->removePoints(open->count() - 1, 1);
      if (step.newSegment)
        m_used--;   // empty again, back in the pool
      marks++;
    }
    m_pending = step.pendingBefore;
  }
  if (m_markers && marks > 0)
    m_markers->removePoints(m_markers->count() - marks, marks);
  updateHold();
}

void GapLine::replace(const QList<QPointF> &points)
{
  m_all = points;
  load();
}

void GapLine::setSpan(double min, double max)
{
  m_spanMin = min;
  m_spanMax = max;
  const double width = max - min;
  if (!(width > 0) || !std::isfinite(width))
  {
    if (m_from > 0 || !m_toEnd)
      load();
    return;
  }
  // anew when the span leaves what the chart has, or when that is much
  // more than the span (zoomed in, or Live has run on for a while)
  const double loadedTo = m_toEnd ? qMax(max, m_all.isEmpty() ? max : m_all.last().x()) : m_loadMax;
  if ((min < m_loadMin && m_from > 0) || (!m_toEnd && max > m_loadMax) || loadedTo - m_loadMin > 2 * width)
    load();
}

void GapLine::load()
{
  const double width = m_spanMax - m_spanMin;
  m_from = 0;
  qsizetype to = m_all.size();
  m_loadMin = -qInf();
  m_loadMax = qInf();
  if (width > 0 && std::isfinite(width))
  {
    // the span and half of it on each side; a point beyond, so that the line
    // runs out of the plot
    m_loadMin = m_spanMin - width / 2;
    m_loadMax = m_spanMax + width / 2;
    auto byX = [](const QPointF &p, double x) { return p.x() < x; };
    m_from = qMax<qsizetype>(0, std::lower_bound(m_all.cbegin(), m_all.cend(), m_loadMin, byX) - m_all.cbegin() - 1);
    to = qMin<qsizetype>(m_all.size(), std::lower_bound(m_all.cbegin(), m_all.cend(), m_loadMax, byX) - m_all.cbegin() + 1);
  }
  m_toEnd = to >= m_all.size();

  // split first, then one replace() per segment: much faster than append()
  // point by point
  QList<QList<QPointF>> runs(1);
  m_steps.clear();
  m_pending = false;
  for (qsizetype i = m_from; i < to; ++i)
  {
    const QPointF &p = m_all[i];
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
  QList<QPointF> marks;
  for (int i = 0; i < runs.size(); ++i)
  {
    segment(i)->replace(runs[i]);
    if (m_markers)
      marks << runs[i];
  }
  if (m_markers)
    m_markers->replace(marks);
  for (int i = m_used; i < m_segments.size(); ++i)
    if (m_segments[i]->count() > 0)
      m_segments[i]->clear();
  updateHold();
}

void GapLine::setHold(double x)
{
  m_holdX = x;
  updateHold();
}

void GapLine::updateHold()
{
  // the newest value holds until the next one; not after a gap
  const bool on = m_toEnd && !m_all.isEmpty() && std::isfinite(m_all.last().y()) && m_holdX > m_all.last().x();
  if (!on)
  {
    if (m_hold && m_hold->count() > 0)
      m_hold->clear();
    return;
  }
  if (!m_hold)
    m_hold = newSeries(holdPen(m_pen));
  const QPointF last = m_all.last();
  m_hold->replace({ last, QPointF(m_holdX, last.y()) });
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
