// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device/dmmdecoder.h"

/// OWON's Bluetooth LE meters with the 6-byte frame: B33T+, B35T+, B41T+,
/// OW16B, OW18B, OW18E and the CM2100B clamp. They advertise as "BDM" (or a
/// name the user gave them), have the 0xFFF0 service and notify one frame
/// per reading on 0xFFF4, about three a second, unasked.
///
/// A frame is three 16-bit little-endian words, no header, length or
/// checksum: function/range (bits 0-2 decimal point, 6 UL, 7 OL; 3-5 SI
/// prefix; 6-9 function), status (HOLD, REL, AUTO, low battery, MIN, MAX in
/// bits 0-5) and the count, sign and magnitude (bit 15 the sign). The value
/// is count / 10^dp in the function's unit with the prefix.
///
/// The frame does not tell the model; 0xFFF2, read once after connecting,
/// does in its byte 0 (modelsFromInfo()). Keys: `<code> <01 short | 00 long>`
/// on 0xFFF3 (keyRequest()).
///
/// Source: dmm-tools (GPL-3.0-or-later),
/// docs/research/owon/reverse-engineered-protocol.md - from OWON's app, its
/// PC software and manuals, with community captures (§14.5); no meter there.
/// Spec with vectors in docs/protocols/spec/owon_ble.yaml.
class DecoderOwon : public DmmDecoder
{
  Q_OBJECT
public:
  DecoderOwon(FrameFormat::DataFormat df) : DmmDecoder(df) { m_name = "OwonBLE"; }

  std::optional<DmmDecoder::DmmResponse> decode(const QByteArray &data, int id) override;
  bool checkFormat(const char *data, size_t idx) override;
  size_t getPacketLength() override { return kFrameLength; }
  QByteArray keyRequest(const QString &key) const override;
  bool hasKey(const QString &key, const QString &model) const override;
  bool tellsModel() const override { return true; }
  QStringList modelsFromInfo(const QByteArray &info) const override;

private:
  static constexpr int kFrameLength = 6;
};
