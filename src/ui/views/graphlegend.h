// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QPen>
#include <QWidget>

/// The legend top left in the plot: a piece of each curve's line - its
/// colour and its style, so the values tell apart also without colour -
/// and its name, in a row.
class GraphLegend : public QWidget
{
public:
  struct Entry
  {
    QPen    pen;
    QString name;
  };

  explicit GraphLegend(QWidget *parent);

  /// The curves, and the colour of the names.
  void setEntries(const QList<Entry> &entries, const QColor &ink);
  /// The names, two spaces between them (for the tests).
  QString text() const;

  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent *) override;

private:
  static constexpr int kSample = 24;   ///< the piece of line
  static constexpr int kGap = 5;       ///< between it and the name
  static constexpr int kSpace = 14;    ///< between two entries

  QList<Entry> m_entries;
  QColor       m_ink;
};
