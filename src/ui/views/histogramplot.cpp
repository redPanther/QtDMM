// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/views/histogramplot.h"

#include "3rdparty/qcustomplot/qcustomplot.h"
#include "ui/views/plotaxis.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

HistogramPlot::HistogramPlot(QWidget *parent) :
  QcpGraphCommon(parent, QCoreApplication::translate("HistogramPlot", "No readings yet"))
{
  m_refreshTimer.setSingleShot(true);
  connect(&m_refreshTimer, &QTimer::timeout, this, &HistogramPlot::refreshPlot);
  m_plot->xAxis->setNumberFormat(QStringLiteral("gb"));
  m_plot->xAxis->setNumberPrecision(4);
  m_plot->yAxis->setNumberFormat(QStringLiteral("gb"));
  m_plot->yAxis->setNumberPrecision(3);
  m_bars = new QCPBars(m_plot->xAxis, m_plot->yAxis);
  m_bars->setAntialiased(false);
  refreshPlot();
}

void HistogramPlot::setThemeColors(const QBrush &background, const QColor &grid, const QColor &labels,
                                   const QColor &data)
{
  PlotTheme theme;
  theme.background = background.style() == Qt::NoBrush ? QBrush(palette().base()) : background;
  theme.grid = grid;
  theme.labels = labels;
  theme.data = data;
  setTheme(theme);
  m_bars->setPen(QPen(data.darker(115)));
  m_bars->setBrush(data);
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void HistogramPlot::setValueId(int id)
{
  if (m_valueId == id)
    return;
  m_valueId = id;
  clear();
}

void HistogramPlot::addReading(const Reading &reading)
{
  if (reading.id != m_valueId)
    return;
  if (reading.baseUnit != m_unit)
  {
    clear();
    m_unit = reading.baseUnit;
  }
  if (reading.noNumber() || reading.overload || reading.hold || !std::isfinite(reading.value))
    return;
  if (m_values.size() < kMaxValues)
    m_values.append(reading.value);
  else
  {
    m_values[m_valueHead] = reading.value;
    m_valueHead = (m_valueHead + 1) % kMaxValues;
  }
  if (!hold())
    scheduleRefresh();
}

void HistogramPlot::clear()
{
  m_refreshTimer.stop();
  m_values.clear();
  m_shown.clear();
  m_valueHead = 0;
  refreshPlot();
}

void HistogramPlot::setHold(bool hold)
{
  if (this->hold() == hold)
    return;
  m_refreshTimer.stop();
  QcpGraphCommon::setHold(hold);
  if (hold)
    m_shown = m_values;
  else
    m_shown.clear();
  refreshPlot();
}

void HistogramPlot::setCaption(const QString &caption)
{
  if (m_caption == caption)
    return;
  m_caption = caption;
  setTitle(caption);
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void HistogramPlot::setNotice(const QString &notice)
{
  if (m_notice == notice)
    return;
  m_notice = notice;
  setNoDataText(notice.isEmpty() ? tr("No readings yet") : notice);
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void HistogramPlot::scheduleRefresh()
{
  // Keep expensive data preparation and plotting to at most ~30 updates/second.
  if (!m_refreshTimer.isActive())
    m_refreshTimer.start(33);
}

void HistogramPlot::refreshPlot()
{
  const QVector<double> &shown = hold() ? m_shown : m_values;
  setNoDataVisible(shown.isEmpty());
  if (shown.isEmpty())
  {
    const PlotAxis::Range axis = PlotAxis::range(0, 0, m_unit);
    m_bars->setData(QVector<double>(), QVector<double>());
    setAxisRange(m_plot->xAxis, axis);
    m_plot->yAxis->setRange(0, 1);
    m_plot->yAxis->setLabel(tr("Count"));
    m_plot->replot(QCustomPlot::rpQueuedReplot);
    return;
  }
  double lo = shown.first(), hi = lo;
  for (double value : shown)
  {
    lo = qMin(lo, value);
    hi = qMax(hi, value);
  }
  const PlotAxis::Range axis = PlotAxis::range(lo, hi, m_unit);
  const int bins = qBound(5, int(std::ceil(std::sqrt(shown.size()))), 40);
  QVector<double> keys(bins), counts(bins, 0);
  const double step = (axis.maximum - axis.minimum) / bins;
  for (int bin = 0; bin < bins; ++bin)
    keys[bin] = axis.minimum / axis.factor + (bin + 0.5) * step / axis.factor;
  for (double value : shown)
  {
    const int bin = qBound(0, int((value - axis.minimum) / (axis.maximum - axis.minimum) * bins), bins - 1);
    ++counts[bin];
  }
  m_bars->setWidth(step / axis.factor * 0.94);
  m_bars->setData(keys, counts);
  setAxisRange(m_plot->xAxis, axis);
  m_plot->yAxis->setRange(0, qMax(1.0, *std::max_element(counts.cbegin(), counts.cend()) * 1.12));
  m_plot->yAxis->setLabel(tr("Count"));
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}
