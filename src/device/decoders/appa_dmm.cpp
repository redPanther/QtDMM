// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "device/decoders/appa_dmm.h"

#include <iterator>

#include "core/sampletypes.h"

// The meters libsigrok's appa-dmm reads over the optical cable (9600 8N1);
// none at hand, so "*". Display counts from APPA's data sheets: 501/502
// 40000, 503/505 and their relatives 50000 (sub display 9999), 506/506B,
// 507, 208/208B 50000.
static const bool registered = []() {
  DmmDecoder::addConfig({"APPA", "501 *", "", 9600, FrameFormat::AppaDmm, 8, 1, 2, 0, 40000, 0, 0, 0});
  DmmDecoder::addConfig({"APPA", "502 *", "", 9600, FrameFormat::AppaDmm, 8, 1, 2, 0, 40000, 0, 0, 0});
  DmmDecoder::addConfig({"APPA", "506 *", "", 9600, FrameFormat::AppaDmm, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"APPA", "506B *", "", 9600, FrameFormat::AppaDmm, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"APPA", "507 *", "", 9600, FrameFormat::AppaDmm, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"APPA", "208 *", "", 9600, FrameFormat::AppaDmm, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"APPA", "208B *", "", 9600, FrameFormat::AppaDmm, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"BENNING", "MM 12 *", "", 9600, FrameFormat::AppaDmm, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"Sefram", "7351 *", "", 9600, FrameFormat::AppaDmm, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"Sefram", "7352B *", "", 9600, FrameFormat::AppaDmm, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"KPS", "DMM9000BT *", "", 9600, FrameFormat::AppaDmm, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"APPA", "503 *", "", 9600, FrameFormat::Appa500, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"APPA", "505 *", "", 9600, FrameFormat::Appa500, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"Voltcraft", "VC 930 *", "", 9600, FrameFormat::Appa500, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"Voltcraft", "VC 950 *", "", 9600, FrameFormat::Appa500, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"Iso-Tech", "IDM 503 *", "", 9600, FrameFormat::Appa500, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"Iso-Tech", "IDM 505 *", "", 9600, FrameFormat::Appa500, 8, 1, 2, 0, 50000, 0, 0, 0});
  DmmDecoder::addConfig({"CMT", "3503 *", "", 9600, FrameFormat::Appa500, 8, 1, 2, 0, 50000, 0, 0, 0});
  return true;
}();

namespace
{
constexpr qint32 kTextCodes = 0x700000;   // a word on the display instead of digits
// libsigrok's appadmm_wordcode_e: the words that mean an overload. Every
// other word ("----", blank, "Er", "FUSE", "APO", ...) gives no reading.
constexpr qint32 kWordSpace = 0x700000;
constexpr qint32 kWordEr = 0x700010;
constexpr qint32 kWordOL = 0x70002a;
constexpr qint32 kWordOLA = 0x700033;
constexpr qint32 kWordOLV = 0x700034;
constexpr qint32 kWordOLVA = 0x700035;

// AppaDmm function codes (libsigrok appadmm_functioncode_e) of the legacy
// 500 codes: the high byte the switch position, the low byte the function
// there
int functionOf500(int code, bool lowPass)
{
  switch (code)
  {
    case 0x0000: return 0x0d;                    // °C
    case 0x0001: return 0x0e;                    // °F
    case 0x0100: return lowPass ? 0x19 : 0x01;   // AC V
    case 0x0101: return 0x02;                    // DC V
    case 0x0102: return 0x15;                    // AC+DC V
    case 0x0200: return lowPass ? 0x1a : 0x03;   // AC mV
    case 0x0201: return 0x04;                    // DC mV
    case 0x0202: return 0x16;                    // AC+DC mV
    case 0x0300: return 0x05;                    // Ω
    case 0x0301: return 0x06;                    // continuity
    case 0x0302: return 0x08;                    // capacitance
    case 0x0303: return 0x07;                    // diode
    case 0x0400: return lowPass ? 0x1c : 0x0b;   // AC mA
    case 0x0401: return 0x0c;                    // DC mA
    case 0x0402: return 0x18;                    // AC+DC mA
    case 0x0500: return lowPass ? 0x1b : 0x09;   // AC A
    case 0x0501: return 0x0a;                    // DC A
    case 0x0502: return 0x17;                    // AC+DC A
    case 0x0600: return 0x0f;                    // frequency
    case 0x0601: return 0x10;                    // duty cycle
    default: return 0;
  }
}

qint32 i24(unsigned char b0, unsigned char b1, unsigned char b2)   // b0 lowest
{
  qint32 v = b0 | (b1 << 8) | (b2 << 16);
  if (v & 0x800000)
    v -= 0x1000000;
  return v;
}
}

