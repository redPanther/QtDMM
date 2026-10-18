//======================================================================
// File:		graphwidget_curves.cpp (from dmmgraph.cpp)
// Author:	Matthias Toussaint
// Created:	Tue Apr 10 17:45:35 CEST 2001
//----------------------------------------------------------------------
// This file is part of QtDMM.
//
// QtDMM is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// QtDMM is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with QtDMM.  If not, see <http://www.gnu.org/licenses/>.
//----------------------------------------------------------------------
// Copyright (c) 2001 Matthias Toussaint
//======================================================================
//
// GraphWidget, part: building the curves: buckets, the integral, the envelope, the other values and the legend.
// The class is in graphwidget.h, its other parts in graphwidget*.cpp.

#include <QtGui>
#include <QtWidgets>
#include <QPen>
#include <cmath>
#include <climits>

#include "ui/views/graphwidget.h"
#include "ui/views/graphwidget_p.h"

QPainterPath GraphWidget::envelopePath() const
{
  return m_envelope->isVisible() ? m_envelope->path() : QPainterPath();
}

// Columns of one pixel over the plot (and kEnvelopeColumns beyond each
// edge, so the band runs out of the plot as the curve does): the lowest
// and highest of the curve as drawn in each, its points and the lines
// between them - readings further apart than the band is wide would leave
// it a staircase. The curve has a point for every bucket, its minimum and
// maximum, the buckets themselves a column wide, so this reads a few
// points per column, not the readings. A gap (NaN) parts the band as it
// parts the curve. Each column gets the extremes of the columns within
// kEnvelopeColumns of it on its side of a gap.
void GraphWidget::updateEnvelope()
{
  const QRectF plot = m_chart->plotArea();
  const double x0 = m_xAxis->min(), x1 = m_xAxis->max();
  const double y0 = m_yAxis->min(), y1 = m_yAxis->max();
  const QList<QPointF> pts = m_envelopeOn ? referencePoints() : QList<QPointF>();
  if (pts.isEmpty() || plot.width() < 2 || plot.height() <= 0 || !(x1 > x0) || !(y1 > y0))
  {
    m_envelope->setVisible(false);
    return;
  }
  const int k = kEnvelopeColumns;
  const int columns = int(plot.width()) + 2 * k;
  const double perColumn = (x1 - x0) / plot.width();
  const double from = x0 - k * perColumn, to = x1 + k * perColumn;
  struct Column
  {
    double lo = qInf(), hi = -qInf();
    int run = -1;   ///< which stretch between gaps; -1 no point
  };
  QList<Column> col(columns);
  auto take = [&](int c, double lo, double hi, int r)
  {
    Column &cc = col[c];
    cc.lo = qMin(cc.lo, lo);
    cc.hi = qMax(cc.hi, hi);
    cc.run = r;
  };
  auto columnOf = [&](double x) { return qBound(0, int((x - from) / perColumn), columns - 1); };
  // the point before the window too: its line runs into it
  auto it = std::lower_bound(pts.cbegin(), pts.cend(), from, [](const QPointF &p, double x) { return p.x() < x; });
  if (it != pts.cbegin())
    --it;
  int run = 0;
  const QPointF *prev = nullptr;
  for (; it != pts.cend(); ++it)
  {
    if (std::isnan(it->y()))
    {
      // a gap parts the curve and the band
      if (it->x() > to)
        break;
      run++;
      prev = nullptr;
      continue;
    }
    if (it->x() >= from)
    {
      const int c = columnOf(it->x());
      take(c, it->y(), it->y(), run);
      // the line from the point before: in each column it crosses, the
      // part of it there
      if (prev)
        for (int j = columnOf(prev->x()); j <= c; ++j)
        {
          const double xa = qMax(prev->x(), from + j * perColumn), xb = qMin(it->x(), from + (j + 1) * perColumn);
          const double dx = it->x() - prev->x();
          auto yAt = [&](double x) { return dx > 0 ? prev->y() + (it->y() - prev->y()) * (x - prev->x()) / dx : it->y(); };
          take(j, qMin(yAt(xa), yAt(xb)), qMax(yAt(xa), yAt(xb)), run);
        }
    }
    if (it->x() > to)
      break;
    prev = &*it;
  }
  auto yOf = [&](double v) { return plot.bottom() - (v - y0) / (y1 - y0) * plot.height(); };
  auto xOf = [&](int c) { return plot.left() + (c - k + 0.5); };
  QPainterPath path;
  QList<QPointF> upper, lower;
  auto flush = [&]
  {
    if (upper.size() > 1)
    {
      QPolygonF poly(upper);
      for (qsizetype i = lower.size() - 1; i >= 0; --i)
        poly << lower[i];
      path.addPolygon(poly);
      path.closeSubpath();
    }
    upper.clear();
    lower.clear();
  };
  int lastRun = -1;
  for (int c = 0; c < columns; ++c)
  {
    if (col[c].run < 0)
      continue;
    if (col[c].run != lastRun)
      flush();
    lastRun = col[c].run;
    double lo = col[c].lo, hi = col[c].hi;
    for (int j = qMax(0, c - k); j <= qMin(columns - 1, c + k); ++j)
      if (col[j].run == col[c].run)
      {
        lo = qMin(lo, col[j].lo);
        hi = qMax(hi, col[j].hi);
      }
    upper << QPointF(xOf(c), yOf(hi));
    lower << QPointF(xOf(c), yOf(lo));
  }
  flush();
  QPainterPath clip;
  clip.addRect(plot);
  m_envelope->setPath(path.intersected(clip));
  m_envelope->setVisible(true);
}

