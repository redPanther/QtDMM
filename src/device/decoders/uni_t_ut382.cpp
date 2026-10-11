// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "device/decoders/uni_t_ut382.h"

#include <algorithm>
#include <cmath>

#include "core/sampletypes.h"

// Ranges 2 ... 20000 lx with four digits and x10: 2000 counts. Not tried
// with a real meter, so "*".
static const bool registered = []() {
  DmmDecoder::addConfig({"Uni-Trend", "UT382 *", "", 19200, FrameFormat::UniTUT382, 8, 1, 1, 0, 2000, 0, 0, 0});
  return true;
}();

namespace
{
/// 7-segment code of the large display to its digit, -1 blank, -2 'L',
/// -3 anything else.
int segmentDigit(unsigned char code)
{
  switch (code)
  {
    case 0x00: return -1;
    case 0x7B: return 0;
    case 0x60: return 1;
    case 0x5E: return 2;
    case 0x7C: return 3;
    case 0x65: return 4;
    case 0x3D: return 5;
    case 0x3F: return 6;
    case 0x70: return 7;
    case 0x7F: return 8;
    case 0x7D: return 9;
    case 0x0B: return -2;
    default: return -3;
  }
}
}

bool DecoderUniTUT382::checkFormat(const char *data, size_t idx)
{
  // the newest byte is the one after CR LF; 30 bytes 0x3? before them
  auto at = [&](int back) { return static_cast<unsigned char>(data[(idx + FIFO_LENGTH - back) % FIFO_LENGTH]); };
  if (at(1) != 0x0a || at(2) != 0x0d)
    return false;
  for (int back = 3; back < 33; ++back)
    if ((at(back) & 0xf0) != 0x30)
      return false;
  return true;
}

std::optional<DmmDecoder::DmmResponse> DecoderUniTUT382::decode(const QByteArray &data, int id)
{
  Q_UNUSED(id);
  const auto *raw = reinterpret_cast<const unsigned char *>(data.constData());
  if (data.size() != 33 || raw[30] != 0x0d || raw[31] != 0x0a)
    return std::nullopt;
  unsigned char b[15];
  for (int i = 0; i < 15; ++i)
  {
    if ((raw[2 * i] & 0xf0) != 0x30 || (raw[2 * i + 1] & 0xf0) != 0x30)
      return std::nullopt;
    b[i] = static_cast<unsigned char>((raw[2 * i] & 0x0f) | ((raw[2 * i + 1] & 0x0f) << 4));
  }

  // a menu on the display: no reading
  if (b[0] == 0x20 || b[0] == 0x30 || b[0] == 0x40 || b[0] == 0x50 || b[0] == 0x60)
    return std::nullopt;

  QString unit;
  switch (b[9] & 0x0c)
  {
    case 0x08: unit = "lx"; break;
    case 0x04: unit = "fc"; break;
    default: return std::nullopt;
  }

  m_result = {};
  m_result.id = 0;
  m_result.special = "LUX";
  m_result.unit = unit;
  m_result.range = (b[10] & 0x02) ? "AUTO" : "MANU";
  m_result.hold = b[11] & 0x10;
  m_result.lowBat = b[9] & 0x10;
  if (b[11] & 0x20)
    m_result.states |= SampleFlag::Max;
  if (b[11] & 0x40)
    m_result.states |= SampleFlag::Min;

  // thousands first: @4 @3 @2 @1
  const int d[4] = { segmentDigit(b[4] & 0x7f), segmentDigit(b[3] & 0x7f), segmentDigit(b[2] & 0x7f),
                     segmentDigit(b[1] & 0x7f) };
  if (d[0] == -1 && d[1] == 0 && d[2] == -2 && d[3] == -1)
  {
    m_result.val = "OL";
    m_result.dval = 0;
    return m_result;
  }
  int count = 0;
  for (int digit : d)
  {
    if (digit == -1)
      continue;   // a leading blank
    if (digit < 0)
      return std::nullopt;
    count = count * 10 + digit;
  }
  int decimals = (b[4] & 0x80) ? 3 : (b[3] & 0x80) ? 2 : (b[2] & 0x80) ? 1 : 0;
  double value = count / std::pow(10.0, decimals);
  if (b[9] & 0x01)
  {
    value *= 10;
    decimals = std::max(0, decimals - 1);
  }
  m_result.dval = value;
  m_result.val = QString::number(value, 'f', decimals);
  return m_result;
}
