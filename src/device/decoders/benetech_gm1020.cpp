// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "device/decoders/benetech_gm1020.h"

#include "core/sampletypes.h"

// 0.1 ... 409.5 lx with a decimal, up to 40950 lx with x100: 4000 counts.
// The temperature comes as second value in the same frame: channels 2.
// Not tried with a real meter, so "*".
static const bool registered = []() {
  DmmDecoder::addConfig({"Benetech", "GM1020 *", "", 19200, FrameFormat::BenetechGM1020, 8, 1, 1, 0, 4000, 0, 0, 0, "", "", 2});
  return true;
}();

QByteArray DecoderBenetechGM1020::pollRequest() const
{
  static const char kLiveStart[] = { 0x3c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3d };
  return QByteArray(kLiveStart, sizeof(kLiveStart));
}

bool DecoderBenetechGM1020::checkFormat(const char *data, size_t idx)
{
  auto at = [&](int back) { return ringByte(data, idx, back); };
  return at(0) == 0x11 && at(3) == 0x01 && at(6) == 0x22 && at(7) == 0x33;
}

std::optional<DmmDecoder::DmmResponse> DecoderBenetechGM1020::decode(const QByteArray &data, int id)
{
  Q_UNUSED(id);
  const auto *b = reinterpret_cast<const unsigned char *>(data.constData());
  if (data.size() != 8 || b[0] != 0x33 || b[1] != 0x22 || b[4] != 0x01 || b[7] != 0x11)
    return std::nullopt;

  const int lux = (b[2] << 8) | b[3];
  double value = (lux & 0x0fff) / 10.0;
  int decimals = 1;
  if (lux & 0x4000)
  {
    value *= 10;
    decimals = 0;
  }
  if (lux & 0x8000)
  {
    value *= 100;
    decimals = 0;
  }

  m_result = {};
  m_result.main().id = 0;
  m_result.main().special = "LUX";
  m_result.main().unit = "lx";
  m_result.main().dval = value;
  m_result.main().val = QString::number(value, 'f', decimals);

  const double temp = ((b[5] << 8) | b[6]) / 10.0;
  DmmChannel &second = m_result.addChannel(1);
  second.special = "TE";
  second.unit = "C";
  second.dval = temp;
  second.val = QString::number(temp, 'f', 1);
  return m_result;
}
