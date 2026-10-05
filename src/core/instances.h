// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

class DeviceLibrary;
class Settings;

/// The instances as a whole: their names and renaming one. An instance's id
/// is the name of its settings file and the variable name of its reading in
/// the formulas of calculated values.
namespace Instances
{
/// Letters, digits and underscores, not starting with a digit, and not
/// "default" (the instance without --config-id).
bool        isValidName(const QString &id);

/// @p keys with the variable @p from renamed to @p to in a formula
/// (DMM/calc-expression, the formula in "calc ..." of Port settings/device);
/// unchanged keys when there is none.
QVariantMap renamedInFormula(const QVariantMap &keys, const QString &from, const QString &to);

/// Renames the stopped instance @p from to @p to: its settings file, and the
/// variable in the formulas of all instances and of My devices. Returns the
/// instances whose formula changed; on failure nothing is changed and
/// @p error says why.
QStringList rename(Settings &settings, DeviceLibrary *library, const QString &from, const QString &to,
                   QString *error);
}
