// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QBrush>
#include <QColor>
#include <QString>

/// The colour designs of the main window: the system's look, "Silver"
/// (polished aluminium, gradients only) and "Dark".
///
/// A design is more than a palette: Fusion ignores gradient brushes in the
/// palette on most widgets, so the gradients come from a style sheet
/// (toolbars, status bar) and from the views that paint them themselves
/// (ViewFrame, the MDI area background, the graph). The LCD tint and the
/// analog meter's style are settings of their own and stay as they are.
namespace Designs
{
  enum Design { System, Silver, Dark };

  /// "system", "silver", "dark" - the settings value.
  QString name(Design d);
  Design fromName(const QString &name);

  /// Sets the application palette, style and style sheet. The first call
  /// remembers the system's palette and style for System.
  void apply(Design d);
  Design current();

  /// Background of the MDI area.
  QBrush areaBrush(Design d);
  /// Background of a view frame (display, meter, table); Qt::NoBrush = the
  /// palette's window colour.
  QBrush frameBrush(Design d);

  /// Colours the graph uses in design @p d (System: invalid = keep the
  /// colours from the settings).
  struct GraphColors
  {
    QBrush background;   ///< chart background
    QColor grid;
    QColor labels;       ///< axis labels and titles
  };
  GraphColors graphColors(Design d);
}