QByteArray DecoderAppa::pollRequest() const
{
  // 55 55 <command> 00 <sum>
  QByteArray r = QByteArray::fromHex("55550000");
  r[2] = char(command());
  r.append(char(0x55 + 0x55 + command()));
  return r;
}

size_t DecoderAppa::getPacketLength()
{
  return (m_type == FrameFormat::AppaDmm || m_type == FrameFormat::Appa500) ? size_t(4 + dataLength() + 1) : 0;
}

bool DecoderAppa::checkFormat(const char *data, size_t idx)
{
  const int total = 4 + dataLength() + 1;
  auto at = [&](int back) { return static_cast<unsigned char>(data[(idx + FIFO_LENGTH - back) % FIFO_LENGTH]); };
  const int start = total - 1;   // bytes back to the first 0x55
  if (at(start) != 0x55 || at(start - 1) != 0x55 || at(start - 2) != command() || at(start - 3) != dataLength())
    return false;
  unsigned sum = 0;
  for (int back = start; back >= 1; --back)
    sum += at(back);
  return (sum & 0xff) == at(0);
}

bool DecoderAppa::checksumOk(const QByteArray &frame)
{
  unsigned sum = 0;
  for (int i = 0; i + 1 < frame.size(); ++i)
    sum += static_cast<unsigned char>(frame[i]);
  return !frame.isEmpty() && (sum & 0xff) == static_cast<unsigned char>(frame.back());
}

QString DecoderAppa::special(int function)
{
  switch (function)
  {
    case 0x05: return "OH";
    case 0x06: return "BUZ";
    case 0x07: return "DI";
    case 0x08: return "CA";
    case 0x0d: case 0x0e: return "TE";
    case 0x0f: case 0x11: case 0x12: case 0x13: case 0x14: case 0x2f: case 0x38: return "FR";
    case 0x02: case 0x04: case 0x0a: case 0x0c: case 0x1e: case 0x1f: case 0x20: case 0x21: case 0x22:
    case 0x23: case 0x24: case 0x2c: case 0x33: case 0x45:
      return "DC";
    case 0x15: case 0x16: case 0x17: case 0x18: case 0x26: case 0x2d: case 0x48:
      return "ACDC";
    case 0x01: case 0x03: case 0x09: case 0x0b: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d:
    case 0x2b: case 0x2e: case 0x32: case 0x35: case 0x36: case 0x37: case 0x39: case 0x3a: case 0x3b:
    case 0x3c: case 0x3d: case 0x3e: case 0x3f: case 0x40: case 0x41: case 0x42: case 0x43: case 0x44:
    case 0x46: case 0x47:
      return "AC";
    default:
      return QString();
  }
}

bool DecoderAppa::reading(const Display &d, double *dval, QString *val, QString *unit)
{
  struct Unit { const char *text; double factor; };
  static const Unit units[] = {
    { "", 1 },        { "V", 1 },       { "mV", 1e-3 },  { "A", 1 },       { "mA", 1e-3 },  { "dBV", 1 },
    { "dBm", 1 },     { "mF", 1e-3 },   { "µF", 1e-6 },  { "nF", 1e-9 },   { "GOhm", 1e9 }, { "MOhm", 1e6 },
    { "kOhm", 1e3 },  { "Ohm", 1 },     { "%", 1 },      { "MHz", 1e6 },   { "kHz", 1e3 },  { "Hz", 1 },
    { "C", 1 },       { "dF", 1 },      { "s", 1 },      { "ms", 1e-3 },   { "µs", 1e-6 },  { "ns", 1e-9 },
    { "µA", 1e-6 },   { "min", 1 },     { "kW", 1e3 },   { "cosphi", 1 },
  };
  const bool knownUnit = d.unit > 0 && d.unit < int(std::size(units));
  if (d.count >= kTextCodes)
  {
    // an overload spelled out: "OL", or "OLA"/"OLV"/"OLVA", which name the unit
    const char *word = nullptr;
    switch (d.count)
    {
      case kWordOL: word = ""; break;
      case kWordOLA: word = "A"; break;
      case kWordOLV: word = "V"; break;
      case kWordOLVA: word = "VA"; break;
      default: return false;
    }
    if (knownUnit)
      *unit = QString::fromUtf8(units[d.unit].text);
    else if (*word)
      *unit = QString::fromUtf8(word);
    else
      return false;   // "OL" without a unit: of what is unknown
    *val = "OL";
    *dval = 0;
    return true;
  }
  if (!knownUnit)
    return false;
  *unit = QString::fromUtf8(units[d.unit].text);
  const int dot = qBound(0, d.dot, 4);
  if (d.overload)
  {
    *val = d.count < 0 ? "-OL" : "OL";
    *dval = 0;
    return true;
  }
  double divisor = 1;
  for (int i = 0; i < dot; ++i)
    divisor *= 10;
  const double shown = d.count / divisor;
  *val = QString::number(shown, 'f', dot);
  *dval = shown * units[d.unit].factor;
  return true;
}

