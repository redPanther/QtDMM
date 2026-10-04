// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
// The min/max memory of the main value: kept across range changes, afresh
// on another port or another unit at the same port (MinMaxMemory).
#include <QDebug>
#include <cmath>

#include "core/minmaxmemory.h"

static int failed = 0;

static void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAILED:" << what;
    failed++;
  }
}

static Reading reading(double value, const char *port, const char *baseUnit, bool overload = false)
{
  Reading r;
  r.value = overload ? std::nan("") : value;
  r.port = PortKey::fromString(port);
  r.baseUnit = baseUnit;
  r.overload = overload;
  return r;
}

int main()
{
  MinMaxMemory m;
  MinMaxMemory::Result res = m.feed(reading(0.22118, "voltage.dc", "V"));   // 221.18 mV
  check(!res.reset && res.newMin && res.newMax, "the first value is min and max");
  // the range goes to V: the same port, the memory stays
  res = m.feed(reading(1.5, "voltage.dc", "V"));
  check(!res.reset && res.newMax && !res.newMin, "mV -> V keeps min/max");
  check(m.minimum() == 0.22118 && m.maximum() == 1.5, "min 221.18 mV, max 1.5 V");
  // an overload says nothing about the function, and is no extreme
  res = m.feed(reading(0, "voltage.dc", "V", true));
  check(!res.reset && !res.newMin && !res.newMax, "OL keeps min/max");
  check(m.minimum() == 0.22118 && m.maximum() == 1.5, "min/max unchanged after OL");
  // a value without a known quantity neither
  res = m.feed(reading(0.5, "", ""));
  check(!res.reset, "unknown quantity keeps min/max");
  // another coupling at the meter
  res = m.feed(reading(0.9, "voltage.ac", "V"));
  check(res.reset && res.newMin && res.newMax && m.minimum() == 0.9, "DC -> AC starts afresh");
  // another function
  res = m.feed(reading(4700, "resistance", "Ohm"));
  check(res.reset && m.minimum() == 4700 && m.maximum() == 4700, "V -> Ω starts afresh");
  // the same port, another unit
  m.feed(reading(21.5, "temperature", "C"));
  res = m.feed(reading(70.7, "temperature", "dF"));
  check(res.reset && m.minimum() == 70.7, "°C -> °F starts afresh");
  // the Reset key
  m.clear();
  check(m.minimum() > 1e19 && m.maximum() < -1e19, "clear empties the memory");
  res = m.feed(reading(3.0, "current.dc", "A"));
  check(!res.reset && res.newMin && res.newMax, "after clear the next value is no port change");

  if (failed)
    qWarning() << failed << "check(s) failed";
  else
    qInfo() << "all checks passed";
  return failed ? 1 : 0;
}
