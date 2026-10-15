// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device/dmmdecoder.h"

/// UNI-T's small Bluetooth LE meters for UNI-T's "iENV" app: UT383BT
/// luxmeter, UT353BT sound level meter. A vendor GATT service 0xFF12: QtDMM
/// writes the poll 0x5E to 0xFF01, the meter answers with one 19-byte frame
/// on 0xFF02:
///
///     AA BB 10 | 01 | model | 9 x ASCII display | 3 bytes | checksum
///
/// "model" is 0x3A on the UT383BT, 0x3B on the UT353BT. The display carries
/// the value right-aligned and the unit after it (" 158LUX", "101.1dBA");
/// the checksum is the 16-bit BE sum of bytes 0..16, as in the iDMM frame
/// (it holds on every capture in the sources). The three bytes before it
/// (';' 30 00 on the UT383BT, '=' 34 00 on the UT353BT) are not decoded:
/// no capture shows HOLD, MAX/MIN, low battery or an overload.
///
/// One class for the family, a table of the unit words per model in the
/// .cpp; the UT363BT anemometer and the UT333BT hygrometer follow once
/// their captures are in.
///
/// Sources (no code taken): the UT383BT frames of ha-ut383bt-lux-meter
/// (MIT, from an Android btsnoop capture of UNI-T's app), ut383bt-lux-ble-
/// python (WTFPL: UUIDs, poll), esphome_uni_trend_sound_meter (MIT: UT353BT
/// frames). In ablage/ienv/.
class DecoderUniTiENV : public DmmDecoder
{
  Q_OBJECT
public:
  DecoderUniTiENV(FrameFormat::DataFormat df) : DmmDecoder(df) { m_name = "UniTiENV"; }

  std::optional<DmmDecoder::DmmResponse> decode(const QByteArray &data, int id) override;
  QByteArray pollRequest() const override;
  bool checkFormat(const char *data, size_t idx) override;
  size_t getPacketLength() override { return kFrameLength; }

  static constexpr int kFrameLength = 19;
  /// True when @p frame (kFrameLength bytes) has the header, the length byte
  /// and a matching checksum.
  static bool frameValid(const unsigned char *frame);
};
