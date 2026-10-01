// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMetaType>
#include <QString>

#include "sampletypes.h"

/// One reading as the views get it from the MeterController: made once,
/// right behind the decoder, from what DMM::value() delivers.
///
/// Everything the views used to derive on their own from the display
/// strings - overload from letters in the text, the unit's SI prefix split
/// off - is worked out here once. The decoder interface is unchanged.
struct Reading
{
  double  value = 0;       ///< SI base units ("0.0123" for 12.3 mV)
  QString text;            ///< as the meter shows it: "12.30", "OL", "-OL"
  QString unit;            ///< as shown, prefix included: "mV", "kOhm"
  QString prefix;          ///< the SI prefix of the unit: "m", "k", ""
  QString baseUnit;        ///< the unit without prefix: "V", "Ohm"
  QString special;         ///< mode code from the decoder: "DC", "AC", "ACDC", "DI", "BUZ", ...
  QString range;           ///< "AUTO", "MANU" or the meter's range text
  bool    hold = false;    ///< the display is frozen (HOLD): an old value again
  bool    showBar = false; ///< the decoder has a bar graph value
  bool    overload = false;///< no number: OL, EFLO and the like
  int     id = 0;          ///< 0 = main value, 1..3 = secondary values
  qint64  msecs = 0;       ///< when it arrived (ms since the epoch)

  /// A temperature has no measuring range: the display count says nothing
  /// about its scale. "F" alone is farad, so only the decoders' "TE" mode
  /// and the temperature spellings of the unit count.
  bool temperature() const
  {
    return special == QLatin1String("TE") || baseUnit == QLatin1String("C") || baseUnit == QLatin1String("dF")
           || baseUnit == QStringLiteral("°C") || baseUnit == QStringLiteral("°F");
  }
};

Q_DECLARE_METATYPE(Reading)

/// The flags of @p reading, until Reading carries them itself (the step
/// after ReadingAdapter).
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
