// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device/dmmdecoder.h"

/// The Bluetooth LE meters ZOTEK makes, sold as ZOYI, ZOTEK, BSIDE and ANENG
/// (ZT-5B, ZT-5BQ, ZT-300AB, ZT-5566SE and their ANENG twins). They advertise
/// as "Bluetooth DMM", have the 0xFFF0 service of many BLE UART modules and
/// stream about three packets a second on characteristic 0xFFF4, unasked;
/// the keys are written to the same characteristic.
///
/// Every packet is XORed with a fixed 20-byte key, from key byte 0 in every
/// notification. Descrambled it starts `5A A5 <type>`; the type (1 to 4)
/// selects one of four layouts, each a dump of the LCD: four digits as
/// seven-segment glyphs and the annunciators as bits. No length byte, no
/// checksum: header, type and the type's length (10, 10, 11, 19 bytes) mark
/// a packet, the glyphs check the rest. Type 4 (ZT-5566SE) has a second
/// display, DmmChannel id 1.
///
/// The type names the layout, not the meter, but each layout belongs to one
/// model (modelsFromFrame()): 1 ZT-5BQ, 2 ZT-5B, 3 ZT-300AB, 4 ZT-5566SE.
///
/// Keys: `AB CD <cmd> <p0> 00 00 00 00 <16 bit BE sum>`, scrambled like the
/// packets; cmd 03 is a key press (keyRequest()).
///
/// Source: dmm-tools (GPL-3.0-or-later),
/// docs/research/zotek/reverse-engineered-protocol.md - from ZOTEK's apps and
/// manuals, the type-2 packets and keys checked on a ZOYI ZT-5B there. Spec
/// with vectors in docs/protocols/spec/zotek.yaml.
class DecoderZotek : public DmmDecoder
{
  Q_OBJECT
public:
  DecoderZotek(FrameFormat::DataFormat df) : DmmDecoder(df) { m_name = "ZotekBLE"; }

  std::optional<DmmDecoder::DmmResponse> decode(const QByteArray &data, int id) override;
  bool checkFormat(const char *data, size_t idx) override;
  /// Variable: the packet length goes with its type. The frame is everything
  /// since the previous one, the packet its end.
  size_t getPacketLength() override { return 0; }
  QByteArray keyRequest(const QString &key) const override;
  bool hasKey(const QString &key, const QString &model) const override;
  bool tellsModel() const override { return true; }
  QStringList modelsFromFrame(const QByteArray &frame) const override;

  /// XORs @p data with the meter's key, from key byte 0: scrambles and
  /// descrambles alike.
  static QByteArray scramble(QByteArray data);

private:
  /// The type of the packet that ends @p frame, 0 for none.
  static int packetType(const QByteArray &frame);
  /// The unit of the last reading, for the keys that depend on it.
  QString m_unit;
};
