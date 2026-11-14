// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/views/heatmapplot.h"

#include "ui/views/plotaxis.h"
#include "ui/views/plottheme.h"

#include "3rdparty/qcustomplot/qcustomplot.h"

#include <cmath>

namespace
{
constexpr int kMaxGridDimension = 256;
}

HeatmapPlot::HeatmapPlot(QWidget *parent) : QcpGraphCommon(parent, tr("No heatmap data yet"))
{
  m_plot->plotLayout()->insertColumn(1);

  m_map = new QCPColorMap(m_plot->xAxis, m_plot->yAxis);
  m_map->data()->setSize(m_nx, m_ny);
  m_map->setInterpolate(false);
  m_map->setTightBoundary(false);
  m_map->data()->setRange(QCPRange(0, 1), QCPRange(0, 1));
  m_map->setDataRange(QCPRange(0, 1));

  m_scale = new QCPColorScale(m_plot);
  m_scale->setMinimumMargins(QMargins(0, 0, 0, 0));
  m_plot->plotLayout()->addElement(1, 1, m_scale);
  m_scale->setType(QCPAxis::atRight);
  m_map->setColorScale(m_scale);
  auto *axisMargins = new QCPMarginGroup(m_plot);
  m_plot->axisRect()->setMarginGroup(QCP::msTop | QCP::msBottom, axisMargins);
  m_scale->axis()->axisRect()->setMarginGroup(QCP::msTop | QCP::msBottom, axisMargins);

  applyGradient();
  setAxisUnits({}, {}, {});
  setLegend({});
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void HeatmapPlot::setThemeColors(const QBrush &background, const QColor &grid, const QColor &labels)
{
  PlotTheme t{background.style() == Qt::NoBrush ? QBrush(palette().base()) : background, grid, labels, {}};
  setTheme(t);
  m_scale->axis()->setBasePen(QPen(grid)); m_scale->axis()->setTickPen(QPen(grid));
  m_scale->axis()->setSubTickPen(QPen(grid)); m_scale->axis()->setTickLabelColor(labels); m_scale->axis()->setLabelColor(labels);
  m_scale->axis()->grid()->setPen(QPen(grid, 1, Qt::DotLine)); m_scale->axis()->grid()->setSubGridPen(QPen(grid, 1, Qt::DotLine));
}

void HeatmapPlot::setAxisUnits(const QString &x, const QString &y, const QString &z)
{
  m_xUnit = x;
  m_yUnit = y;
  m_zUnit = z;
  refreshMap();
}

void HeatmapPlot::setLegend(const QString &legend)
{
  setTitle(legend);
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void HeatmapPlot::setColorScheme(ColorMap::Scheme scheme)
{
  if (m_scheme == scheme)
    return;
  m_scheme = scheme;
  applyGradient();
  refreshMap();
}

void HeatmapPlot::setCellCount(int nx, int ny)
{
  m_nx = qBound(2, nx, kMaxGridDimension);
  m_ny = qBound(2, ny, kMaxGridDimension);
  refreshMap();
}

void HeatmapPlot::addPoint(double x, double y, double z)
{
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
    return;
  m_samples.append({x, y, z});
  if (!hold())
    refreshMap();
}

void HeatmapPlot::clear()
{
  m_samples.clear();
  m_shown.clear();
  refreshMap();
}

void HeatmapPlot::setHold(bool hold)
{
  if (this->hold() == hold) return;
  QcpGraphCommon::setHold(hold);
  m_shown = hold ? m_samples.hold() : QVector<Sample>();
  refreshMap();
}

void HeatmapPlot::applyGradient()
{
  QCPColorGradient gradient;
  if (m_scheme == ColorMap::Ironbow)
  {
    gradient.setColorStopAt(0.00, QColor(0, 0, 0));
    gradient.setColorStopAt(0.17, QColor(32, 0, 120));
    gradient.setColorStopAt(0.38, QColor(140, 20, 140));
    gradient.setColorStopAt(0.55, QColor(210, 50, 60));
    gradient.setColorStopAt(0.72, QColor(250, 130, 10));
    gradient.setColorStopAt(0.88, QColor(255, 220, 60));
    gradient.setColorStopAt(1.00, QColor(255, 255, 230));
  }
  else
  {
    for (int i = 0; i <= 16; ++i)
    {
      const double t = double(i) / 16;
      gradient.setColorStopAt(t, ColorMap::color(m_scheme, t));
    }
  }
  gradient.setLevelCount(256);
  gradient.setNanHandling(QCPColorGradient::nhNanColor);
  gradient.setNanColor(ColorMap::noDataColor(m_scheme));
  m_map->setGradient(gradient);
  m_scale->setGradient(gradient);
}

void HeatmapPlot::refreshMap()
{
  const QVector<Sample> live = m_samples.values();
  const QVector<Sample> &shown = hold() ? m_shown : live;
  setNoDataVisible(shown.isEmpty());
  if (shown.isEmpty())
  {
    const PlotAxis::Range xAxis = PlotAxis::range(0, 0, m_xUnit);
    const PlotAxis::Range yAxis = PlotAxis::range(0, 0, m_yUnit);
    const PlotAxis::Range zAxis = PlotAxis::range(0, 0, m_zUnit);
    m_map->data()->setSize(m_nx, m_ny);
    m_map->data()->setRange(QCPRange(0, 1), QCPRange(0, 1));
    m_map->setDataRange(QCPRange(0, 1));
    for (int y = 0; y < m_ny; ++y)
      for (int x = 0; x < m_nx; ++x)
        m_map->data()->setCell(x, y, qQNaN());
    setAxisRange(m_plot->xAxis, xAxis);
    setAxisRange(m_plot->yAxis, yAxis);
    m_scale->axis()->setRange(0, 1);
    m_scale->setLabel(zAxis.title);
    m_plot->replot(QCustomPlot::rpQueuedReplot);
    return;
  }

  double xmin = shown.first().x, xmax = xmin;
  double ymin = shown.first().y, ymax = ymin;
  double zmin = shown.first().z, zmax = zmin;
  for (const Sample &sample : shown)
  {
    xmin = qMin(xmin, sample.x);
    xmax = qMax(xmax, sample.x);
    ymin = qMin(ymin, sample.y);
    ymax = qMax(ymax, sample.y);
    zmin = qMin(zmin, sample.z);
    zmax = qMax(zmax, sample.z);
  }

  const PlotAxis::Range xAxis = PlotAxis::range(xmin, xmax, m_xUnit);
  const PlotAxis::Range yAxis = PlotAxis::range(ymin, ymax, m_yUnit);
  const PlotAxis::Range zAxis = PlotAxis::range(zmin, zmax, m_zUnit);
  const QCPRange xRange(xAxis.minimum / xAxis.factor, xAxis.maximum / xAxis.factor);
  const QCPRange yRange(yAxis.minimum / yAxis.factor, yAxis.maximum / yAxis.factor);
  const QCPRange zRange(zAxis.minimum / zAxis.factor, zAxis.maximum / zAxis.factor);
  m_map->data()->setSize(m_nx, m_ny);
  m_map->data()->setRange(xRange, yRange);
  m_map->setDataRange(zRange);
  setAxisRange(m_plot->xAxis, xAxis);
  setAxisRange(m_plot->yAxis, yAxis);
  m_scale->axis()->setRange(zRange);
  m_scale->setLabel(zAxis.title);

  const int cellCount = m_nx * m_ny;
  QVector<double> sums(cellCount, 0.0);
  QVector<int> counts(cellCount, 0);
  const double dx = xmax - xmin;
  const double dy = ymax - ymin;
  for (const Sample &sample : shown)
  {
    const int ix = dx == 0 ? m_nx / 2 : qBound(0, int((sample.x - xmin) / dx * m_nx), m_nx - 1);
    const int iy = dy == 0 ? m_ny / 2 : qBound(0, int((sample.y - ymin) / dy * m_ny), m_ny - 1);
    const int i = iy * m_nx + ix;
    sums[i] += sample.z / zAxis.factor;
    ++counts[i];
  }
  for (int y = 0; y < m_ny; ++y)
    for (int x = 0; x < m_nx; ++x)
    {
      const int i = y * m_nx + x;
      m_map->data()->setCell(x, y, counts[i] ? sums[i] / counts[i] : qQNaN());
    }

  m_plot->replot(QCustomPlot::rpQueuedReplot);
}
