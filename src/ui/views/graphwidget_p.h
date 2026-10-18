// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Helpers that more than one part of GraphWidget needs (graphwidget*.cpp);
// not part of its interface.

#pragma once

#include <QString>

#include "core/sampletypes.h"
#include "core/siprefix.h"
#include "recording/recordingseries.h"

/// "1:05:09", "5:09", "2 d 1:05:09"
inline QString durationText(qint64 seconds)
{
  const qint64 d = seconds / 86400, h = seconds / 3600 % 24, m = seconds / 60 % 60, sec = seconds % 60;
  QString text = h || d ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'))
                        : QString("%1:%2").arg(m).arg(sec, 2, 10, QChar('0'));
  return d ? QString("%1 d %2").arg(d).arg(text) : text;
}

/// What a curve shows: "Temperature (°C)", or the unit alone
inline QString curveName(const RecordingSeries &s, const QString &unit)
{
  const QString u = SiPrefix::displayText(unit);
  if (s.port.quantity == Quantity::Unknown)
    return u;
  QString text = Quantities::name(s.port.quantity);
  if (s.port.defining & (SampleFlag::AC | SampleFlag::DC))
    text += ' ' + couplingText(s.port.defining);
  return u.isEmpty() ? text : QString("%1 (%2)").arg(text, u);
}

/// The auto scale takes @p val in, with a margin; true when it grew.
inline bool growScale(double val, double &min, double &max)
{
  bool ret = false;

  if (val > max * 0.95)
  {
    if (val > 0)
      max = val * 1.2;
    else
      max = val / 1.2;

    ret = true;
  }
  if (val < min * 0.95)
  {
    if (val > 0)
      min = val / 1.2;
    else
      min = val * 1.2;

    ret = true;
  }

  return ret;
}
