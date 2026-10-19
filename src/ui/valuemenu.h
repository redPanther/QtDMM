// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

#include <QCoreApplication>

class QMenu;
class InstanceWidget;

/// The context menu of one of the meter's values (a value row of the
/// sidebar): which window shows it, and where. One entry per kind of view -
/// LCD as the main value or on the small line (at most two, a third
/// replaces the older), Analog meter (one value), Graph shown or not and on
/// which axis, Poincaré plot (one value). A choice opens the window it is for.
///
/// It knows only the instance's choice of values (InstanceWidget) and asks
/// the caller to open a window: with more meters (27.1) the same menu goes
/// below a device of My devices, with a choice of window.
class ValueMenu
{
  Q_DECLARE_TR_FUNCTIONS(ValueMenu)
public:
  enum class View { Lcd, Meter, Poincare, Graph };

  /// Fills @p menu for value @p id (Reading::id) of @p wid; @p show opens the
  /// window of a view.
  static void fill(QMenu *menu, InstanceWidget *wid, int id, const std::function<void(View)> &show);
};