// ms per pixel column, rounded up to a power of two. Counted over the window
// or, while the recording does not fill it yet, over what is there: a short
// recording in a long window is drawn as it is. Readings further apart than a
// column are a bucket each. The power of two: a growing recording builds the
// curve anew only when the buckets double, not every few readings (as the
// overview, GraphMinimap); a bucket is then one to two columns wide.
qint64 GraphWidget::bucketSize() const
{
  const double plotWidth = m_chart->plotArea().width();
  const int columns = qMax(1, int(plotWidth > 0 ? plotWidth : width()));
  const qint64 shown = qMin<qint64>(qint64(m_windowSeconds) * 1000, m_store->duration() - m_store->origin());
  const qint64 need = (shown + columns - 1) / columns;
  qint64 ms = 1;
  while (ms < need)
    ms *= 2;
  return ms;
}

// The index of the first reading in the bucket of reading @p i. The buckets
// are columns of time counted from the start of the recording: while the
// window scrolls, a reading stays in its bucket and the drawn minima and
// maxima stay put instead of jittering with every new reading.
int GraphWidget::bucketStart(int i) const
{
  const RecordingSeries &series = m_store->series();
  const qint64 begin = series.at(i).t / m_bucket * m_bucket;
  return qMin(i, series.lowerBound(begin));
}

