// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>

class QWidget;

/// Finds Alt letters (the "&" mnemonics) used twice where they can be
/// pressed at the same time: in one menu, or among the widgets shown
/// together in a window - each page of a settings dialog on its own, and
/// the meter page once per kind of port, as its groups replace each other.
/// Qt does not warn about them; a doubled letter only moves the focus
/// between the entries instead of triggering one.
///
/// Also reports two actions with the same key sequence in one window.
///
/// Run by "qtdmm --check-mnemonics" (a test, in every language); it shows
/// and switches widgets, so the window is of no further use afterwards.
namespace MnemonicCheck
{
/// One line per conflict, empty when there is none. Builds the lazily
/// created main menu first by triggering "action_Menu".
QStringList run(QWidget *mainWindow);
}
