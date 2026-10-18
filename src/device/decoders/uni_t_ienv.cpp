// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "device/decoders/uni_t_ienv.h"

#include <QHash>
#include <QRegularExpression>

#include "core/sampletypes.h"

// The display counts are the frame's digits, not a manual's ranges. Only the
// UT333BT has been tried with QtDMM, so the others keep the "*".
static const bool registered = []() {
  DmmDecoder::addConfig({"Uni-Trend", "UT333BT", "", 0, FrameFormat::UniTiENV, 8, 1, 2, 0, 1000, 0, 0, 0});
  DmmDecoder::addConfig({"Uni-Trend", "UT353BT *", "", 0, FrameFormat::UniTiENV, 8, 1, 1, 0, 10000, 0, 0, 0});
  DmmDecoder::addConfig({"Uni-Trend", "UT363BT *", "", 0, FrameFormat::UniTiENV, 8, 1, 2, 0, 10000, 0, 0, 0});
  DmmDecoder::addConfig({"Uni-Trend", "UT383BT *", "", 0, FrameFormat::UniTiENV, 8, 1, 1, 0, 100000, 0, 0, 0});
  return true;
}();

namespace
{
// frame byte 4: the measurement channel, one per frame
constexpr quint8 kFnTemp = 0x30;     // UT333BT / UT363BT: the second value
constexpr quint8 kFnHumidity = 0x34; // UT333BT
constexpr quint8 kFnWind = 0x37;     // UT363BT
constexpr quint8 kFnLight = 0x3A;    // UT383BT
constexpr quint8 kFnSound = 0x3B;    // UT353BT

// frame byte 15 on the sound meter, the vendor app's constants (Slow=51,
// Fast=52); UNVERIFIED
constexpr quint8 kModeSlow = 0x33;
constexpr quint8 kModeFast = 0x34;

// The unit ids: frame byte 14 and the SET payload share one id space. "next"
// is what a unit key selects (a ring, so the wind speed's five units can be
// stepped through). "FC", "dBC" and the wind units are the vendor app's, not
// captured.
struct UnitInfo
{
  quint8 id;
  const char *name;
  quint8 next;
};
const UnitInfo kUnits[] = {
  { 0x30, "°C",     0x31 }, { 0x31, "°F",  0x30 },
  { 0x32, "%RH",    0x33 }, { 0x33, "%",   0x32 },
  { 0x34, "m/s",    0x35 }, { 0x35, "km/h", 0x36 }, { 0x36, "ft/min", 0x37 },
  { 0x37, "knots",  0x38 }, { 0x38, "mph", 0x34 },
  { 0x3B, "lx",     0x3C }, { 0x3C, "fc",  0x3B },
  { 0x3D, "dBA",    0x3E }, { 0x3E, "dBC", 0x3D },
};

const UnitInfo *findUnit(quint8 id)
{
  for (const UnitInfo &u : kUnits)
    if (u.id == id)
      return &u;
  return nullptr;
}

const QByteArray kPoll(1, char(0x5E));
}

QByteArray DecoderUniTiENV::pollRequest() const
{
  return kPoll;
}

// SET frame: AA BB 04 30 <payload> <16-bit BE sum of the first five>
// (HOLD: AA BB 04 30 42 01 DB)
QByteArray DecoderUniTiENV::commandFrame(quint8 payload)
{
  QByteArray f = QByteArray::fromHex("aabb0430");
  f.append(char(payload));
  unsigned sum = 0;
  for (char c : std::as_const(f))
    sum += static_cast<unsigned char>(c);
  f.append(char((sum >> 8) & 0xFF));
  f.append(char(sum & 0xFF));
  return f;
}

