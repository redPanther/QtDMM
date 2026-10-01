// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>

#include "reading.h"

/// @file
/// The first of the core's base types (quality and flags of a measured
/// value), in the vocabulary of libsigrok. Only QtCore: the recorder store
/// uses them, and the core built on them later runs without widgets.

/// How good a stored value is.
enum class Quality : quint8
{
  Valid,      ///< a number the meter showed
  Stale,      ///< no reading arrived for longer than the timeout
  Overload,   ///< the meter showed OL (or the like); the value is not a measurement
  Inactive    ///< the function is not being measured right now
};

/// State and mode of a value, a bit mask after libsigrok's sr_mqflag, plus
/// LowBattery and Peak, which QtDMM meters report and sigrok does not have.
namespace SampleFlag
{
enum : quint32
{
  AC        = 1u << 0,
  DC        = 1u << 1,
  RMS       = 1u << 2,
  Diode     = 1u << 3,
  Hold      = 1u << 4,
  Max       = 1u << 5,
  Min       = 1u << 6,
  Autorange = 1u << 7,
  Relative  = 1u << 8,
  // 1u << 9 .. 1u << 16: the sound level weightings, not used by QtDMM
  Duration  = 1u << 17,
  Avg       = 1u << 18,
  Reference = 1u << 19,
  Unstable  = 1u << 20,
  FourWire  = 1u << 21,
  LowBattery = 1u << 28,   ///< QtDMM
  Peak       = 1u << 29    ///< QtDMM
};
}

/// The flags of @p reading. The decoders still hand over their mode as a
/// string (Reading::special); this is the one table from it to flags until
/// the adapter behind the decoder takes that over.
inline quint32 sampleFlags(const Reading &reading)
{
  quint32 flags = 0;
  if (reading.special == QLatin1String("DC"))
    flags |= SampleFlag::DC;
  else if (reading.special == QLatin1String("AC"))
    flags |= SampleFlag::AC;
  else if (reading.special == QLatin1String("ACDC"))
    flags |= SampleFlag::AC | SampleFlag::DC;
  else if (reading.special == QLatin1String("DI"))
    flags |= SampleFlag::Diode;
  if (reading.hold)
    flags |= SampleFlag::Hold;
  if (reading.range == QLatin1String("AUTO"))
    flags |= SampleFlag::Autorange;
  return flags;
}
