// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device/dmmdecoder.h"

/// UNI-T's small Bluetooth LE meters for UNI-T's "iENV" app: UT383BT
/// luxmeter, UT353BT sound level meter, UT333BT hygrometer, UT363BT
/// anemometer. A vendor GATT service 0xFF12: QtDMM writes the poll 0x5E to
/// 0xFF01, the meter answers with one 19-byte frame per channel on 0xFF02:
///
///     AA BB 10 | 01 | function | 6 x ASCII display | 4 bytes | mode | status | checksum
///
/// "function" names the channel: 0x3A light (UT383BT), 0x3B sound (UT353BT),
/// 0x34 humidity (UT333BT), 0x37 wind speed (UT363BT) and 0x30 temperature
/// (UT333BT / UT363BT, the second value). The display is the value with sign
/// and decimal point. Frame byte 14 is the unit id (0x30 C, 0x31 F, 0x32
/// %RH, 0x33 %, 0x34..0x38 m/s km/h ft/min knots mph, 0x3B lx, 0x3C fc,
/// 0x3D dBA, 0x3E dBC), the id space the SET commands use too. The display
/// stays in the base unit (°C, m/s, lx): the decoder converts it to the unit
/// shown. Status byte
/// 16: bit0 HOLD, bit1 AVG, bit2 MIN, bit3 MAX. The checksum is the 16-bit BE
/// sum of bytes 0..16, as in the iDMM frame.
///
/// The two-channel meters answer one channel per poll. The decoder keeps the
/// last frame of each channel and sends both on every frame: main value
/// plus the temperature as second value (channel 1).
///
/// Keys are SET frames "AA BB 04 30 <id> sum": HOLD 0x42, MAX 0x3F, MIN
/// 0x40, AVG 0x41, Fast 0x43, Slow 0x44 ("weighting"); "unit" steps the main
/// value's unit, "unit2" the temperature's, from the last unit id seen.
///
/// Sources (no code taken): the UT383BT frames of ha-ut383bt-lux-meter
/// (MIT, from an Android btsnoop capture of UNI-T's app), ut383bt-lux-ble-
/// python (WTFPL: UUIDs, poll), esphome_uni_trend_sound_meter (MIT: UT353BT
/// frames), the notes decompiled from UNI-T's app (ut333bt.md and siblings).
/// In ablage/ienv/.
class DecoderUniTiENV : public DmmDecoder
{
  Q_OBJECT
public:
  DecoderUniTiENV(FrameFormat::DataFormat df) : DmmDecoder(df) { m_name = "UniTiENV"; }

  std::optional<DmmDecoder::DmmResponse> decode(const QByteArray &data, int id) override;
  QByteArray pollRequest() const override;
  QByteArray keyRequest(const QString &key) const override;
  bool hasKey(const QString &key, const QString &model) const override;
  bool checkFormat(const char *data, size_t idx) override;
  size_t getPacketLength() override { return kFrameLength; }

  static constexpr int kFrameLength = 19;
  /// True when @p frame (kFrameLength bytes) has the header, the length byte
  /// and a matching checksum.
  static bool frameValid(const unsigned char *frame);
  /// SET frame for one payload byte (a key id or a unit id).
  static QByteArray commandFrame(quint8 payload);

private:
  // last frame of each channel of a two-channel meter (UT333BT / UT363BT)
  DmmDecoder::DmmResponse m_main{};
  bool m_haveMain = false;
  QString m_tempVal, m_tempUnit;
  double m_tempD = 0;
  bool m_haveTemp = false;
  // last unit ids seen (frame byte 14), for the unit keys
  quint8 m_mainUnit = 0;
  quint8 m_tempUnitByte = 0;
  // sound meter time weighting (frame byte 15), for the Fast/Slow key
  quint8 m_weighting = 0;

  void attachTemp(DmmDecoder::DmmResponse &r) const;
};