// HOLD is confirmed on a UT333BT. MAX/MIN/AVG are the protocol's ids; what
// repeated presses of MAX/MIN do is UNVERIFIED (the status byte shows it).
// select1 steps the main value's unit, select2 the temperature's or, on the
// sound meter, the time weighting. Nothing is sent before a frame has told
// the current unit.
QByteArray DecoderUniTiENV::keyRequest(const QString &key) const
{
  static const QHash<QString, quint8> codes = {
    { "hold", 0x42 }, { "minmax", 0x3F }, { "minmax_off", 0x40 },
  };
  const auto it = codes.constFind(key);
  if (it != codes.constEnd())
    return commandFrame(it.value());

  auto stepUnit = [](quint8 cur) -> QByteArray {
    const UnitInfo *u = findUnit(cur);
    return u ? commandFrame(u->next) : QByteArray();
  };

  if (key == "select1")
    return stepUnit(m_mainUnit);
  if (key == "select2")
  {
    if (m_tempUnitByte)
      return stepUnit(m_tempUnitByte);
    if (m_weighting == kModeSlow)
      return commandFrame(0x43); // Fast
    if (m_weighting == kModeFast)
      return commandFrame(0x44); // Slow
  }
  if (key == "rel")
    return commandFrame(0x41); // AVG, the nearest of the meters' own keys
  return QByteArray();
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

// The second display of a two-channel meter: the cached temperature, or
// id2 = 1 with an empty val2 while none has been seen (the display off, see
// DmmResponse::id2).
void DecoderUniTiENV::attachTemp(DmmDecoder::DmmResponse &r) const
{
  r.id2 = 1;
  if (!m_haveTemp)
    return;
  r.val2 = m_tempVal;
  r.dval2 = m_tempD;
  r.unit2 = m_tempUnit;
  r.special2.clear();
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

  const quint8 func = f[4];
  const quint8 unitId = f[14];

  bool temp = false;
  bool twoChannel = false;
  switch (func)
  {
  case kFnTemp:
    temp = true;
    m_tempUnitByte = unitId;
    break;
  case kFnHumidity:
  case kFnWind:
    twoChannel = true;
    m_mainUnit = unitId;
    break;
  case kFnLight:
  case kFnSound:
    m_mainUnit = unitId;
    break;
  default:
    return std::nullopt; // a channel this decoder does not know
  }

  // the display: 6 characters, sign and decimal point included
  QString text = QString::fromLatin1(reinterpret_cast<const char *>(f + 5), 6);
  text.remove(' ');
  bool ok = false;
  const double shown = text.toDouble(&ok);
  // no overload captured. A word ("OL") goes on as it comes, ReadingAdapter
  // makes it an overload; dashes or a blank display are no reading now (as
  // with APPA), not a valid 0
  static const QRegularExpression letters("[A-Za-z]");
  if (!ok && !text.contains(letters))
    return std::nullopt;

  // the unit from the unit id; an id not listed falls back to the meter's own
  // unit word (frame bytes 11..13)
  QString unit;
  if (const UnitInfo *u = findUnit(unitId))
    unit = QString::fromUtf8(u->name);
  else
    unit = QString::fromLatin1(reinterpret_cast<const char *>(f + 11), 3).trimmed();

  if (temp)
  {
    m_tempVal = text;
    m_tempD = ok ? shown : 0;
    m_tempUnit = unit;
    m_haveTemp = true;
    if (!m_haveMain)
      return std::nullopt; // nothing to hang it on yet
    // the main value of the last humidity / wind frame, with the fresh
    // temperature, so every poll answer updates the display
    DmmDecoder::DmmResponse r = m_main;
    r.id = id;
    attachTemp(r);
    return r;
  }

  m_result.val = text;
  m_result.dval = ok ? shown : 0;
  m_result.unit = unit;

  // status byte: bit0 HOLD, bit1 AVG, bit2 MIN, bit3 MAX (HOLD confirmed on
  // a UT333BT, the others from the vendor app)
  m_result.hold = f[16] & 0x01;
  if (f[16] & 0x08)
    m_result.states |= SampleFlag::Max;
  if (f[16] & 0x04)
    m_result.states |= SampleFlag::Min;

  if (func == kFnSound)
  {
    m_weighting = f[15];
    if (m_weighting == kModeFast)
      m_result.range = "FAST";
    else if (m_weighting == kModeSlow)
      m_result.range = "SLOW";
  }

  if (twoChannel)
  {
    m_main = m_result;
    m_haveMain = true;
    attachTemp(m_result);
  }
  return m_result;
}
