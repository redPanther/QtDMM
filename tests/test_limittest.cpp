// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// LimitTest: the limits of each form, the verdict (OL, -OL, no number), the
// deviation, the description and the JSON round trip.

#include <QtCore>
#include <cmath>

#include "core/limittest.h"
#include "testutil.h"

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);

  // --- 1. the limits of each form ---
  LimitTest pct;
  pct.nominal = 5;
  pct.tolerance = 2;
  check(qFuzzyCompare(pct.low(), 4.9) && qFuzzyCompare(pct.high(), 5.1), "percent: 5 ± 2 % is 4.9 to 5.1");
  LimitTest neg = pct;
  neg.nominal = -5;
  check(qFuzzyCompare(neg.low(), -5.1) && qFuzzyCompare(neg.high(), -4.9), "percent of a negative nominal: lower first");
  LimitTest amt;
  amt.form = LimitTest::Amount;
  amt.nominal = 2;
  amt.tolerance = -0.05;
  check(qFuzzyCompare(amt.low(), 1.95) && qFuzzyCompare(amt.high(), 2.05), "amount: the sign of the tolerance does not matter");
  LimitTest rng;
  rng.form = LimitTest::Range;
  rng.lower = 12;
  rng.upper = 10;
  check(rng.low() == 10 && rng.high() == 12 && rng.centre() == 11, "range: either order, centre in the middle");

  // --- 2. the verdict ---
  check(pct.judge(5.0, false) == LimitTest::Pass && pct.judge(4.9, false) == LimitTest::Pass
        && pct.judge(5.1, false) == LimitTest::Pass, "the limits themselves pass");
  check(pct.judge(5.11, false) == LimitTest::High && pct.judge(4.89, false) == LimitTest::Low, "beyond: HI and LO");
  check(pct.judge(0, true) == LimitTest::High && pct.judge(0, true, true) == LimitTest::Low, "OL is HI, -OL is LO");
  check(pct.judge(std::nan(""), false) == LimitTest::NoVerdict, "no number, no verdict");

  // --- 3. the deviation ---
  check(qFuzzyCompare(pct.deviation(5.1), 2.0), "deviation +2 %");
  check(qFuzzyCompare(neg.deviation(-5.1), -2.0), "deviation of a negative nominal: below is minus");
  check(qFuzzyCompare(rng.deviation(11.11), 1.0), "deviation of a range: from its middle");
  LimitTest zero;
  zero.form = LimitTest::Amount;
  zero.tolerance = 0.01;
  check(std::isnan(zero.deviation(0.005)), "nominal 0: no deviation in percent");

  // --- 4. words ---
  check(pct.describe("V") == QString("4.9V to 5.1V (5V ± 2 %)"), "describe percent: " + pct.describe("V"));
  check(rng.describe("Ohm") == QString("10Ohm to 12Ohm"), "describe range: " + rng.describe("Ohm"));
  check(LimitTest::verdictText(LimitTest::High) == "FAIL HI" && LimitTest::verdictText(LimitTest::NoVerdict).isEmpty(),
        "verdict texts");

  // --- 5. JSON ---
  LimitTest t = amt;
  t.enabled = true;
  t.tint = false;
  t.highSound = "alarm_crit";
  t.lowSound = "alarm_low";
  t.lowRepeat = true;
  t.passSound = "beep";
  const LimitTest back = LimitTest::fromJsonText(t.toJsonText());
  check(back.enabled && back.form == LimitTest::Amount && back.nominal == 2 && back.tolerance == -0.05 && !back.tint
        && back.highSound == "alarm_crit" && back.lowSound == "alarm_low" && !back.highRepeat && back.lowRepeat
        && back.passSound == "beep" && back.valueId == 0, "JSON round trip");
  const LimitTest none = LimitTest::fromJsonText(QString());
  check(!none.enabled && none.tint && none.form == LimitTest::Percent, "no settings: off, coloured, percent");

  return failed == 0 ? 0 : 1;
}
