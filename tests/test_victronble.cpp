// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// VictronBle: parsing and decrypting Instant Readout advertisements, then
// the decoder on the plaintext. Vectors are the public ones from
// victron-ble (keshavdv) - a SmartShunt 500A/50mV and a BlueSolar MPPT
// 75/15 with their keys - plus the bit reader on hand-made bytes.

#include <QtCore>

#include "device/decoders/victron_ble.h"
#include "device/victronble.h"
#include "testutil.h"

// The second value of a frame; a default one (id 0) when it has none.
static DmmDecoder::DmmChannel second(const DmmDecoder::DmmResponse &r)
{
  return r.channels.value(1);
}

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);

  // --- 1. bit reader: LSB first, little endian, signed ---
  {
    VictronBle::BitReader r(QByteArray::fromHex("ffff0000ff7f"));   // 0xFFFF, 0x0000, 0x7FFF
    check(r.unsignedBits(16) == 0xFFFF, "u16 all ones");
    check(r.signedBits(16) == 0, "s16 zero");
    check(r.signedBits(16) == 0x7FFF, "s16 max");
    check(r.unsignedBits(8) == 0xFF, "past the end reads ones");
    VictronBle::BitReader s(QByteArray::fromHex("fe"));              // 0b11111110
    check(s.unsignedBits(1) == 0 && s.signedBits(7) == -1, "bit split and sign");
    VictronBle::BitReader t(QByteArray::fromHex("ffffff3f"));         // 22 bits of ones = -1
    check(t.signedBits(22) == -1, "s22 minus one");
    // ok() tells a real -1 from one that was read past the end: for an
    // unsigned field all ones is the NA pattern, for a signed one it is not
    VictronBle::BitReader u(QByteArray::fromHex("ffffff3f"));
    check(u.signedBits(22) == -1 && u.ok(), "a complete s22 is ok");
    u.unsignedBits(20);          // 22 + 20 bits > the 32 the record has
    check(!u.ok(), "reading past the end is not");
    VictronBle::BitReader e(QByteArray::fromHex("0000"));
    check(e.signedBits(16) == 0 && e.ok() && e.signedBits(16) == -1 && !e.ok(), "and it stays not ok");
  }

  // --- 1b. a record cut short must not show values ---
  {
    DecoderVictronBLE d(FrameFormat::VictronBLE);
    // battery monitor: 4 bytes instead of 12 - voltage is still there, the
    // current is not. Before, its all-ones bits became -1 = -0.001 A.
    const auto r = d.decode(VictronBle::frame(VictronBle::BatteryMonitor, QByteArray::fromHex("e803e504")), 0);
    check(r.has_value(), "short battery monitor record still decodes");
    if (r)
    {
      check(r->main().val == "12.53" && r->main().unit == "V", "the voltage that is there is shown: " + r->main().val);
      check(second(*r).val.isEmpty() || second(*r).val == "---", "the truncated current is not: '" + second(*r).val + "'");
    }
    const auto sc = d.decode(VictronBle::frame(VictronBle::SolarCharger, QByteArray::fromHex("0000e504"), "V", "I"), 0);
    check(sc.has_value() && (second(*sc).val.isEmpty() || second(*sc).val == "---"), "solar charger: truncated current is not shown");
  }

  // --- 2. key parsing ---
  check(VictronBle::keyFromHex("aff4d0995b7d1e176c0c33ecb9e70dcd").size() == 16, "key from hex");
  check(VictronBle::keyFromHex("AF F4 D0 99 5B 7D 1E 17 6C 0C 33 EC B9 E7 0D CD").size() == 16, "key with blanks");
  check(VictronBle::keyFromHex("aff4d0995b7d1e176c0c33ecb9e70d").isEmpty(), "short key rejected");
  check(VictronBle::keyFromHex("zzf4d0995b7d1e176c0c33ecb9e70dcd").isEmpty(), "non-hex rejected");

  // --- 3. SmartShunt advertisement (victron-ble test vector) ---
  DecoderVictronBLE decoder(FrameFormat::VictronBLE);
  {
    const QByteArray adv = QByteArray::fromHex("100289a302b040af925d09a4d89aa0128bdef48c6298a9");
    const QByteArray key = VictronBle::keyFromHex("aff4d0995b7d1e176c0c33ecb9e70dcd");
    const auto parsed = VictronBle::parse(adv);
    check(parsed.has_value(), "shunt advertisement parses");
    if (parsed)
    {
      check(parsed->model == 0xA389 && parsed->readoutType == VictronBle::BatteryMonitor, "model and readout type");
      check(VictronBle::modelName(parsed->model) == "SmartShunt 500A/50mV", "model name");
      check(parsed->iv == 0x40B0, QString("iv %1").arg(parsed->iv, 0, 16));
      check(!VictronBle::decrypt(*parsed, VictronBle::keyFromHex("00f4d0995b7d1e176c0c33ecb9e70dcd")).has_value(),
            "wrong key fails the key check");
      const auto plain = VictronBle::decrypt(*parsed, key);
      check(plain.has_value(), "decrypts with the right key");
      if (plain)
      {
        const auto r = decoder.decode(VictronBle::frame(parsed->readoutType, *plain), 0);
        check(r.has_value(), "battery monitor decodes");
        if (r)
        {
          check(qFuzzyCompare(r->main().dval, 12.53) && r->main().val == "12.53" && r->main().unit == "V" && r->main().special == "DC",
                QString("voltage 12.53 V: %1 %2").arg(r->main().val, r->main().unit));
          check(second(*r).id == 1 && qFuzzyCompare(second(*r).dval + 1, 1.0) && second(*r).unit == "A", "current 0 A as second value");
        }
      }
    }
  }

  // --- 4. BlueSolar MPPT advertisement (victron-ble test vector) ---
  {
    const QByteArray adv = QByteArray::fromHex("100242a0016207adceb37b605d7e0ee21b24df5c");
    const QByteArray key = VictronBle::keyFromHex("adeccb947395801a4dd45a2eaa44bf17");
    const auto parsed = VictronBle::parse(adv);
    check(parsed && parsed->model == 0xA042 && parsed->readoutType == VictronBle::SolarCharger, "mppt parses");
    const auto plain = parsed ? VictronBle::decrypt(*parsed, key) : std::nullopt;
    check(plain.has_value(), "mppt decrypts");
    if (plain)
    {
      const auto r = decoder.decode(VictronBle::frame(VictronBle::SolarCharger, *plain), 0);
      check(r.has_value(), "solar charger decodes");
      if (r)
      {
        check(r->main().dval == 19.0 && r->main().val == "19" && r->main().unit == "W", QString("PV power 19 W: %1 %2").arg(r->main().val, r->main().unit));
        check(second(*r).id == 1 && qFuzzyCompare(second(*r).dval, 13.88) && second(*r).val == "13.88" && second(*r).unit == "V",
              QString("battery 13.88 V as second value: %1 %2").arg(second(*r).val, second(*r).unit));
      }
    }
  }

  // --- 4b. Phoenix Inverter: the plaintext of a live 12V 500VA advertisement
  // (13.17 V, 6 VA, 230.02 V AC, inverting), re-encrypted with a test key ---
  {
    const QByteArray adv = QByteArray::fromHex("100251a20334120124dda175e47aa1fec45db8");
    const QByteArray key = VictronBle::keyFromHex("0123456789abcdef0123456789abcdef");
    const auto parsed = VictronBle::parse(adv);
    check(parsed && parsed->model == 0xA251 && parsed->readoutType == VictronBle::Inverter && parsed->iv == 0x1234, "inverter parses");
    check(parsed && VictronBle::modelName(parsed->model) == "Phoenix Inverter 12V 500VA 230V", "inverter model name");
    const auto plain = parsed ? VictronBle::decrypt(*parsed, key) : std::nullopt;
    check(plain && plain->toHex() == "09000025050600da5900fc", "inverter decrypts to the live plaintext");
    if (plain)
    {
      const auto r = decoder.decode(VictronBle::frame(VictronBle::Inverter, *plain), 0);
      check(r.has_value(), "inverter decodes");
      if (r)
      {
        check(r->main().dval == 6.0 && r->main().val == "6" && r->main().unit == "VA" && r->main().special == "AC",
              QString("6 VA AC: %1 %2 %3").arg(r->main().val, r->main().unit, r->main().special));
        check(second(*r).id == 1 && qFuzzyCompare(second(*r).dval, 13.17) && second(*r).unit == "V", "battery 13.17 V as second value");
      }
    }
  }

  // --- 4c. choosing the fields ---
  {
    // the SmartShunt vector again: 12.53 V, 0 A, SOC 50 %, -50 Ah consumed
    const auto parsed = VictronBle::parse(QByteArray::fromHex("100289a302b040af925d09a4d89aa0128bdef48c6298a9"));
    const auto plain = VictronBle::decrypt(*parsed, VictronBle::keyFromHex("aff4d0995b7d1e176c0c33ecb9e70dcd"));
    const auto soc = decoder.decode(VictronBle::frame(VictronBle::BatteryMonitor, *plain, "SOC", "AH"), 0);
    check(soc && qFuzzyCompare(soc->main().dval, 50.0) && soc->main().val == "50.0" && soc->main().unit == "%",
          QString("SOC as main value: %1 %2").arg(soc ? soc->main().val : "-", soc ? soc->main().unit : "-"));
    check(soc && second(*soc).id == 1 && qFuzzyCompare(second(*soc).dval, -50.0) && second(*soc).unit == "Ah", "consumed Ah as second value");
    const auto none = decoder.decode(VictronBle::frame(VictronBle::BatteryMonitor, *plain, "P", "-"), 0);
    check(none && second(*none).id == 1 && second(*none).val.isEmpty() && none->main().unit == "W" && qFuzzyCompare(none->main().dval + 1, 1.0), "power main, no second value");
    check(!decoder.decode(VictronBle::frame(VictronBle::BatteryMonitor, *plain, "NOPE", "V"), 0).has_value(),
          "unknown field id gives nothing");
    const QList<DecoderVictronBLE::Value> all = DecoderVictronBLE::values(VictronBle::BatteryMonitor, *plain);
    check(all.size() == 7 && all[0].id == "V" && all[6].id == "AUX", "battery monitor has seven fields");
    check(VictronBle::fields(VictronBle::BatteryMonitor).size() == 7 && VictronBle::fields(0x42).isEmpty(), "field tables");
    check(VictronBle::readoutTypeForModel("SmartShunt") == VictronBle::BatteryMonitor
          && VictronBle::readoutTypeForModel("BMV-712 Smart *") == VictronBle::BatteryMonitor
          && VictronBle::readoutTypeForModel("SmartSolar MPPT") == VictronBle::SolarCharger
          && VictronBle::readoutTypeForModel("Phoenix Inverter Smart") == VictronBle::Inverter
          && VictronBle::readoutTypeForModel("UT61E") == 0, "readout type from model name");
    check(VictronBle::frame(2, QByteArray::fromHex("aa"), "V", "") == "02aa V -\n", "frame with fields: " + VictronBle::frame(2, QByteArray::fromHex("aa"), "V", ""));
  }

  // --- 5. other records and junk ---
  check(!VictronBle::parse(QByteArray::fromHex("0269b907109a")).has_value(), "non-readout record ignored");
  check(!VictronBle::parse(QByteArray()).has_value(), "empty ignored");
  check(!decoder.decode("08" + QByteArray(16, 'a') + "\n", 0).has_value(), "unknown readout type gives nothing");
  check(!decoder.decode("zz\n", 0).has_value(), "junk line gives nothing");
  // a battery monitor with no voltage yet
  check(!decoder.decode(VictronBle::frame(VictronBle::BatteryMonitor, QByteArray::fromHex("ffffff7fffffffffffffffffffffff")), 0).has_value(),
        "n/a voltage gives nothing");

  if (failed == 0)
    qInfo() << "All Victron BLE tests passed.";
  else
    qWarning() << failed << "Victron BLE test(s) failed.";
  return failed == 0 ? 0 : 1;
}