// The points of the readings first..last: the reading itself, or with more
// than one the minimum and the maximum in time order, so a spike survives
// the thinning. The store keeps every reading; only the drawing is thinned.
// Readings without a value (NaN: overload, stale) do not count; a bucket of
// nothing else is one NaN point, the gap, and one that ends where the meter
// fell silent ends with it.
int GraphWidget::pointsOf(const RecordingSeries &series, int first, int last, bool integral, QList<QPointF> &out) const
{
  auto value = [&](int i)
  {
    const RawPoint &p = series.at(i);
    return integral ? m_integrationOffset + p.integral * m_integrationScale : p.value;
  };
  auto x = [&](int i) { return series.at(i).t / 1000.0; };
  while (first <= last && std::isnan(value(first)))
    first++;
  if (first > last)
  {
    out.append(QPointF(x(last), qQNaN()));
    return 1;
  }
  int lo = first, hi = first;
  double loValue = value(first), hiValue = loValue;
  for (int i = first + 1; i <= last; i++)
  {
    const double v = value(i);
    if (std::isnan(v))
      continue;
    if (v < loValue)
    {
      loValue = v;
      lo = i;
    }
    if (v > hiValue)
    {
      hiValue = v;
      hi = i;
    }
  }
  // a bucket that ends where the meter fell silent: the gap goes on to the
  // next reading, in a later bucket - the line must not bridge it (a sensor
  // silent for minutes was drawn as a straight line whenever its last value
  // and the gap fell into one column). An overload comes at the meter's
  // pace and is as short as a reading.
  const bool gapAfter = std::isnan(value(last)) && series.at(last).quality == Quality::Stale;
  int n = 1;
  if (lo == hi)
    out.append(QPointF(x(lo), loValue));
  else
  {
    if (lo > hi)
    {
      qSwap(lo, hi);
      qSwap(loValue, hiValue);
    }
    out.append(QPointF(x(lo), loValue));
    out.append(QPointF(x(hi), hiValue));
    n = 2;
  }
  if (gapAfter)
  {
    out.append(QPointF(x(last), qQNaN()));
    n++;
  }
  return n;
}

void GraphWidget::rebuildSeries()
{
  m_rebuilds++;
  QList<QPointF> points;
  QList<QPointF> intPoints;
  const int count = m_store->count();
  m_bucket = bucketSize();
  points.reserve(2 * count + 2);
  intPoints.reserve(2 * count + 2);

  m_tailData = m_tailInt = 0;
  const RecordingSeries &series = m_store->series();
  for (int first = 0; first < count;)
  {
    // the readings of this bucket
    const qint64 end = (series.at(first).t / m_bucket + 1) * m_bucket;
    const int last = qMin(series.lowerBound(end), count) - 1;
    m_tailData = pointsOf(m_store->series(), first, last, false, points);
    if (m_intValueId == 0 && m_showIntegration)
      m_tailInt = pointsOf(m_store->series(), first, last, true, intPoints);
    first = last + 1;
  }

  m_dataLine->replace(points);
  m_curvePts = points;
  m_dataPoints->replace(pointsShown() ? withoutGaps(points) : QList<QPointF>());
  // the integral of another value: rebuildExtras()
  m_intLine->replace(intPoints);
  m_intPoints->replace(intPointsShown() ? withoutGaps(intPoints) : QList<QPointF>());
  m_tailDataPts = pointsShown() ? finiteTail(points, m_tailData) : 0;
  m_tailIntPts = intPointsShown() ? finiteTail(intPoints, m_tailInt) : 0;
  rebuildExtras();   // the same buckets
}

// The newest reading went into the series: a new bucket adds its point, one
// that is still filling replaces the points it had.
void GraphWidget::appendToSeries()
{
  const int count = m_store->count();
  const int first = bucketStart(count - 1);
  const bool filling = first != count - 1;
  if (filling)
  {
    m_dataLine->removeLast(m_tailData);
    m_curvePts.resize(qMax<qsizetype>(0, m_curvePts.size() - m_tailData));
  }
  QList<QPointF> points;
  m_tailData = pointsOf(m_store->series(), first, count - 1, false, points);
  m_dataLine->append(points);
  m_curvePts.append(points);
  if (pointsShown())
    m_tailDataPts = replaceTail(m_dataPoints, filling ? m_tailDataPts : 0, withoutGaps(points));
  if (m_intValueId == 0 && m_showIntegration)   // else onExtraAppended()
    appendIntegral(m_store->series(), first, count - 1, filling);
}

