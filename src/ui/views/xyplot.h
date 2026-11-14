// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPointF>
#include <QBrush>
#include <QColor>
#include <QVector>
#include "ui/views/qcpgraphcommon.h"
#include "ui/views/plotsamples.h"

class QCPGraph;

/// A live scatter plot of one value against another.
class XYPlot : public QcpGraphCommon
{
  Q_OBJECT
public:
  explicit XYPlot(QWidget *parent = nullptr);
  void        setThemeColors(const QBrush &background, const QColor &grid, const QColor &labels, const QColor &data);
  void        setAxisUnits(const QString &x, const QString &y);
  void        setLegend(const QString &legend);
  void        addPoint(double x, double y);
  void        clear() override;

private:
  void refreshPlot();

  QCPGraph *m_graph = nullptr;
  RingBuffer<QPointF> m_points{2000};
  QVector<QPointF> m_shown;
  QString     m_xUnit;
  QString     m_yUnit;
};
