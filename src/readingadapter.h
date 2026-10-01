// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>

#include "dmmdecoder.h"
#include "readevent.h"
#include "reading.h"
#include "sampletypes.h"

/// One value of a decoder frame, made uniform: the port it comes from, the
/// port's unit and the sample. What the decoder said is kept beside it for
/// the views that show it as before (the unit with the decoder's spelling).
struct PortSample
{
  int     id = 0;           ///< the decoder's value id: 0 the main display, 1..3 the others
  PortKey port;
  Unit    unit;             ///< the port's unit in one spelling: "Ω", "°C"
  Sample  sample;
  QString decoderUnit;      ///< the decoder's unit with prefix: "kOhm", "mV"
  QString decoderBaseUnit;  ///< the same without prefix: "Ohm", "V"
  QString rangeText;        ///< "AUTO", "MANU" or empty, as the decoder said
  bool    showBar = false;  ///< the views draw a bar graph: the decoder has one, and the quantity a range
};

/// The one place that interprets what the decoders deliver.
///
/// The decoders describe a value with strings: a mode code ("DC", "DI",
/// "TE", "OH", ...), a unit with prefix in their own spelling ("kOhm",
/// "dF", "uA"), the range as "AUTO"/"MANU" and an overload as letters in
/// the value text. The adapter turns that into the core's types - quantity,
/// flags, unit, range, quality - so nobody behind it has to read those
/// strings again (kern_spezifikation §2.1, §5.4). The decoders stay as they
/// are.
class ReadingAdapter
{
public:
  explicit ReadingAdapter(ReadEvent::DataFormat format = ReadEvent::Invalid) : m_format(format) {}

  /// The protocol: a few units mean something else on some meters (a
  /// Victron's "%" is the battery's state of charge, not a duty cycle).
  void setFormat(ReadEvent::DataFormat format) { m_format = format; }
  /// The meter's display count (DMMInfo::display), for Range::full and the
  /// bar graph; 0 when not known.
  void setCounts(int counts) { m_counts = counts; }

  /// The values of one decoded frame: the main one and, when the frame has
  /// one, the second. A second value that is empty (the meter's secondary
  /// display is off) comes along with an empty text, so the views can clear
  /// it.
  QList<PortSample> adapt(const DmmDecoder::DmmResponse &response, qint64 wall);

  /// One value, the fields as DMM::value() delivers them.
  PortSample adaptValue(double dval, const QString &text, const QString &unit, const QString &special,
                        const QString &range, bool hold, bool showBar, bool lowBattery, int id, qint64 wall);

  /// The Reading the views get for @p ps (kern_spezifikation §10: the
  /// facade until the views take Samples).
  static Reading reading(const PortSample &ps);

private:
  ReadEvent::DataFormat m_format;
  int                   m_counts = 0;
  PortKey               m_mainKey;   ///< the last main value's port, for the slot of the others
};