// The integral's points of readings first..last of @p series, in place of
// the ones they had while the bucket was @p filling.
void GraphWidget::appendIntegral(const RecordingSeries &series, int first, int last, bool filling)
{
  if (filling)
    m_intLine->removeLast(m_tailInt);
  QList<QPointF> intPoints;
  m_tailInt = pointsOf(series, first, last, true, intPoints);
  m_intLine->append(intPoints);
  if (intPointsShown())
    m_tailIntPts = replaceTail(m_intPoints, filling ? m_tailIntPts : 0, withoutGaps(intPoints));
}

// The last @p remove points of @p series give way to @p points, in one
// change: Qt Charts lays a scatter series out anew with every change, and
// removing and appending point by point were three. Returns how many came.
int GraphWidget::replaceTail(QScatterSeries *series, int remove, const QList<QPointF> &points)
{
  QList<QPointF> all = series->points();
  all.resize(qMax<qsizetype>(0, all.size() - remove));
  all.append(points);
  series->replace(all);
  return int(points.size());
}

void GraphWidget::setIntegrationValue(int id)
{
  id = qMax(0, id);
  if (id == m_intValueId)
    return;
  m_intValueId = id;
  // the curve on the main value's axis until its value says otherwise
  integralOnAxis(m_yAxis);
  rebuildSeries();
}

void GraphWidget::integralOnAxis(QAbstractAxis *axis)
{
  m_intLine->setAxes({ m_xAxis, axis });
  for (QAbstractAxis *a : m_intPoints->attachedAxes())
    if (a != m_xAxis && a != axis)
      m_intPoints->detachAxis(a);
  if (!m_intPoints->attachedAxes().contains(axis))
    m_intPoints->attachAxis(axis);
}

QList<QPointF> GraphWidget::withoutGaps(const QList<QPointF> &points)
{
  QList<QPointF> out;
  out.reserve(points.size());
  for (const QPointF &p : points)
    if (!std::isnan(p.y()))
      out.append(p);
  return out;
}

// how many of the last @p tail points have a value
int GraphWidget::finiteTail(const QList<QPointF> &points, int tail)
{
  int n = 0;
  for (qsizetype i = qMax<qsizetype>(0, points.size() - tail); i < points.size(); ++i)
    if (!std::isnan(points[i].y()))
      n++;
  return n;
}

void GraphWidget::onAppended(bool shifted)
{
  const RawPoint &p = m_store->series().last();

  // "All": the window grows once the recording fills it. The new window
  // rebuilds the series with this reading in it: then it must not be
  // appended again
  // A window much longer than what there is (Live starts with All, the
  // window from the settings) comes down to it.
  const int rebuilds = m_rebuilds;
  const qint64 recorded = m_store->duration() - m_store->origin();
  const qint64 window = qint64(m_windowSeconds) * 1000;
  if (m_followAll && (recorded >= window || window > 2 * qMax<qint64>(10000, recorded)))
    requestAll(true);
  const bool rebuilt = m_rebuilds != rebuilds;
  updateScrollRange();
  // a full store, or Live: the window moves on with what it keeps
  if (m_store->origin() > 0 || m_store->state() == RecordingStore::Live)
  {
    updateXAxisRange();
    updateMarkPositions();
  }

  const bool resFlag = m_autoScale && m_mainShown && growScale(p.value, m_scaleMin, m_scaleMax);

  if (shifted || bucketSize() != m_bucket)
    rebuildSeries();
  else if (!rebuilt)
    appendToSeries();

  if (resFlag)
  {
    setYRange(m_scaleMin, m_scaleMax);
    updateThresholdLinePositions();
  }
  else if (m_referenceKind == RefMean || m_referenceKind == RefMin || m_referenceKind == RefMax)
    updateReferenceLine();
  if (m_cursorT[0] >= 0 || m_cursorT[1] >= 0)
    updateTimeCursors();
  updateEnvelope();
}

