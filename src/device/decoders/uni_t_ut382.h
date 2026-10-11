// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device/dmmdecoder.h"

/// UNI-T UT382 luxmeter over its USB cable (a serial port, 19200 8N1). Once
/// "USB.1" is set in its menu (it starts with USB.0, nothing sent, after
/// every power-on) the meter streams about eight frames a second of 33
/// bytes: 30 bytes 0x3?, whose low nibbles in pairs (the first one low) give
/// 15 data bytes, then CR LF and one byte that may be a checksum.
///
/// Data bytes: @0 a menu on the display (0x20 USB, 0x30 APO, 0x40 REC, 0x50
/// code, 0x60 def; no reading then); @1..@4 the large display, ones to
/// thousands, as 7-segment codes, bit 7 of @2/@3/@4 the decimal point before
/// that digit; @5..@8 the small display (not read); @9 bit 0 x10, bits 2-3
/// the unit (0x08 lx, 0x04 fc), bit 4 low battery; @10 bit 1 auto range,
/// bits 4-7 the range; @11 bit 4 HOLD, bit 5 MAX, bit 6 MIN. " 0L " is the
/// overload.
///
/// Source: ut382.py of github.com/parametrek/gm1020 (GPL-3.0); no capture.
class DecoderUniTUT382 : public DmmDecoder
{
  Q_OBJECT
public:
  DecoderUniTUT382(FrameFormat::DataFormat df) : DmmDecoder(df) { m_name = "UT382"; }

  std::optional<DmmDecoder::DmmResponse> decode(const QByteArray &data, int id) override;
  bool checkFormat(const char *data, size_t idx) override;
  size_t getPacketLength() override { return 33; }
};
