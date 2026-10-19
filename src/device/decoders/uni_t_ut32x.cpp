// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "device/decoders/uni_t_ut32x.h"

#include "core/sampletypes.h"

// docs/protocols/sources/UT325.log: 9999 counts, 2400 8N1 over the HE2325U
// cable. The UT325 frames are from a real meter; the start byte over the
// cable is not tried, so "*" for all.
static const bool registered = []() {
  DmmDecoder::addConfig({"Uni-Trend", "UT325 *", "", 2400, FrameFormat::UniTUT32x, 8, 1, 1, 0, 9999, 0, 0, 0});
  DmmDecoder::addConfig({"Uni-Trend", "UT321 *", "", 2400, FrameFormat::UniTUT32x, 8, 1, 1, 0, 9999, 0, 0, 0});
  DmmDecoder::addConfig({"Uni-Trend", "UT322 *", "", 2400, FrameFormat::UniTUT32x, 8, 1, 1, 0, 9999, 0, 0, 0});
  DmmDecoder::addConfig({"Tenma", "72-7712 *", "", 2400, FrameFormat::UniTUT32x, 8, 1, 1, 0, 9999, 0, 0, 0});
  DmmDecoder::addConfig({"Tenma", "72-7715 *", "", 2400, FrameFormat::UniTUT32x, 8, 1, 1, 0, 9999, 0, 0, 0});
  return true;
}();

bool DecoderUniTUT32x::checkFormat(const char *data, size_t idx)
{
  // CR LF at the end, '1' before it, the thermocouple type '0'..'6' at @8
  auto at = [&](int back) { return ringByte(data, idx, back); };
  return data[idx] == '\n' && at(1) == '\r' && at(2) == '1' && at(10) >= '0' && at(10) <= '6';
}

std::optional<DmmDecoder::DmmResponse> DecoderUniTUT32x::decode(const QByteArray &data, int id)
{
  if (data.size() != 19 || data[17] != '\r' || data[18] != '\n' || data[16] != '1' || data[8] < '0' || data[8] > '6')
    return std::nullopt;

  m_result = {};
  m_result.main().id = id;
  m_result.main().special = "TE";
  switch (data[5])
  {
    case '1': m_result.main().unit = "C"; break;
    case '2': m_result.main().unit = "dF"; break;
    case '3': m_result.main().unit = "K"; break;
    default: return std::nullopt;   // a stored value, or nothing to show
  }
  // which sensor the large display shows: T1-T2 is a difference
  if (data[13] == '2' || data[13] == '3')
    m_result.main().states |= SampleFlag::Relative;

  // ':' blank, ';' minus, ";;;;" no sensor; @0 says where the point is
  QString digits;
  bool negative = false;
  int dashes = 0;
  for (int i = 1; i <= 4; ++i)
  {
    const char c = data[i];
    if (c == ':')
      continue;
    if (c == ';')
    {
      negative = true;
      ++dashes;
      continue;
    }
    if (c < '0' || c > '9')
      return std::nullopt;
    digits += QLatin1Char(c);
  }
  if (dashes == 4)
  {
    m_result.main().val = "OPEN";   // no sensor at that input
    m_result.main().dval = 0;
    return m_result;
  }
  if (dashes > 1 || digits.isEmpty())
    return std::nullopt;
  const int decimals = (data[0] & 0x02) ? 1 : 0;
  const double value = (negative ? -1 : 1) * digits.toInt() / (decimals ? 10.0 : 1.0);
  m_result.main().dval = value;
  m_result.main().val = QString::number(value, 'f', decimals);
  return m_result;
}
