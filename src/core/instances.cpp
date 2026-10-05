// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/instances.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include "core/calcexpr.h"
#include "core/devicelibrary.h"
#include "core/settings.h"

namespace
{
const char *kExpression = "DMM/calc-expression";
const char *kDevice = "Port settings/device";
}

bool Instances::isValidName(const QString &id)
{
  static const QRegularExpression identifier("^[A-Za-z_][A-Za-z0-9_]*$");
  return id != QLatin1String("default") && identifier.match(id).hasMatch();
}

QVariantMap Instances::renamedInFormula(const QVariantMap &keys, const QString &from, const QString &to)
{
  QVariantMap renamed = keys;
  if (keys.contains(kExpression))
    renamed.insert(kExpression, CalcExpr::renameVariable(keys.value(kExpression).toString(), from, to));
  // "calc <unit> <formula>": the unit stays
  const QString device = keys.value(kDevice).toString();
  if (device.startsWith(QLatin1String("calc ")) && device.count(' ') >= 2)
    renamed.insert(kDevice, device.section(' ', 0, 1) + ' '
                              + CalcExpr::renameVariable(device.section(' ', 2), from, to));
  return renamed;
}

QStringList Instances::rename(Settings &settings, DeviceLibrary *library, const QString &from, const QString &to,
                              QString *error)
{
  const auto fail = [error](const char *text)
  {
    if (error)
      *error = QCoreApplication::translate("Instances", text);
    return QStringList();
  };
  if (!isValidName(to))
    return fail(QT_TRANSLATE_NOOP("Instances", "Instance names may contain letters, digits and underscores and must not start with a digit."));
  if (settings.getConfigInstances().contains(to))
    return fail(QT_TRANSLATE_NOOP("Instances", "Instance already exists."));
  if (!settings.renameConfig(from, to))
    return fail(QT_TRANSLATE_NOOP("Instances", "The settings file could not be renamed."));

  QStringList changed;
  for (const QString &id : settings.getConfigInstances())
  {
    Settings other(id == QLatin1String("default") ? QString() : id, settings.configDir());
    const QVariantMap keys { { kExpression, other.getString(kExpression) }, { kDevice, other.getString(kDevice) } };
    const QVariantMap renamed = renamedInFormula(keys, from, to);
    if (renamed == keys)
      continue;
    for (auto it = renamed.constBegin(); it != renamed.constEnd(); ++it)
      if (!it.value().toString().isEmpty())
        other.setString(it.key(), it.value().toString());
    other.save();
    changed << id;
  }
  if (library)
    for (const MyDevice &d : library->list())
    {
      const QVariantMap renamed = renamedInFormula(d.keys, from, to);
      if (renamed != d.keys)
        library->update(d.id, renamed);
    }
  return changed;
}
