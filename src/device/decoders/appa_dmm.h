// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device/dmmdecoder.h"

/// APPA handheld meters and their relatives (BENNING, Sefram, Voltcraft,
/// ISO-TECH, RS PRO, CMT) over the optical RS-232/USB cable, 9600 8N1; the
/// AppaDmm meters with Bluetooth (S0-S3, 150B, 170B, A17N, sFlex, 208B,
/// 506B and the BENNING, KPS, MEGGER, METRAVI, RS PRO, Sefram ones) also
/// over Bluetooth LE (BleGattDevice, rows with baud 0).
/// Polled; every frame is `55 55 <command> <length> <data> <checksum>`,
/// the checksum the sum of all bytes before it.
///
/// Two protocols:
/// - AppaDmm (APPA 501/502/506/506B, 208/208B, BENNING MM 12, ...): the
///   request `55 55 01 00 AB` (read display), the answer 12 data bytes:
///   @0 function (bits 0-6), @1 range (bits 0-6) and auto range (bit 7),
///   then per display (main @2, sub @7) a 24-bit little-endian signed count,
///   a byte with the decimal point (bits 0-2) and the unit (bits 3-7), and a
///   byte with what it shows (bits 0-6: max, min, hold, rel ...) and
///   overload (bit 7).
/// - Appa500 (the older APPA 503/505, Voltcraft VC-930/VC-950, ISO-TECH
///   IDM503/505): the request `55 55 00 00 AA` (read all data), the answer
///   54 data bytes: @20..@21 the function (16-bit big-endian), @23 range,
///   @24 manual range (bit 0), low-pass filter (bit 4); per display (main
///   @37, sub @42) a 24-bit big-endian count, decimal point and unit as
///   above, and @41/@46 what it shows (bits 0-4, one above AppaDmm's),
///   overload (bit 5), a text instead of digits (bits 6/7).
///
/// A count of 0x700000 and above is a text on the display ("----", "Er",
/// "FUSE"): no reading. The main display is the reading, the sub display
/// the second value.
///
/// Source: libsigrok appa-dmm by Martin Eitzenberger (GPL-3.0-or-later,
/// branch appadmm-stage of github.com/Cymaphore/libsigrok); no capture.
class DecoderAppa : public DmmDecoder
{
  Q_OBJECT
public:
  DecoderAppa(FrameFormat::DataFormat df) : DmmDecoder(df) { m_name = df == FrameFormat::Appa500 ? "APPA 500" : "APPA"; }

  std::optional<DmmDecoder::DmmResponse> decode(const QByteArray &data, int id) override;
  QByteArray pollRequest() const override;
  bool checkFormat(const char *data, size_t idx) override;
  size_t getPacketLength() override;

  /// One display: what the frame says about it.
  struct Display
  {
    qint32 count = 0;     ///< signed, or a text code from 0x700000 on
    int    dot = 0;       ///< decimals, 0..4
    int    unit = 0;      ///< APPA unit code
    int    content = 0;   ///< what it shows (AppaDmm numbering)
    bool   overload = false;
  };
  /// Value, display text and unit of @p d; an overload word ("OL", "OLA",
  /// "OLV", "OLVA") is an overload, false for any other word.
  static bool reading(const Display &d, double *dval, QString *val, QString *unit);
  /// A word that is no reading but says why there is none: blank or dashes
  /// (DmmChannel::noValue) or a fault (DmmChannel::fault: "Er", "FUSE",
  /// "PROBE", "BATT", "HAZ", "BAD"), its text in val. False for the other
  /// words (menus, "APO") and for numbers.
  static bool showsWord(const Display &d, DmmChannel *c);
  /// Coupling or function code ("DC", "AC", "ACDC", "OH", "DI", ...) of
  /// an AppaDmm function code.
  static QString special(int function);

private:
  static bool        checksumOk(const QByteArray &frame);
  int                dataLength() const { return m_type == FrameFormat::Appa500 ? 54 : 12; }
  int                command() const { return m_type == FrameFormat::Appa500 ? 0x00 : 0x01; }
};