std::optional<DmmDecoder::DmmResponse> DecoderAppa::decode(const QByteArray &frame, int id)
{
  Q_UNUSED(id);
  const auto *b = reinterpret_cast<const unsigned char *>(frame.constData());
  if (frame.size() != 4 + dataLength() + 1 || b[0] != 0x55 || b[1] != 0x55 || b[2] != command()
      || b[3] != dataLength() || !checksumOk(frame))
    return std::nullopt;
  const unsigned char *p = b + 4;

  int function = 0;
  bool autoRange = false;
  Display main, sub;
  if (m_type == FrameFormat::AppaDmm)
  {
    function = p[0] & 0x7f;
    autoRange = p[1] & 0x80;
    auto display = [&](const unsigned char *q)
    {
      Display d;
      d.count = i24(q[0], q[1], q[2]);
      d.dot = q[3] & 0x07;
      d.unit = q[3] >> 3;
      d.content = q[4] & 0x7f;
      d.overload = q[4] & 0x80;
      return d;
    };
    main = display(p + 2);
    sub = display(p + 7);
  }
  else
  {
    function = functionOf500((p[20] << 8) | p[21], p[24] & 0x10);
    autoRange = !(p[24] & 0x01);
    auto display = [&](const unsigned char *q)
    {
      Display d;
      d.count = i24(q[2], q[1], q[0]);   // big-endian here
      d.dot = q[3] & 0x07;
      d.unit = q[3] >> 3;
      const int content = q[4] & 0x1f;
      d.content = content < 2 ? 0 : content - 1;
      d.overload = q[4] & 0x20;
      // bit 7: nothing on this display; bit 6: a word, its number in the
      // count (0 is "Er"), as libsigrok's appadmm_500_dec_read_display
      if (q[4] & 0x80)
        d.count = kWordSpace;
      else if (q[4] & 0x40)
        d.count = d.count == 0 ? kWordEr : kWordSpace + d.count;
      return d;
    };
    main = display(p + 37);
    sub = display(p + 42);
  }

  m_result = {};
  m_result.id = 0;
  m_result.showBar = true;
  m_result.range = autoRange ? "AUTO" : "MANU";
  if (!reading(main, &m_result.dval, &m_result.val, &m_result.unit))
    return std::nullopt;   // "----", "Er", "FUSE" ...: no reading now (OL words are one)
  // the coupling of the function, where the unit has one
  const QString base = m_result.unit.right(1);
  const QString code = special(function);
  if (code == "AC" || code == "DC" || code == "ACDC")
    m_result.special = (base == "V" || base == "A" || base == "W") ? code : QString();
  else
    m_result.special = code;
  switch (main.content)
  {
    case 0x0a: case 0x0b: m_result.states |= SampleFlag::Relative; break;
    case 0x0d: case 0x10: m_result.states |= SampleFlag::Max; break;
    case 0x0e: case 0x11: m_result.states |= SampleFlag::Min; break;
    case 0x0f: m_result.states |= SampleFlag::Avg; break;
    case 0x14: case 0x1a: m_result.hold = true; break;
    default: break;
  }

  // the sub display: frequency, the other value, or off (cleared)
  m_result.id2 = 1;
  if (reading(sub, &m_result.dval2, &m_result.val2, &m_result.unit2))
  {
    if (m_result.unit2.endsWith("Hz"))
      m_result.special2 = "FR";
    else if (m_result.unit2 == "C" || m_result.unit2 == "dF")
      m_result.special2 = "TE";
  }
  else
  {
    m_result.val2.clear();
    m_result.unit2.clear();
    m_result.dval2 = 0;
  }
  return m_result;
}
