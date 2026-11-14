// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QBrush>
#include <QColor>
#include <QList>
class QCPAxis;
class QCustomPlot;

/// Shared color palette applied consistently to QCustomPlot axes and canvas.
class PlotTheme
{
public:
  QBrush background;
  QColor grid;
  QColor labels;
  QColor data;
  void applyTo(QCustomPlot *plot, const QList<QCPAxis *> &axes = {}) const;
};
