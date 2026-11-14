// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/views/xyplot.h"
#include "ui/views/plottheme.h"

#include "3rdparty/qcustomplot/qcustomplot.h"
#include "ui/views/plotaxis.h"

#include <cmath>
#include <utility>

XYPlot::XYPlot(QWidget *parent) : QcpGraphCommon(parent, tr("No XY data yet"))
{
  m_graph = m_plot->addGraph(m_plot->xAxis, m_plot->yAxis);
  m_graph->setLineStyle(QCPGraph::lsNone);
  m_graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 5));
  m_graph->setAdaptiveSampling(true);
  for (QCPAxis *axis : { m_plot->xAxis, m_plot->yAxis }) {
    axis->setNumberFormat(QStringLiteral("gb")); axis->setNumberPrecision(4);
  }
  refreshPlot();
}

void XYPlot::setThemeColors(const QBrush &background, const QColor &grid, const QColor &labels, const QColor &data)
{
  PlotTheme t{background.style() == Qt::NoBrush ? QBrush(palette().base()) : background, grid, labels, data};
  setTheme(t);
  m_graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, QPen(data), QBrush(data), 5));
}

void XYPlot::setLegend(const QString &legend)
{
  setTitle(legend);
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void XYPlot::setAxisUnits(const QString &x, const QString &y)
{
  m_xUnit = x;
  m_yUnit = y;
  refreshPlot();
}

void XYPlot::addPoint(double x, double y)
{
  if (!std::isfinite(x) || !std::isfinite(y))
    return;
  m_points.append(QPointF(x, y));
  if (!hold())
    refreshPlot();
}

void XYPlot::clear()
{
  m_points.clear();
  m_shown.clear();
  refreshPlot();
}

void XYPlot::setHold(bool hold)
{
  if (this->hold() == hold) return;
  QcpGraphCommon::setHold(hold);
  m_shown = hold ? m_points.hold() : QVector<QPointF>();
  refreshPlot();
}

void XYPlot::refreshPlot()
{
  const QVector<QPointF> live = m_points.values();
  const QVector<QPointF> &shown = hold() ? m_shown : live;
  setNoDataVisible(shown.isEmpty());
  if (shown.isEmpty())
  {
    m_graph->data()->clear();
    const PlotAxis::Range xAxis = PlotAxis::range(0, 0, m_xUnit);
    const PlotAxis::Range yAxis = PlotAxis::range(0, 0, m_yUnit);
    setAxisRange(m_plot->xAxis, xAxis);
    setAxisRange(m_plot->yAxis, yAxis);
    m_plot->replot(QCustomPlot::rpQueuedReplot);
    return;
  }

  double xmin = shown.first().x(), xmax = xmin;
  double ymin = shown.first().y(), ymax = ymin;
  for (const QPointF &point : std::as_const(shown))
  {
    xmin = qMin(xmin, point.x());
    xmax = qMax(xmax, point.x());
    ymin = qMin(ymin, point.y());
    ymax = qMax(ymax, point.y());
  }
  const PlotAxis::Range xAxis = PlotAxis::range(xmin, xmax, m_xUnit);
  const PlotAxis::Range yAxis = PlotAxis::range(ymin, ymax, m_yUnit);
  setAxisRange(m_plot->xAxis, xAxis);
  setAxisRange(m_plot->yAxis, yAxis);

  QVector<double> xValues;
  QVector<double> yValues;
  xValues.reserve(shown.size());
  yValues.reserve(shown.size());
  for (const QPointF &point : shown)
  {
    xValues.append(point.x() / xAxis.factor);
    yValues.append(point.y() / yAxis.factor);
  }
  m_graph->setData(xValues, yValues);
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}
