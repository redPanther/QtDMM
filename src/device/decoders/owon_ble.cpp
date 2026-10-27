// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "device/decoders/owon_ble.h"

#include "core/sampletypes.h"

#include <cmath>

static const bool registered = []() {
  // Bluetooth LE, no baud rate. Tried on the CM2100B (all keys and ranges);
  // the others, built much the same, not yet. Counts from OWON's manuals
  // (dmm-tools spec §9.1)
  DmmDecoder::addConfig({"OWON", "B33T+ *", "", 0, FrameFormat::OwonBLE, 8, 1, 1, 0, 4000, 0, 0, 0});
  DmmDecoder::addConfig({"OWON", "B35T+ *", "", 0, FrameFormat::OwonBLE, 8, 1, 1, 0, 6000, 0, 0, 0});
  DmmDecoder::addConfig({"OWON", "B41T+ *", "", 0, FrameFormat::OwonBLE, 8, 1, 1, 0, 22000, 0, 0, 0});
  DmmDecoder::addConfig({"OWON", "OW16B *", "", 0, FrameFormat::OwonBLE, 8, 1, 1, 0, 6000, 0, 0, 0});
  DmmDecoder::addConfig({"OWON", "OW18B *", "", 0, FrameFormat::OwonBLE, 8, 1, 1, 0, 6000, 0, 0, 0});
  DmmDecoder::addConfig({"OWON", "OW18E *", "", 0, FrameFormat::OwonBLE, 8, 1, 1, 0, 20000, 0, 0, 0});
  DmmDecoder::addConfig({"OWON", "CM2100B", "", 0, FrameFormat::OwonBLE, 8, 1, 1, 0, 20000, 0, 0, 0});
  return true;
}();

namespace
{
// the functions of the function/range word, bits 6-9 (spec §6.2); 14 and 15
// only in OWON's app, on no dial of these meters
struct Function
{
  const char *special;
  const char *unit;
  bool prefixed;   ///< the prefix bits apply (not for %, °C, °F, hFE, NCV, spec §6.3)
};
const Function kFunctions[16] = {
  { "DC", "V", true },     { "AC", "V", true },  { "DC", "A", true },   { "AC", "A", true },
  { "OH", "Ohm", true },   { "CA", "F", true },  { "HZ", "Hz", true },  { "DU", "%", false },
  { "TE", "C", false },    { "TE", "dF", false }, { "DI", "V", true },  { "BUZ", "Ohm", true },
  { "HFE", "", false },    { "NCV", "", false },  { "", "W", true },    { "", "VA", true },
};
// bits 3-5 (spec §6.3)
const char *const kPrefixes[8] = { "p", "n", "µ", "m", "", "k", "M", "G" };

// the model code in byte 0 of 0xFFF2 (spec §1); 18 is the OW18B's, the
// OW16B's by the PC software's name for it ("OW18_16")
struct ModelCode
{
  const char *model;   ///< DMMInfo::model without " *"
  int code;
};
const ModelCode kModels[] = {
  { "B33T+", 33 }, { "B35T+", 35 }, { "B41T+", 41 }, { "OW18B", 18 }, { "OW16B", 18 }, { "OW18E", 20 }, { "CM2100B", 21 },
};

// the count that means no reading in OWON's app (spec §6.5)
constexpr int kNoReading = 0x6FFF;

quint16 word(const QByteArray &data, int i)
{
  return quint16(quint8(data[i]) | (quint8(data[i + 1]) << 8));
}
}

bool DecoderOwon::checkFormat(const char *data, size_t idx)
{
  // no header: the frames come whole in the notifications. Bits 10-15 of the
  // function word are 111100 in every frame captured (spec §6.1, §14.5),
  // which finds the frames again after a lost byte. @p idx counts the bytes
  // since the last frame: a frame needs six of them
  if (idx < kFrameLength - 1)
    return false;
  unsigned char f[kFrameLength];
  lastBytes(data, idx, kFrameLength, f);
  return (f[1] & 0xFC) == 0xF0;
}

