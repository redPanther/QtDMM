// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device/dmmdecoder.h"

/// Benetech GM1020 logging luxmeter over its USB cable (a serial port,
/// 19200 8N1). The command "live start", 3C 01 00 00 00 00 00 3D, which
/// pollRequest() sends, makes it stream two 8-byte frames a second:
/// 33 22 <lux hi> <lux lo> 01 <temp hi> <temp lo> 11.
///
/// Lux: the low 12 bits in tenths of lx; bit 14 x10, bit 15 x100 (then no
/// decimal). Always lx, the fc setting changes only the meter's display.
/// Temperature: unsigned tenths of °C, the second value.
///
/// Source: gm1020.py of github.com/parametrek/gm1020 (GPL-3.0); no capture.
/// Its other commands (settings, the logged readings) are not used.
class DecoderBenetechGM1020 : public DmmDecoder
{
  Q_OBJECT
public:
  DecoderBenetechGM1020(FrameFormat::DataFormat df) : DmmDecoder(df) { m_name = "GM1020"; }

  std::optional<DmmDecoder::DmmResponse> decode(const QByteArray &data, int id) override;
  QByteArray pollRequest() const override;
  bool checkFormat(const char *data, size_t idx) override;
  size_t getPacketLength() override { return 8; }
};