void GraphWidget::rebuildExtras()
{
  const int n = m_store->extraCount();
  // the units: the main value's on the left; on the right the unit of the
  // lowest value in another one (by number, not by which came first), its
  // newest series. The integrated value has its axis also while it is hidden
  const QString main = leftUnit();
  m_unit2.clear();
  int unit2Id = INT_MAX;
  for (int i = 0; i < n; ++i)
  {
    const RecordingSeries &s = m_store->extra(i);
    const bool onAxis = m_shownValues.contains(s.id) || (m_intValueId > 0 && s.id == m_intValueId);
    if (onAxis && !s.unit.isEmpty() && s.unit != main && s.id <= unit2Id)
    {
      unit2Id = s.id;
      m_unit2 = s.unit;
    }
  }

  while (int(m_extraCurves.size()) < n)
  {
    ExtraCurve c;
    c.series = new QLineSeries();
    m_chart->addSeries(c.series);
    c.series->attachAxis(m_xAxis);
    c.series->attachAxis(m_yAxis);
    c.line = std::make_unique<GapLine>(c.series);
    m_extraCurves.push_back(std::move(c));
  }

  m_scale2Min = 1e40;
  m_scale2Max = -1e40;
  bool leftGrew = false;
  for (int i = 0; i < int(m_extraCurves.size()); ++i)
  {
    ExtraCurve &c = m_extraCurves[size_t(i)];
    c.line->clear();
    c.points.clear();
    c.tail = 0;
    if (i >= n)
    {
      c.shown = false;
      c.line->setVisible(false);
      continue;
    }
    const RecordingSeries &s = m_store->extra(i);
    // a series without a unit yet (only gaps) goes with the main one
    const bool left = s.unit.isEmpty() || s.unit == main;
    c.right = !left && s.unit == m_unit2;
    c.shown = m_shownValues.contains(s.id) && (left || c.right);
    c.line->setAxes({ m_xAxis, c.right ? m_yAxis2 : m_yAxis });
    const bool dashed = phosphor();
    c.line->setPen(QPen(extraColor(s.id), m_lineWidth, dashed ? (s.id % 2 ? Qt::DashLine : Qt::DotLine) : Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    c.line->setVisible(c.shown);
    if (!c.shown)
    {
      // the right axis also scales a hidden value integrated there
      if (c.right && s.id == m_intValueId)
        for (int j = 0; j < s.count(); ++j)
          if (std::isfinite(s.at(j).value))
            growScale(s.at(j).value, m_scale2Min, m_scale2Max);
      continue;
    }
    QList<QPointF> points;
    const int count = s.count();
    for (int first = 0; first < count;)
    {
      const qint64 end = (s.at(first).t / m_bucket + 1) * m_bucket;
      const int last = qMin(s.lowerBound(end), count) - 1;
      c.tail = pointsOf(s, first, last, false, points);
      first = last + 1;
    }
    c.line->replace(points);
    c.points = points;
    for (const QPointF &p : std::as_const(points))
      if (std::isfinite(p.y()))
      {
        if (c.right)
          growScale(p.y(), m_scale2Min, m_scale2Max);
        else if (m_autoScale)
          leftGrew = growScale(p.y(), m_scaleMin, m_scaleMax) || leftGrew;
      }
  }
  if (leftGrew)
  {
    setYRange(m_scaleMin, m_scaleMax);
    updateThresholdLinePositions();
  }
  updateRightAxis();
  updateLegend();
  if (m_intValueId != 0)
    rebuildOtherIntegral();
}

// The integration curve of another value than the main one: its series in
// turn (a gap between them), on the axis the value is on.
void GraphWidget::rebuildOtherIntegral()
{
  QList<QPointF> points;
  QAbstractAxis *axis = m_yAxis;
  m_tailInt = 0;
  for (int i = 0; i < m_store->extraCount(); ++i)
  {
    const RecordingSeries &s = m_store->extra(i);
    if (s.id != m_intValueId || s.isEmpty())
      continue;
    if (i < int(m_extraCurves.size()) && m_extraCurves[size_t(i)].right)
      axis = m_yAxis2;
    if (!m_showIntegration)
      continue;
    if (!points.isEmpty())
      points.append(QPointF(s.first().t / 1000.0, qQNaN()));
    for (int first = 0; first < s.count();)
    {
      const qint64 end = (s.at(first).t / m_bucket + 1) * m_bucket;
      const int last = qMin(s.lowerBound(end), s.count()) - 1;
      m_tailInt = pointsOf(s, first, last, true, points);
      first = last + 1;
    }
  }
  integralOnAxis(axis);
  m_intLine->replace(points);
  m_intPoints->replace(intPointsShown() ? withoutGaps(points) : QList<QPointF>());
  m_tailIntPts = intPointsShown() ? finiteTail(points, m_tailInt) : 0;
  updateThresholdLinePositions();   // the integration threshold on that axis
}

void GraphWidget::onExtraAppended(int i)
{
  if (i >= int(m_extraCurves.size()))
  {
    rebuildExtras();
    return;
  }
  if (bucketSize() != m_bucket)
  {
    rebuildSeries();   // sets the bucket, then rebuildExtras()
    return;
  }
  ExtraCurve &c = m_extraCurves[size_t(i)];
  const RecordingSeries &s = m_store->extra(i);
  const int count = s.count();
  // as appendToSeries(): a new bucket adds its points, one still filling
  // replaces them
  const qint64 begin = s.at(count - 1).t / m_bucket * m_bucket;
  const int first = qMin(count - 1, s.lowerBound(begin));
  // the integration curve of this value
  if (s.id == m_intValueId && m_showIntegration)
    appendIntegral(s, first, count - 1, first != count - 1);
  if (!c.shown)
  {
    const double v = s.last().value;
    if (c.right && s.id == m_intValueId && std::isfinite(v) && growScale(v, m_scale2Min, m_scale2Max))
      updateRightAxis();
    return;
  }
  if (first != count - 1)
  {
    c.line->removeLast(c.tail);
    c.points.resize(qMax<qsizetype>(0, c.points.size() - c.tail));
  }
  QList<QPointF> points;
  c.tail = pointsOf(s, first, count - 1, false, points);
  c.line->append(points);
  c.points.append(points);
  const double v = s.last().value;
  bool grew = false;
  if (std::isfinite(v) && c.right)
  {
    if (growScale(v, m_scale2Min, m_scale2Max))
      updateRightAxis();
  }
  else if (std::isfinite(v) && m_autoScale && growScale(v, m_scaleMin, m_scaleMax))
  {
    setYRange(m_scaleMin, m_scaleMax);
    updateThresholdLinePositions();
    grew = true;   // the reference line and the band with it
  }
  // what follows the value of the left axis, as onAppended() for the main one
  if (s.id == referenceId() && !c.right && !grew)
  {
    if (m_referenceKind == RefMean || m_referenceKind == RefMin || m_referenceKind == RefMax)
      updateReferenceLine();
    updateEnvelope();
  }
  if (m_cursorT[0] >= 0 || m_cursorT[1] >= 0)
    updateTimeCursors();
}

QString GraphWidget::legendText() const
{
  return m_legend && m_legend->isVisible() ? m_legend->text() : QString();
}

void GraphWidget::updateLegend()
{
  if (!m_legend)
    return;
  // one entry per value, its newest series; only with more than the main one
  QStringList entries;
  QSet<int> seen;
  auto entry = [](const QColor &c, const QString &name)
  {
    return QString("<span style=\"color:%1\">&#9632;</span>&nbsp;%2").arg(c.name(), name.toHtmlEscaped());
  };
  for (int i = m_store->extraCount() - 1; i >= 0; --i)
  {
    const RecordingSeries &s = m_store->extra(i);
    if (seen.contains(s.id) || i >= int(m_extraCurves.size()) || !m_extraCurves[size_t(i)].shown)
      continue;
    seen.insert(s.id);
    entries.prepend(entry(extraColor(s.id), curveName(s, s.unit)));
  }
  // the main value alone needs none; without it, the axis does not say which
  if (entries.isEmpty() && m_mainShown)
  {
    m_legend->hide();
    return;
  }
  if (m_mainShown)
    entries.prepend(entry(dataColor(), curveName(m_store->series(), m_store->unit())));
  if (entries.isEmpty())
  {
    m_legend->hide();
    return;
  }
  const QColor ink = m_xLabelColor.isValid() ? m_xLabelColor : palette().color(QPalette::WindowText);
  m_legend->setStyleSheet(QString("QLabel { color: %1; background: transparent; }").arg(ink.name()));
  m_legend->setText(entries.join("&nbsp;&nbsp;&nbsp;"));
  m_legend->adjustSize();
  // top left inside the plot, below the state ("LIVE", "REC")
  const QPoint topLeft = m_chartView->mapFromScene(m_chart->plotArea().topLeft());
  const int below = m_stateLabel && m_stateLabel->isVisible() ? m_stateLabel->height() : 0;
  m_legend->move(topLeft.x() + 6, topLeft.y() + 4 + below);
  m_legend->show();
  m_legend->raise();
}

void GraphWidget::setValueShown(int id, bool shown)
{
  if (id == 0)
  {
    if (shown == m_mainShown)
      return;
    m_mainShown = shown;
  }
  else if (shown == m_shownValues.contains(id))
    return;
  else if (shown)
    m_shownValues.insert(id);
  else
    m_shownValues.remove(id);
  // the view afresh: the left unit may be another one now
  m_scale2Min = 1e40;
  m_scale2Max = -1e40;
  updateSeriesAppearance();   // the curves, then rebuildExtras()
  showUnit();
  if (m_autoScale)
    setScale(true, m_includeZero, m_scaleMin, m_scaleMax);
  updateYLabels();
  updateThresholdLinesVisibility();
  updateReferenceLine();
  updateEnvelope();
  updateTimeCursors();
}

int GraphWidget::referenceId() const
{
  if (m_mainShown)
    return 0;
  int lowest = -1;
  for (int i = 0; i < m_store->extraCount(); ++i)
  {
    const RecordingSeries &s = m_store->extra(i);
    if (m_shownValues.contains(s.id) && !s.unit.isEmpty() && (lowest < 0 || s.id < lowest))
      lowest = s.id;
  }
  return lowest;
}

QString GraphWidget::leftUnit() const
{
  // the newest series of the value has the say
  const int id = referenceId();
  for (int i = m_store->extraCount() - 1; id > 0 && i >= 0; --i)
    if (m_store->extra(i).id == id && !m_store->extra(i).unit.isEmpty())
      return m_store->extra(i).unit;
  return m_store->unit();
}

QList<QPointF> GraphWidget::referencePoints() const
{
  const int id = referenceId();
  if (id == 0)
    return m_curvePts;
  // the curves of the value on the left (Hz, then % again: two), in time,
  // a gap between them
  QList<const QList<QPointF> *> parts;
  for (int i = 0; id > 0 && i < m_store->extraCount() && i < int(m_extraCurves.size()); ++i)
  {
    const ExtraCurve &c = m_extraCurves[size_t(i)];
    if (m_store->extra(i).id == id && c.shown && !c.right && !c.points.isEmpty())
      parts << &c.points;
  }
  if (parts.size() == 1)
    return *parts.first();
  std::sort(parts.begin(), parts.end(), [](auto a, auto b) { return a->first().x() < b->first().x(); });
  QList<QPointF> out;
  for (const QList<QPointF> *p : std::as_const(parts))
  {
    if (!out.isEmpty())
      out << QPointF(p->first().x(), qQNaN());
    out += *p;
  }
  return out;
}

QList<int> GraphWidget::shownValues() const
{
  QList<int> ids(m_shownValues.cbegin(), m_shownValues.cend());
  std::sort(ids.begin(), ids.end());
  return ids;
}
