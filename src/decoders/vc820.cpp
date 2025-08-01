#include "vc820.h"

/* FortuneFS9721
 *
 * Packet structure:
 * Byte  bits 7-4 	Bit 3 	Bit 2 	Bit 1 	Bit 0
 * 0 	0x1  AC 	DC 	Auto 	RS232
 * 1 	0x2  Negative 	1A 	1B 	1C
 * 2 	0x3  1D 	1E 	1F 	1G
 * 3 	0x4  DP1 	2A 	2B 	2C
 * 4 	0x5  2D 	2E 	2F 	2G
 * 5 	0x6  DP2 	3A 	3B 	3C
 * 6 	0x7  3D 	3E 	3F 	3G
 * 7 	0x8  DP3 	4A 	4B 	4C
 * 8 	0x9  4D 	4E 	4F 	4G
 * 9 	0xa  u 	n 	k 	Diode
 * 10 0xb  m 	% 	M 	Beep
 * 11 0xc  Farads 	Ohms 	Rel 	Hold
 * 12 0xd  A 	V 	Hz 	Low battery
 * 13 0xe User3 User2 User1 User0
 *
 * Segment lettering:
 *   	C
 *
 * B 		G
 *
 *   	F
 *
 * A 		E
 *
 *   	D
 *  *
 *
 */

static const bool registered = []() {
  DmmDecoder::addConfig({"Digitek", "DT-9062"  , "", 2400, ReadEvent::VC820Continuous, 8, 1, 1, 0, 4000, 0, 0, 1});
  DmmDecoder::addConfig({"Digitek", "INO2513"  , "", 2400, ReadEvent::VC820Continuous, 8, 1, 1, 0, 4000, 0, 0, 1});
  DmmDecoder::addConfig({"Digitech", "QM1462"  , "", 2400, ReadEvent::VC820Continuous, 8, 1, 1, 0, 4000, 0, 0, 1});
  DmmDecoder::addConfig({"Digitech", "QM1538"  , "", 2400, ReadEvent::VC820Continuous, 8, 1, 1, 0, 4000, 0, 0, 1});
  DmmDecoder::addConfig({"HoldPeak", "HP-90EPC", "", 2400, ReadEvent::VC820Continuous, 8, 1, 1, 0, 4000, 0, 0, 1});
  DmmDecoder::addConfig({"PeakTech", "3330"    , "", 2400, ReadEvent::VC820Continuous, 8, 1, 1, 0, 4000, 0, 0, 1});
  DmmDecoder::addConfig({"Tenma", "72-7745"    , "", 2400, ReadEvent::VC820Continuous, 8, 1, 1, 0, 4000, 0, 0, 1});
  DmmDecoder::addConfig({"Uni-Trend", "UT60A"  , "", 2400, ReadEvent::VC820Continuous, 8, 1, 1, 0, 4000, 0, 0, 1});
  DmmDecoder::addConfig({"Uni-Trend", "UT60E"  , "", 2400, ReadEvent::VC820Continuous, 8, 1, 1, 0, 4000, 0, 0, 1});
  DmmDecoder::addConfig({"Voltcraft", "VC 820" , "", 2400, ReadEvent::VC820Continuous, 8, 1, 1, 0, 4000, 0, 0, 1});
  DmmDecoder::addConfig({"Voltcraft", "VC 840" , "", 2400, ReadEvent::VC820Continuous, 8, 1, 1, 0, 4000, 0, 0, 1});
  return true;
}();

bool DecoderVC820::checkFormat(const char* data, size_t idx)
{
  return (m_type == ReadEvent::VC820Continuous && ((data[idx] & 0xf0) == 0xe0));
}

size_t DecoderVC820::getPacketLength()
{
  return  (m_type == ReadEvent::VC820Continuous ? 14 : 0);
}

std::optional<DmmDecoder::DmmResponse> DecoderVC820::decode(const QByteArray &data, int id)
{
  m_result = {};
  m_result.id     = id;
  m_result.hold   = (data[11] & 0x01);
  m_result.showBar= true;
  m_result.range = (bit(data, 0, 1)? "AUTO" : "MANU");

  QString val;
  QString special;
  QString unit;

  const char *in = data.data();

  // check for overload else find sign and fill in digits
  if (((in[3] & 0x07) == 0x07) &&
      ((in[4] & 0x0f) == 0x0d) &&
      ((in[5] & 0x07) == 0x06) &&
      ((in[6] & 0x0f) == 0x08))
    val = "  0L";
  else
  {
    val = (in[1] & 0x08) ? " -" : "  ";

    // create string;
    for (int i = 0; i < 4; ++i)
      val += vc820Digit(((in[1 + 2 * i] << 4) & 0xf0) | (in[2 + 2 * i] & 0x0f));
  }



  // find comma position
  if (in[3] & 0x08) val = insertComma(val, 1);
  else if (in[5] & 0x08) val = insertComma(val, 2);
  else if (in[7] & 0x08) val = insertComma(val, 3);

  m_result.dval  = val.toDouble();

  // try to find some special modes
  if (in[9] & 0x01)
    special = "DI";

  special = (in[0] & 0x08) ? "AC" : "DC";

  // try to find mode
  if (in[11] & 0x08)
  {
    unit    = "F";
    special = "CA";
  }
  else if (in[11] & 0x04)
  {
    unit    = "Ohm";
    special = "OH";
  }
  else if (in[12] & 0x08)
    unit = "A";
  else if (in[12] & 0x02)
  {
    unit    = "Hz";
    special = "HZ";
  }
  else if (in[12] & 0x04)
    unit = "V";
  else if (in[10] & 0x04)
  {
    unit    = "%";
    special = "PC";
  }
  else if (in[13] & 0x01)
  {
    unit    = "C";
    special = "TE";
  }
  else
    qWarning() << "Unknown unit!";

  // try to find prefix
  if (in[9] & 0x04)       unit.prepend("n");
  else if (in[ 9] & 0x08) unit.prepend("u");
  else if (in[10] & 0x08) unit.prepend("m");
  else if (in[ 9] & 0x02) unit.prepend("k");
  else if (in[10] & 0x02) unit.prepend("M");

  m_result.special= special;
  m_result.val    = val;
  m_result.unit  = unit;

  return m_result;
}

const char *DecoderVC820::vc820Digit(int byte)
{
  int           digit[10] = { 0x7d, 0x05, 0x5b, 0x1f, 0x27, 0x3e, 0x7e, 0x15, 0x7f, 0x3f };
  const char *c_digit[10] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };

  byte &= 0x7f;

  for (int n = 0; n < 10; n++)
  {
    if (byte == digit[n])
      return c_digit[n];
  }
  return 0;
}
