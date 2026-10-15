// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "device/decoders/uni_t_ienv.h"

#include <QRegularExpression>

// The display counts are the frame's digits (5 for lux, 4 for dB), not a
// manual's ranges. Not tried with QtDMM, so "*".
static const bool registered = []() {
  DmmDecoder::addConfig({"Uni-Trend", "UT383BT *", "", 0, FrameFormat::UniTiENV, 8, 1, 1, 0, 100000, 0, 0, 0});
  DmmDecoder::addConfig({"Uni-Trend", "UT353BT *", "", 0, FrameFormat::UniTiENV, 8, 1, 1, 0, 10000, 0, 0, 0});
  return true;
}();

namespace
{
// The meters' unit words and QtDMM's spelling. The UT383BT's "LUX" and the
// UT353BT's "dBA" are captured; "FC" (the UT383BT's foot-candle setting)
// and "dBC" (C weighting) are the manuals' units in the meters' style,
// unverified. A word not listed is shown as the meter sends it.
struct UnitWord
{
  const char *word;
  const char *unit;
  const char *special;
};
const UnitWord kUnits[] = {
  { "LUX", "lx",  "" },
  { "LX",  "lx",  "" },
  { "FC",  "fc",  "" },
  { "dBA", "dBA", "" },
  { "dBC", "dBC", "" },
};

const QByteArray kPoll(1, char(0x5E));
}

QByteArray DecoderUniTiENV::pollRequest() const
{
  return kPoll;
}

bool DecoderUniTiENV::frameValid(const unsigned char *f)
{
  if (f[0] != 0xAA || f[1] != 0xBB || f[2] != kFrameLength - 3)
    return false;
  unsigned sum = 0;
  for (int i = 0; i < kFrameLength - 2; ++i)
    sum += f[i];
  return ((sum >> 8) & 0xFF) == f[kFrameLength - 2] && (sum & 0xFF) == f[kFrameLength - 1];
}

// As the iDMM frame: the last kFrameLength bytes, header, length and
// checksum; the reader's FIFO is a ring. A first answer of 26 bytes (the
// frame and seven more, seen on a UT383BT) is read at the frame's end.
bool DecoderUniTiENV::checkFormat(const char *data, size_t idx)
{
  unsigned char f[kFrameLength];
  const size_t start = (idx + FIFO_LENGTH + 1 - kFrameLength) % FIFO_LENGTH;
  for (int i = 0; i < kFrameLength; ++i)
    f[i] = static_cast<unsigned char>(data[(start + i) % FIFO_LENGTH]);
  return frameValid(f);
}

std::optional<DmmDecoder::DmmResponse> DecoderUniTiENV::decode(const QByteArray &data, int id)
{
  m_result = {};
  m_result.id = id;
  m_result.showBar = true;
  if (data.size() < kFrameLength)
    return std::nullopt;
  const auto *f = reinterpret_cast<const unsigned char *>(data.constData()) + data.size() - kFrameLength;
  if (!frameValid(f))
    return std::nullopt;

  // the display: the value, then the unit word
  const QString display = QString::fromLatin1(reinterpret_cast<const char *>(f + 5), 9);
  static const QRegularExpression parts("^\\s*(\\S*?)\\s*([A-Za-z%/]+)$");
  const QRegularExpressionMatch m = parts.match(display);
  if (!m.hasMatch())
    return std::nullopt;
  const QString text = m.captured(1);
  const QString word = m.captured(2);
  m_result.unit = word;
  for (const UnitWord &u : kUnits)
    if (word.compare(QLatin1String(u.word), Qt::CaseInsensitive) == 0)
    {
      m_result.unit = QString::fromUtf8(u.unit);
      m_result.special = QString::fromLatin1(u.special);
      break;
    }

  bool ok = false;
  const double shown = text.toDouble(&ok);
  // no overload captured. A word ("OL") goes on as it comes, ReadingAdapter
  // makes it an overload; dashes or a blank display are no reading now (as
  // with APPA), not a valid 0
  static const QRegularExpression letters("[A-Za-z]");
  if (!ok && !text.contains(letters))
    return std::nullopt;
  m_result.val = text;
  m_result.dval = ok ? shown : 0;
  return m_result;
}
