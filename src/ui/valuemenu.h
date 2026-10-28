// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

#include <QCoreApplication>

#include "core/valueref.h"

class QMenu;
class FrontPanel;
class InstanceWidget;

/// The context menu of one value of an open meter (a value row of the
/// sidebar, or the row of a meter with one value): which window shows it,
/// and where. One entry per kind of view - LCD as the main value or on the
/// left or right of the small line of the meter's front panel (a choice
/// takes the place, the value there swaps with it), Analog meter (one
/// value), Graph shown or not and on which axis, Poincaré plot (one value),
/// Readings table. A choice opens the window it is for.
///
/// The window's meter's values go to InstanceWidget, another meter's to its
/// FrontPanel and to the views by their ValueRef.
class ValueMenu
{
  Q_DECLARE_TR_FUNCTIONS(ValueMenu)
public:
  enum class View { Lcd, Meter, Poincare, Graph, Table };

  /// What the menu acts on.
  struct Target
  {
    InstanceWidget *wid = nullptr;
    /// The front panel of another meter by its source, nullptr when none.
    std::function<FrontPanel *(const QString &source)> panel;
    /// The readings table shows a value, and showing it.
    std::function<bool(const ValueRef &)> tableShown;
    std::function<void(const ValueRef &, bool)> setTableShown;
    /// Opens the window of a view; for the LCD the meter's front panel (@p source).
    std::function<void(View, const QString &source)> show;
    /// The rules on a value: a new alarm on it, the PASS/FAIL test's limits
    /// for it (at the end of the menu, when given).
    std::function<void(const ValueRef &)> addAlarm;
    std::function<void(const ValueRef &)> editLimits;
  };

  /// Fills @p menu for @p value of an open meter.
  static void fill(QMenu *menu, const Target &target, const ValueRef &value);
};
