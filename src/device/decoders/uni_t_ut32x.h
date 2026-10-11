// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device/dmmdecoder.h"

/// UNI-T UT325/UT321 (and the UT32x family, Tenma 72-7712/72-7715) thermometers
/// over their USB cable, a Hoitek HE2325U (CH9325 kind of HID chip) at
/// 2400 8N1. The meter streams the reading of its large display once it got
/// the command 0x01 ("live"), which pollRequest() sends.
///
/// 19 bytes, ending CR LF: @0 the decimal point, '2' one decimal, '0' none;
/// @1..@4 the digits as ASCII, ':' for a blank and ';' for the minus sign
/// (";;;;": no value - no sensor); @5 the unit, '1' °C, '2' °F, '3' K ('0'
/// a stored value); @6..@7 the memory number; @8 the thermocouple type,
/// '0'..'6' = K J T E N R S; @9..@12 a seconds/minutes clock; @13 which
/// sensor the large display shows, '0' T1, '1' T2, '2'/'3' T1-T2; @16 '1'.
/// Hold, MAX/MIN/AVG are not in the frame. Until QtDMM reads several
/// channels (27.2) only the large display.
///
/// Sources: docs/protocols/sources/UT325.log and UT321.log (frames of real
/// meters, heha's byte table), libsigrok uni-t-ut32x (GPL-3.0-or-later; it
/// takes only type K and always one decimal).
class DecoderUniTUT32x : public DmmDecoder
{
  Q_OBJECT
public:
  DecoderUniTUT32x(FrameFormat::DataFormat df) : DmmDecoder(df) { m_name = "UT32x"; }

  std::optional<DmmDecoder::DmmResponse> decode(const QByteArray &data, int id) override;
  QByteArray pollRequest() const override { return QByteArray(1, '\x01'); }
  bool checkFormat(const char *data, size_t idx) override;
  size_t getPacketLength() override { return 19; }
};