std::optional<DmmDecoder::DmmResponse> DecoderOwon::decode(const QByteArray &data, int id)
{
  if (data.size() < kFrameLength)
    return std::nullopt;
  const quint16 gear = word(data, 0);
  const quint16 status = word(data, 2);
  const quint16 reading = word(data, 4);
  const int dp = gear & 7;
  const int magnitude = reading & 0x7FFF;
  const bool minus = reading & 0x8000;
  if (magnitude == kNoReading)
    return std::nullopt;
  const Function &f = kFunctions[(gear >> 6) & 0xF];
  const QString prefix = f.prefixed ? QString::fromUtf8(kPrefixes[(gear >> 3) & 7]) : QString();

  m_result = {};
  DmmChannel &main = m_result.main();
  main.id = id;
  m_result.hold = status & 0x01;
  m_result.lowBat = status & 0x08;
  m_result.range = (status & 0x04) ? "AUTO" : "MANU";
  if (status & 0x02)
    main.states |= SampleFlag::Relative;
  if (status & 0x10)
    main.states |= SampleFlag::Min;
  if (status & 0x20)
    main.states |= SampleFlag::Max;
  main.special = f.special;
  main.unit = f.unit[0] ? prefix + QString::fromUtf8(f.unit) : QString();

  const QString sign = minus ? QStringLiteral("-") : QString();
  if (dp == 6 || dp == 7)
  {
    main.val = sign + (dp == 7 ? "OL" : "UL");
    main.dval = 0;
  }
  else if (main.special == QLatin1String("NCV"))
  {
    // the field strength: EF, then one to four dashes (spec §6.7)
    const int level = int(magnitude / std::pow(10.0, dp));
    if (level > 4)
      return std::nullopt;
    main.val = level == 0 ? QStringLiteral("EF") : QString(level, '-');
    main.dval = 0;
    main.noValue = true;
  }
  else
  {
    const double shown = magnitude / std::pow(10.0, dp);
    main.val = sign + QString::number(shown, 'f', dp);
    main.dval = (minus ? -shown : shown) * prefixFactor(prefix);
  }
  return m_result;
}

QByteArray DecoderOwon::keyRequest(const QString &key) const
{
  // <code> <01 short, 00 long> on 0xFFF3 (spec §7.1). HOLD held is the
  // light: the meters share HOLD and the light on one key (§7.3, §9.1)
  static const QHash<QString, QByteArray> codes = {
    { "select1", QByteArray::fromHex("0101") }, { "range", QByteArray::fromHex("0201") },
    { "hold", QByteArray::fromHex("0301") },    { "lamp", QByteArray::fromHex("0300") },
    { "rel", QByteArray::fromHex("0401") },     { "zero", QByteArray::fromHex("0401") },
    { "select2", QByteArray::fromHex("0501") }, { "minmax", QByteArray::fromHex("0601") },
  };
  return codes.value(key);
}

// The keys of OWON's app per model (spec §7.3); key 4 is REL on the B
// series and ZERO on the CM2100B, key 5 Hz/duty
bool DecoderOwon::hasKey(const QString &key, const QString &model) const
{
  static const QHash<QString, QStringList> keys = {
    { "B33T+", { "select1", "range", "hold", "rel", "select2" } },
    { "B35T+", { "select1", "range", "hold", "lamp", "rel", "select2", "minmax" } },
    { "B41T+", { "select1", "range", "hold", "lamp", "rel", "select2", "minmax" } },
    { "OW16B", { "select1", "range", "hold", "lamp", "select2" } },
    { "OW18B", { "select1", "range", "hold", "lamp", "select2" } },
    { "OW18E", { "select1", "range", "hold", "lamp", "select2" } },
    { "CM2100B", { "select1", "hold", "lamp", "zero" } },
  };
  QString bare = model;
  bare.remove(QLatin1String(" *"));
  return keys.value(bare).contains(key);
}

QStringList DecoderOwon::modelsFromInfo(const QByteArray &info) const
{
  if (info.isEmpty())
    return {};
  const int code = quint8(info[0]);
  QStringList out;
  for (const DMMInfo &m : getDeviceConfigurations())
  {
    if (m.protocol != m_type)
      continue;
    for (const ModelCode &c : kModels)
      if (c.code == code && m.model.startsWith(QLatin1String(c.model)))
        out << m.name;
  }
  return out;
}
