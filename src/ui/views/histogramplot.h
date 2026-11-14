// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QBrush>
#include <QColor>
#include <QTimer>
#include <QVector>

#include "core/reading.h"
#include "ui/views/qcpgraphcommon.h"

class QCPBars;

/// A live histogram of the selected value's recent readings.
class HistogramPlot : public QcpGraphCommon
{
  Q_OBJECT
public:
  explicit HistogramPlot(QWidget *parent = nullptr);
  void        setThemeColors(const QBrush &background, const QColor &grid, const QColor &labels, const QColor &data);
  void        setValueId(int id);
  void        setHold(bool hold) override;
  void        clear() override;

public Q_SLOTS:
  void        addReading(const Reading &reading);
  void        setCaption(const QString &caption);
  void        setNotice(const QString &notice);

private:
  static constexpr int kMaxValues = 1000;
  void scheduleRefresh();
  void refreshPlot();

  QTimer          m_refreshTimer;
  QVector<double> m_values;
  QVector<double> m_shown;
  int             m_valueHead = 0;
  int             m_valueId = 0;
  QString         m_caption;
  QString         m_notice;
  QString         m_unit;
  QCPBars        *m_bars = nullptr;
};
