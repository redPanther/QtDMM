// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QFont>
#include <QGraphicsItem>
#include <QList>
#include <functional>

/// Flags under the plot: a tag in the row of the time labels, its point up
/// at a vertical line - the time cursors A and B, later other lines with a
/// name (marks). The idea after dmm-tools' marker flags; no code taken.
namespace GraphFlags
{
/// One flag: the line's x, its name and a note; layout() fills in text
/// and rect.
struct Flag
{
  double  x = 0;
  QString label;   ///< always shown ("A", "3")
  QString note;    ///< after the label as room allows ("3 · Above 5 V", "3 · Abo…")
  QRectF  rect;    ///< the tag, below the plot; empty when left out
  QString text;    ///< what the tag says
};

/// Air between two tags and the space either side of the text.
constexpr double kAir = 3;
constexpr double kPad = 4;
/// Height of the point between the plot's edge and the tag.
constexpr double kPoint = 4;
/// The widest a tag gets with its note.
constexpr double kMaxWidth = 160;

/// Places @p flags (ascending x) under @p plot, each centred on its line.
/// A note gets no more than the room to the nearer neighbouring line (and
/// kMaxWidth), cut short with "…" or left off; the label always stays.
/// One that would touch the one before slides right, near an edge a tag
/// slides inward; one whose line would no longer leave from its tag is
/// left out (rect empty) - its line still shows. @p widthOf measures a
/// label.
void layout(QList<Flag> &flags, const QRectF &plot, double height, const std::function<double(const QString &)> &widthOf);
}

/// One flag in the chart's scene: the tag and its point, filled in the
/// line's colour, the text in black or white, whichever reads.
class GraphFlagItem : public QGraphicsItem
{
public:
  explicit GraphFlagItem(QGraphicsItem *parent = nullptr);

  /// The tag @p rect and the x of the line its point goes up to.
  void setPlace(const QRectF &rect, double x);
  void setText(const QString &text);
  void setColor(const QColor &color);
  void setFont(const QFont &font);
  const QFont &font() const { return m_font; }
  QString text() const { return m_text; }
  QRectF tagRect() const { return m_rect; }

  QRectF boundingRect() const override;
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
  QRectF  m_rect;
  double  m_x = 0;
  QString m_text;
  QColor  m_color = Qt::black;
  QFont   m_font;
};
