// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMetaType>
#include <QString>

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
};

Q_DECLARE_METATYPE(Reading)
