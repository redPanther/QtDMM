// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device/dmmdecoder.h"

/// MASTECH MS6514 two-channel thermometer over its USB cable (a serial port,
/// 9600 8N1). Streams 18-byte frames: @0..@1 0x65 0x14, @2 bit 0 a stored
/// value, @5..@6 and @7..@8 the main and the auxiliary display as 16-bit
/// big-endian counts, @10 bits 0-1 the unit (1 °C, 2 °F, 3 K) and bit 6
/// HOLD, @11/@12 per display: bit 7 minus, bit 3 one decimal, bit 6 open
/// (no sensor), bits 0-1 what it shows (main T1/T2/T1-T2; aux T1/T2/
/// MAX/MIN/AVG), @16..@17 CR LF.
///
/// The main display is the reading, the auxiliary one the second value.
/// Until QtDMM reads several channels (27.2) that is all there is of T2.
///
/// Source: libsigrok mastech-ms6514 (GPL-3.0-or-later); no capture.
class DecoderMastechMS6514 : public DmmDecoder
{
  Q_OBJECT
public:
  DecoderMastechMS6514(FrameFormat::DataFormat df) : DmmDecoder(df) { m_name = "MS6514"; }

  std::optional<DmmDecoder::DmmResponse> decode(const QByteArray &data, int id) override;
  bool checkFormat(const char *data, size_t idx) override;
  size_t getPacketLength() override { return 18; }
};
