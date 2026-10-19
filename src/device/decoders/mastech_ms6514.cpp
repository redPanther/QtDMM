// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "device/decoders/mastech_ms6514.h"

#include "core/sampletypes.h"

// libsigrok mastech-ms6514: 9600 8N1; no meter at hand, so "*". Two values
// (main and auxiliary display), so numValues 2.
static const bool registered = []() {
  DmmDecoder::addConfig({"MASTECH", "MS6514 *", "", 9600, FrameFormat::MastechMS6514, 8, 1, 2, 0, 9999, 0, 0, 0});
  return true;
}();

bool DecoderMastechMS6514::checkFormat(const char *data, size_t idx)
{
  auto at = [&](int back) { return ringByte(data, idx, back); };
  return at(0) == 0x0a && at(1) == 0x0d && at(17) == 0x65 && at(16) == 0x14;
}

std::optional<DmmDecoder::DmmResponse> DecoderMastechMS6514::decode(const QByteArray &data, int id)
{
  Q_UNUSED(id);
  const auto *b = reinterpret_cast<const unsigned char *>(data.constData());
  if (data.size() != 18 || b[0] != 0x65 || b[1] != 0x14 || b[16] != 0x0d || b[17] != 0x0a)
    return std::nullopt;
  if (b[2] & 0x01)
    return std::nullopt;   // a stored value read back, not the live reading

  QString unit;
  switch (b[10] & 0x03)
  {
    case 1: unit = "C"; break;
    case 2: unit = "dF"; break;
    case 3: unit = "K"; break;
    default: return std::nullopt;
  }

  // one display: its counts, sign, decimal and "no sensor"
  auto value = [&](int channel, double *dval, QString *val)
  {
    const unsigned char mod = b[11 + channel];
    double v = (b[5 + channel * 2] << 8) | b[6 + channel * 2];
    if (mod & 0x80)
      v = -v;
    const bool tenths = mod & 0x08;
    if (tenths)
      v /= 10.0;
    if (mod & 0x40)
    {
      *dval = 0;
      *val = "OPEN";   // no thermocouple at that input
      return;
    }
    *dval = v;
    *val = QString::number(v, 'f', tenths ? 1 : 0);
  };

  m_result = {};
  m_result.main().id = 0;
  m_result.main().special = "TE";
  m_result.main().unit = unit;
  m_result.hold = b[10] & 0x40;
  value(0, &m_result.main().dval, &m_result.main().val);
  // the main display shows T1-T2
  if ((b[11] & 0x03) > 0x01)
    m_result.main().states |= SampleFlag::Relative;

  DmmChannel &second = m_result.addChannel(1);
  second.unit = unit;
  second.special = "TE";
  value(1, &second.dval, &second.val);
  return m_result;
}
