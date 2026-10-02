// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QPen>
#include <QPointF>

class QAbstractAxis;
class QChart;
class QLineSeries;

/// A line with gaps: Qt Charts draws a line across a NaN point as if it were
/// not there, so a gap (overload, no value) splits the line into segments,
/// one QLineSeries each.
///
/// The points come as for one series; a point with a NaN y is the gap. The
/// first segment is the series the chart maps positions with; the others
/// are kept in a pool and reused, so a gap that comes and goes does not
/// grow the chart.
class GapLine
{
public:
  /// @p first is in the chart and on its axes already.
  explicit GapLine(QLineSeries *first);

  QLineSeries *first() const { return m_segments.first(); }

  void setPen(const QPen &pen);
  void setVisible(bool on);

  /// All points anew.
  void replace(const QList<QPointF> &points);
  /// Points after the last ones.
  void append(const QList<QPointF> &points);
  /// Takes back the last @p count points given to append() or replace()
  /// (gap points included); at most the last kUndo.
  void removeLast(int count);
  void clear();

  /// Segments that have points (for the tests).
  int segments() const;

  static constexpr int kUndo = 8;

private:
  struct Step
  {
    bool gap = false;         ///< the point was a gap
    bool newSegment = false;  ///< the point opened a segment
    bool pendingBefore = false;
  };

  QLineSeries *segment(int i);
  void         add(const QPointF &p);

  QList<QLineSeries *> m_segments;
  int          m_used = 1;          ///< segments in use; the last is the open one
  bool         m_pending = false;   ///< a gap came: the next point opens a segment
  QList<Step>  m_steps;             ///< the last points, for removeLast()
  QPen         m_pen;
  bool         m_visible = true;
};
