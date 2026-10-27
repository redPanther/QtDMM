// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>

/// The limit test (PASS/FAIL): limits around a nominal value, either as
/// nominal ± percent, nominal ± an amount, or as lower and upper limit.
/// Each reading of the value gets a verdict, without duration or
/// hysteresis: a test, not an alarm. Kept as JSON in the settings
/// (Limits/test).
struct LimitTest
{
  enum Form { Percent, Amount, Range };
  enum Verdict { NoVerdict, Pass, High, Low };

  bool enabled = false;
  int valueId = 0;         ///< which value of the meter (Reading::id); the main one for now
  Form form = Percent;
  double nominal = 0;      ///< Percent, Amount; base units
  double tolerance = 1;    ///< Percent: percent, Amount: base units
  double lower = 0;        ///< Range; base units
  double upper = 0;
  bool tint = true;        ///< the display and the analog meter green or red
  QString highSound;       ///< Alarm::sound when the verdict turns FAIL HI; empty none
  QString lowSound;        ///< the same for FAIL LO
  bool highRepeat = false; ///< the sound again every second while the verdict stays HI
  bool lowRepeat = false;  ///< the same for LO
  QString passSound;       ///< the same for PASS
  bool passRepeat = false; ///< the same for PASS

  /// The limits in base units, whatever the form; the lower first.
  double low() const;
  double high() const;
  /// The value the deviation is counted from: the nominal, or the middle
  /// of a range.
  double centre() const;

  /// The verdict on @p value (base units); an overload counts as High, or
  /// Low with @p negative (-OL).
  Verdict judge(double value, bool overload, bool negative = false) const;
  /// (value - centre) / centre in percent; NaN for a centre of 0.
  double deviation(double value) const;

  /// "4.9V to 5.1V" and, for the other forms, how they were entered:
  /// "4.9V to 5.1V (5V ± 2 %)". @p unit is the base unit.
  QString describe(const QString &unit) const;
  /// "PASS", "FAIL HI", "FAIL LO" or empty, as the status line and the
  /// files name it.
  static QString verdictText(Verdict v);

  QJsonObject toJson() const;
  static LimitTest fromJson(const QJsonObject &o);
  /// From the settings' text; empty or junk is a test that is off.
  static LimitTest fromJsonText(const QString &json);
  QString toJsonText() const;
};
