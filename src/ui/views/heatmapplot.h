// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/views/colormap.h"
#include "ui/views/qcpgraphcommon.h"
#include "ui/views/plotsamples.h"

#include <QBrush>
#include <QColor>
#include <QString>
#include <QVector>

class QCPColorMap;
class QCPColorScale;

/// A live heatmap drawn with QCustomPlot: X/Y position, colour = the third
/// value, averaged per cell.
class HeatmapPlot : public QcpGraphCommon
{
  Q_OBJECT
public:
  explicit HeatmapPlot(QWidget *parent = nullptr);

  void setThemeColors(const QBrush &background, const QColor &grid, const QColor &labels);
  void setAxisUnits(const QString &x, const QString &y, const QString &z);
  void setLegend(const QString &legend);
  void setColorScheme(ColorMap::Scheme scheme);
  ColorMap::Scheme colorScheme() const { return m_scheme; }
  void setCellCount(int nx, int ny);   // grid resolution, default 32 x 32
  void addPoint(double x, double y, double z);
  void clear() override;

private:
  struct Sample { double x, y, z; };
  void refreshMap();
  void applyGradient();

  QCPColorMap *m_map = nullptr;
  QCPColorScale *m_scale = nullptr;
  RingBuffer<Sample> m_samples{5000};
  QVector<Sample> m_shown;
  int m_nx = 32, m_ny = 32;

  ColorMap::Scheme m_scheme = ColorMap::Rainbow;
  QString m_xUnit, m_yUnit, m_zUnit;
};
