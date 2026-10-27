// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/limittest.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <cmath>

#include "ui/engnumbervalidator.h"

namespace
{
QString eng(double v, const QString &unit)
{
  return EngNumberValidator::engText(v) + unit;
}
}

double LimitTest::low() const
{
  switch (form)
  {
    case Percent: return qMin(nominal * (1 - tolerance / 100), nominal * (1 + tolerance / 100));
    case Amount:  return nominal - qAbs(tolerance);
    case Range:   return qMin(lower, upper);
  }
  return lower;
}

double LimitTest::high() const
{
  switch (form)
  {
    case Percent: return qMax(nominal * (1 - tolerance / 100), nominal * (1 + tolerance / 100));
    case Amount:  return nominal + qAbs(tolerance);
    case Range:   return qMax(lower, upper);
  }
  return upper;
}

double LimitTest::centre() const
{
  return form == Range ? (lower + upper) / 2 : nominal;
}

LimitTest::Verdict LimitTest::judge(double value, bool overload, bool negative) const
{
  if (overload)
    return negative ? Low : High;
  if (std::isnan(value))
    return NoVerdict;
  if (value > high())
    return High;
  if (value < low())
    return Low;
  return Pass;
}

double LimitTest::deviation(double value) const
{
  const double c = centre();
  return c == 0 ? std::nan("") : (value - c) / qAbs(c) * 100;
}

QString LimitTest::describe(const QString &unit) const
{
  const QString range = QCoreApplication::translate("LimitTest", "%1 to %2").arg(eng(low(), unit), eng(high(), unit));
  switch (form)
  {
    case Percent: return QString("%1 (%2 ± %3 %)").arg(range, eng(nominal, unit), EngNumberValidator::engText(tolerance));
    case Amount:  return QString("%1 (%2 ± %3)").arg(range, eng(nominal, unit), eng(qAbs(tolerance), unit));
    case Range:   break;
  }
  return range;
}

QString LimitTest::verdictText(Verdict v)
{
  switch (v)
  {
    case Pass: return QStringLiteral("PASS");
    case High: return QStringLiteral("FAIL HI");
    case Low:  return QStringLiteral("FAIL LO");
    case NoVerdict: break;
  }
  return QString();
}

QJsonObject LimitTest::toJson() const
{
  QJsonObject o;
  o["enabled"] = enabled;
  if (valueId != 0)
    o["value"] = valueId;
  o["form"] = form == Amount ? "amount" : form == Range ? "range" : "percent";
  o["nominal"] = nominal;
  o["tolerance"] = tolerance;
  o["lower"] = lower;
  o["upper"] = upper;
  o["tint"] = tint;
  o["fail-sound"] = failSound;
  o["pass-sound"] = passSound;
  return o;
}

LimitTest LimitTest::fromJson(const QJsonObject &o)
{
  LimitTest t;
  t.enabled = o["enabled"].toBool(false);
  t.valueId = qBound(0, o["value"].toInt(0), 3);
  const QString form = o["form"].toString();
  t.form = form == "amount" ? Amount : form == "range" ? Range : Percent;
  t.nominal = o["nominal"].toDouble();
  t.tolerance = o["tolerance"].toDouble(1);
  t.lower = o["lower"].toDouble();
  t.upper = o["upper"].toDouble();
  t.tint = o["tint"].toBool(true);
  t.failSound = o["fail-sound"].toString();
  t.passSound = o["pass-sound"].toString();
  return t;
}

LimitTest LimitTest::fromJsonText(const QString &json)
{
  return fromJson(QJsonDocument::fromJson(json.toUtf8()).object());
}

QString LimitTest::toJsonText() const
{
  return QString::fromUtf8(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
}
