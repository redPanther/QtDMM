#include <cmath>

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QtTest>
#include <QRegularExpression>

#include "device/devicetype.h"
#include "device/dmmdecoder.h"
#include "device/protocols.h"
#include "device/decoders/uni_t_ienv.h"
#include "core/readingadapter.h"

// The second value of a frame, as the golden data names it (dval2, val2,
// unit2, id2): a default one (id 0) when the frame has none.
static DmmDecoder::DmmChannel second(const DmmDecoder::DmmResponse &r)
{
  return r.channels.value(1);
}

QByteArray parseHexStringToByteArray(const QString &hexString)
{
  QByteArray result;
  QStringList tokens = hexString.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
  for (const QString &token : tokens)
  {
    bool ok = false;
    char byte = static_cast<char>(token.toUInt(&ok, 16));
    if (ok)
      result.append(byte);
  }
  return result;
}

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);

  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--keys")
  {
    // the meter's keys: UNI-T iDMM frames AB CD 03 <cmd> <16-bit BE sum>,
    // the codes ble-multimeter tried on a UT60BTk (ablage/ut60bt/uni-t.md)
    int problems = 0;
    auto expect = [&](FrameFormat::DataFormat df, const QString &key, const char *hex)
    {
      const QByteArray got = DmmDecoder::getInstance(df)->keyRequest(key);
      if (got != QByteArray::fromHex(hex))
      {
        qWarning() << FrameFormat::toString(df) << key << "gives" << got.toHex(' ') << "expected" << hex;
        ++problems;
      }
    };
    const FrameFormat::DataFormat ut60bt = FrameFormat::UniTiDMM, ut61plus = FrameFormat::UniTUT61Plus;
    expect(ut60bt, "lamp", "abcd034b01c6");
    expect(ut60bt, "range", "abcd034601c1");
    expect(ut60bt, "auto", "abcd034701c2");
    expect(ut60bt, "rel", "abcd034801c3");
    expect(ut60bt, "hold", "abcd034a01c5");
    expect(ut60bt, "select1", "abcd034c01c7");
    // the UT60BT has no Hz/%, MAX/MIN and PEAK keys (tried: they do nothing)
    expect(ut60bt, "select2", "");
    expect(ut60bt, "minmax", "");
    expect(ut60bt, "peak", "");
    expect(ut60bt, "nosuchkey", "");
    expect(ut61plus, "hold", "abcd034a01c5");
    expect(ut61plus, "minmax", "abcd034101bc");
    expect(ut61plus, "minmax_off", "abcd034201bd");
    expect(ut61plus, "select2", "abcd034901c4");
    expect(ut61plus, "peak", "abcd034d01c8");
    expect(ut61plus, "peak_off", "abcd034e01c9");
    // the UT202BT: RANGE, HOLD and its colour keys, as UNI-T's app sends them
    const FrameFormat::DataFormat ut202bt = FrameFormat::UniTUT202BT;
    expect(ut202bt, "range", "abcd034601c1");
    expect(ut202bt, "hold", "abcd034a01c5");
    expect(ut202bt, "fn_a", "abcd033101ac");
    expect(ut202bt, "fn_a_long", "abcd033201ad");
    expect(ut202bt, "fn_v", "abcd033301ae");
    expect(ut202bt, "fn_ohm", "abcd033501b0");
    expect(ut202bt, "fn_ncv", "abcd033601b1");
    expect(ut202bt, "fn_ncv_long", "abcd033701b2");
    expect(ut202bt, "auto", "");
    expect(ut202bt, "lamp", "");
    expect(ut202bt, "peak", "");
    expect(ut60bt, "fn_a", "");
    expect(ut61plus, "fn_v", "");
    expect(FrameFormat::Metex14, "hold", "");   // no remote control

    // the Keysight U12xx polling loop as it runs: a request each tick, the
    // meter's answer through the ring buffer the way FrameReader reads a
    // variable-length frame, the frame into decode(). The CONF? answer is no
    // frame of its own, so CONF? must be followed by FETC?; a turn of the
    // rotary switch ("*3") hides values until CONF? says the new function.
    {
      auto keysight = DmmDecoder::getInstance(FrameFormat::KeysightU12xx);
      const QByteArray confV("\"VOLT:DC +6.000000E+00,+1.000000E-04\"\r\n");
      const QByteArray confR("\"RES +6.000000E+02,+1.000000E-01\"\r\n");
      QByteArray conf = confV;
      QByteArray value("+1.20000000E+00\r\n");
      char fifo[FIFO_LENGTH];
      size_t length = 0;
      int volts = 0, ohms = 0, wrong = 0;
      for (int tick = 0; tick < 24; ++tick)
      {
        QByteArray answer;
        if (tick == 12)
        {
          answer = "*3\r\n";   // unasked, before the answer to this tick's request
          conf = confR;
          value = "+1.00000000E+02\r\n";
        }
        const QByteArray request = keysight->pollRequest();
        if (request == "CONF?\n\r\n")
          answer += conf;
        else if (request == "FETC?\n\r\n")
          answer += value;
        else
          ++wrong;
        for (char c : answer)
        {
          fifo[length] = c;
          if (keysight->checkFormat(fifo, length))
          {
            const auto r = keysight->decode(QByteArray(fifo, int(length) + 1), 0);
            if (r && r->main().unit == "V" && tick < 12)
              ++volts;
            else if (r && r->main().unit == "Ohm" && tick > 12)
              ++ohms;
            else if (r)
              ++wrong;
            length = 0;
          }
          else
            length = (length + 1) % FIFO_LENGTH;
        }
      }
      // 12 ticks each: one CONF? (two before the switch: the first poll and
      // the tenth), the rest values; the tick of the switch shows nothing
      if (volts != 10 || ohms < 9 || wrong != 0)
      {
        qWarning() << "KeysightU12xx: polling loop gave" << volts << "V," << ohms << "Ohm," << wrong << "wrong";
        ++problems;
      }
    }

    // the states the keys switch come back in the frame: flags A MAX, MIN,
    // HOLD, REL; flags C peak - into the reading's flags
    auto frame = [](unsigned char a, unsigned char c)
    {
      QByteArray f = QByteArray::fromHex("abcd10003020203237342e370000");
      f.append(char(a)).append(char(0)).append(char(0x08 | c));
      unsigned sum = 0;
      for (char ch : std::as_const(f))
        sum += static_cast<unsigned char>(ch);
      return f.append(char(sum >> 8)).append(char(sum & 0xFF));
    };
    auto flagsOf = [&](const QByteArray &f) -> quint32
    {
      auto decoder = DmmDecoder::getInstance(ut60bt);
      const auto r = decoder->decode(f, 0);
      if (!r)
        return 0xFFFFFFFF;
      ReadingAdapter adapter;
      return adapter.adapt(*r, 0).first().sample.flags;
    };
    const quint32 states = SampleFlag::Max | SampleFlag::Min | SampleFlag::Relative | SampleFlag::Peak;
    const quint32 none = flagsOf(frame(0, 0));
    const quint32 all = flagsOf(frame(0x0F, 0x06));
    if (none == 0xFFFFFFFF || (none & states) || (none & SampleFlag::Hold))
    {
      qWarning() << "a frame without flags gives states" << Qt::hex << none;
      ++problems;
    }
    if ((all & states) != states || !(all & SampleFlag::Hold))
    {
      qWarning() << "MAX, MIN, HOLD, REL and peak give" << Qt::hex << all;
      ++problems;
    }
    if ((flagsOf(frame(0x08, 0)) & states) != SampleFlag::Max || (flagsOf(frame(0x01, 0)) & states) != SampleFlag::Relative
        || (flagsOf(frame(0, 0x02)) & states) != SampleFlag::Peak)
    {
      qWarning() << "MAX, REL or peak min alone not told apart";
      ++problems;
    }
    // they are states of the value: V AC stays the same port
    if ((none & SampleFlag::Defining) != (all & SampleFlag::Defining))
    {
      qWarning() << "the states change the port";
      ++problems;
    }
    // iENV: a display without a number is no reading (R11-06), "OL" an
    // overload; not in any capture yet, so synthetic frames
    auto ienv = [](const char *display, char status = 0, char unit = 0x3b, char func = 0x3a)
    {
      QByteArray f = QByteArray::fromHex("aabb1001");
      f.append(func).append(display, 9).append(unit).append(char(0x30)).append(status);
      unsigned sum = 0;
      for (char ch : std::as_const(f))
        sum += static_cast<unsigned char>(ch);
      return f.append(char(sum >> 8)).append(char(sum & 0xFF));
    };
    {
      auto decoder = DmmDecoder::getInstance(FrameFormat::UniTiENV);
      const auto lux = decoder->decode(ienv("   158LUX"), 0);
      if (!lux || lux->main().dval != 158 || lux->main().unit != "lx")
      {
        qWarning() << "iENV: the synthetic frame of 158 lx does not decode";
        ++problems;
      }
      if (decoder->decode(ienv("  ----LUX"), 0) || decoder->decode(ienv("      LUX"), 0))
      {
        qWarning() << "iENV: dashes or a blank display give a reading";
        ++problems;
      }
      const auto ol = decoder->decode(ienv("   OL LUX"), 0);
      ReadingAdapter adapter;
      if (!ol || adapter.adapt(*ol, 0).first().sample.quality != Quality::Overload)
      {
        qWarning() << "iENV: OL is no overload";
        ++problems;
      }
      // the status byte's MAX, MIN and AVG reach the reading's flags (the
      // display and the keys light them)
      auto ienvFlags = [&](char status) -> quint32
      {
        const auto r = decoder->decode(ienv("   158LUX", status), 0);
        ReadingAdapter adapter;
        return r ? adapter.adapt(*r, 0).first().sample.flags : 0xFFFFFFFF;
      };
      const quint32 modes = SampleFlag::Max | SampleFlag::Min | SampleFlag::Avg;
      if ((ienvFlags(0x08) & modes) != SampleFlag::Max || (ienvFlags(0x04) & modes) != SampleFlag::Min
          || (ienvFlags(0x02) & modes) != SampleFlag::Avg || (ienvFlags(0) & modes))
      {
        qWarning() << "iENV: MAX, MIN or AVG of the status byte not in the reading";
        ++problems;
      }
      // the display is in the base unit, converted to the unit shown: lx to fc,
      // °C to °F (tried on a UT333BT: its °F key changes only the unit id)
      const auto fc = decoder->decode(ienv("  1234LUX", 0, 0x3c), 0);
      if (!fc || fc->main().unit != "fc" || fc->main().val != "114.6" || std::fabs(fc->main().dval - 1234 / 10.7639104) > 1e-9)
      {
        qWarning() << "iENV: 1234 lx shown in fc gives" << (fc ? fc->main().val + " " + fc->main().unit : QString("nothing"));
        ++problems;
      }
      {
        auto hum = DmmDecoder::getInstance(FrameFormat::UniTiENV);
        hum->decode(ienv("  45.2 RH", 0, 0x32, 0x34), 0);
        // the frame reader counts the frames of a two-value meter (id 1 for
        // every other one): each is the main value all the same
        const auto t = hum->decode(ienv("  23.4  C", 0, 0x31, 0x30), 1);
        const auto h = hum->decode(ienv("  45.3 RH", 0, 0x32, 0x34), 1);
        if (!t || t->main().id != 0 || !h || h->main().id != 0 || second(*h).id != 1)
        {
          qWarning() << "iENV: a frame counted as the second one is not the main value";
          ++problems;
        }
        if (!t || t->main().val != "45.2" || second(*t).unit != "°F" || second(*t).val != "74.1" || std::fabs(second(*t).dval - 74.12) > 1e-9)
        {
          qWarning() << "iENV: 23.4 °C shown in °F gives" << (t ? second(*t).val + " " + second(*t).unit : QString("nothing"));
          ++problems;
        }
        // the decimals follow the meter's, not its number of digits
        const struct { const char *shown; const char *f; } temps[] = {
          { "   9.9  C", "49.8" }, { "  10.0  C", "50.0" }, { "   0.0  C", "32.0" }, { "  37.8  C", "100.0" },
        };
        for (const auto &c : temps)
        {
          const auto r = hum->decode(ienv(c.shown, 0, 0x31, 0x30), 1);
          if (!r || second(*r).val != c.f)
          {
            qWarning() << "iENV:" << c.shown << "in °F gives" << (r ? second(*r).val : QString("nothing")) << "not" << c.f;
            ++problems;
          }
        }
      }
      decoder->decode(ienv("   158LUX"), 0);   // back to lx
      // the unit key steps from the unit the last frame told: lx -> fc
      if (decoder->keyRequest("unit") != DecoderUniTiENV::commandFrame(0x3C))
      {
        qWarning() << "iENV: UNIT after a lux frame does not select fc";
        ++problems;
      }
      // the keypads of UNI-T's app per meter
      struct Pad { const char *model; const char *has; const char *lacks; };
      const Pad pads[] = {
        { "UT333BT", "hold max min unit2", "avg unit weighting" },
        { "UT353BT *", "hold max min unit weighting", "avg unit2" },
        { "UT363BT *", "hold max avg unit unit2", "min weighting" },
        { "UT383BT *", "hold max min unit", "avg unit2 weighting" },
      };
      for (const Pad &pad : pads)
      {
        for (const QString &k : QString(pad.has).split(' '))
          if (!decoder->hasKey(k, pad.model))
          {
            qWarning() << "iENV:" << pad.model << "lacks the key" << k;
            ++problems;
          }
        for (const QString &k : QString(pad.lacks).split(' '))
          if (decoder->hasKey(k, pad.model))
          {
            qWarning() << "iENV:" << pad.model << "has the key" << k;
            ++problems;
          }
      }
    }
    if (problems == 0)
      qInfo() << "Meter keys and their states consistent.";
    return problems == 0 ? 0 : 1;
  }

  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--types")
  {
    // every model has a kind (a protocol without one is a compiler warning
    // in DeviceType::ofProtocol()), every model listed by name exists, ids
    // and names are there
    int problems = 0;
    QSet<QString> registered;
    QMap<QString, int> count;
    for (const DmmDecoder::DMMInfo &m : DmmDecoder::getDeviceConfigurations())
    {
      QString model = m.model;
      if (model.endsWith(" *"))
        model.chop(2);
      // a model registered twice shows twice in the model list (a test that
      // registered QtDMM's own meters again, when all of src/ is linked)
      if (registered.contains(m.vendor + '\n' + model.trimmed()))
      {
        qWarning() << "model registered twice:" << m.vendor << m.model;
        ++problems;
      }
      registered.insert(m.vendor + '\n' + model.trimmed());
      ++count[DeviceType::id(DeviceType::of(m))];
    }
    for (const auto &[vendor, model] : DeviceType::exceptions())
      if (!registered.contains(vendor + '\n' + model))
      {
        qWarning() << "device type listed for a model that does not exist:" << vendor << model;
        ++problems;
      }
    QSet<QString> ids;
    for (int t = DeviceType::HandheldDmm; t <= DeviceType::Generic; ++t)
    {
      const auto type = static_cast<DeviceType::Type>(t);
      if (DeviceType::displayName(type).isEmpty() || ids.contains(DeviceType::id(type)))
      {
        qWarning() << "device type without name or with an id twice:" << DeviceType::id(type);
        ++problems;
      }
      ids.insert(DeviceType::id(type));
    }
    qInfo() << "models per type:" << count;
    return problems == 0 ? 0 : 1;
  }

  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--table")
  {
    // the protocol table: one row per enum value, names round-trip, every
    // factory delivers a decoder of its own type
    int problems = 0;
    for (int i = 0; i < FrameFormat::EndOfList; ++i)
    {
      const auto df = static_cast<FrameFormat::DataFormat>(i);
      const ProtocolInfo *p = protocolInfo(df);
      if (!p)
      {
        qWarning() << "no protocol table row for enum value" << i;
        ++problems;
        continue;
      }
      if (FrameFormat::fromString(FrameFormat::toString(df)) != df || FrameFormat::toString(df) != QLatin1String(p->name))
      {
        qWarning() << "name does not round-trip for" << p->name;
        ++problems;
      }
      auto decoder = DmmDecoder::getInstance(df);
      if (!decoder || decoder->getType() != df)
      {
        qWarning() << "factory gives no decoder of type" << p->name;
        ++problems;
      }
    }
    if (protocols().size() != size_t(FrameFormat::EndOfList))
    {
      qWarning() << "table has" << protocols().size() << "rows for" << int(FrameFormat::EndOfList) << "enum values";
      ++problems;
    }
    if (FrameFormat::fromString("NoSuchProtocol") != FrameFormat::Invalid || DmmDecoder::getInstance(FrameFormat::Invalid))
    {
      qWarning() << "unknown names must give Invalid / no decoder";
      ++problems;
    }
    // a saved model keeps its meter when the "not confirmed" mark goes
    if (!DmmDecoder::sameModel("Brymen BM869s *", "Brymen BM869s")
        || !DmmDecoder::sameModel("Brymen BM869s", "Brymen BM869s *")
        || DmmDecoder::sameModel("Brymen BM869s", "Brymen BM867s"))
    {
      qWarning() << "sameModel() does not ignore the trailing \" *\"";
      ++problems;
    }
    if (problems == 0)
      qInfo() << "Protocol table consistent:" << protocols().size() << "protocols.";
    return problems == 0 ? 0 : 1;
  }

  if (argc != 2)
  {
    qCritical() << "Usage: test_decoder <jsonfile> | --table";
    return 1;
  }

  QString jsonFile = QString::fromLocal8Bit(argv[1]);
  QFile file(jsonFile);
  if (!file.open(QIODevice::ReadOnly))
  {
    qCritical() << "Failed to open file:" << jsonFile;
    return 2;
  }

  QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  QJsonObject root = doc.object();
  QString decoderName = root["decoder"].toString();
  QJsonArray tests = root["tests"].toArray();

  auto decoder = DmmDecoder::getInstance(decoderName);
  if (!decoder)
  {
    qCritical() << "Unknown decoder:" << decoderName;
    return 3;
  }

  int failed = 0;

  for (int i = 0; i < tests.size(); ++i)
  {
    QJsonObject test = tests[i].toObject();
    QString hexString = test["hex"].toString();
    QJsonObject expected = test["expected"].toObject();
    QByteArray frame = parseHexStringToByteArray(hexString);

    auto fail = [&](const QString & what)
    {
      qWarning() << "Test case" << i << "failed:" << what;
      failed++;
    };

    // simulate reading from port. The ring holds stale bytes of earlier
    // frames, as the reader's does: a checkFormat() that looks back before
    // the start of the current frame must not see a frame start there
    char fifo[FIFO_LENGTH];
    for (int k = 0; k < FIFO_LENGTH; ++k)
      fifo[k] = frame.isEmpty() ? 0 : frame[k % frame.size()];
    char buffer[FIFO_LENGTH];
    const size_t packetLength = decoder->getPacketLength();
    size_t bytesToRead = packetLength;
    size_t length = 0;
    bool frameValid = false;

    for (size_t n = 0; n < 5 && !frameValid; n++)
    {
      for (qsizetype idx = 0; idx < frame.size(); idx++)
      {
        fifo[length] = frame[idx];
        if (decoder->checkFormat(fifo, length))
        {
          // variable-length protocols (packet length 0) take the whole line
          if (packetLength == 0)
            bytesToRead = length + 1;
          length = (length - bytesToRead + 1 + FIFO_LENGTH) % FIFO_LENGTH;

          for (size_t i = 0; i < bytesToRead; ++i)
          {
            buffer[i] = fifo[length];
            length = (length + 1) % FIFO_LENGTH;
          }

          length = 0;

          frameValid = true;

          break;
          //Q_EMIT readEvent( QByteArray(m_buffer,bytesToRead), m_id, m_format );
          //m_id = (m_id + 1) % m_numValues;
        }
        else
          length = (length + 1) % FIFO_LENGTH;
      }
    }

    if (!frameValid)
    {
      fail(QString("[checkFormat] '%1' is not a valid input").arg(hexString));
      continue;
    }

    auto result = decoder->decode(QByteArray(buffer, int(bytesToRead)), 0);

    if (!result)
    {
      fail(QString("[decode] '%1' produced no result").arg(hexString));
      continue;
    }


    auto check = [&](const QString & key, const auto & actual, const QJsonValue & expectedVal)
    {
      using T = std::decay_t<decltype(actual)>;

      if constexpr(std::is_same_v<T, double>)
      {
        if (!qFuzzyCompare(actual + 1, expectedVal.toDouble() + 1))
          fail(QString("%1 mismatch: got %2, expected %3").arg(key).arg(actual).arg(expectedVal.toDouble()));
      }
      else if constexpr(std::is_same_v<T, QString>)
      {
        if (actual != expectedVal.toString())
          fail(QString("%1 mismatch: got '%2', expected '%3'").arg(key).arg(actual).arg(expectedVal.toString()));
      }
      else if constexpr(std::is_same_v<T, bool>)
      {
        if (actual != expectedVal.toBool())
          fail(QString("%1 mismatch: got %2, expected %3").arg(key).arg(actual).arg(expectedVal.toBool()));
      }
      else if constexpr(std::is_same_v<T, int>)
      {
        if (actual != expectedVal.toInt())
          fail(QString("%1 mismatch: got %2, expected %3").arg(key).arg(actual).arg(expectedVal.toInt()));
      }
      else
        qWarning() << "unknown type";
    };

    const DmmDecoder::DmmChannel &main = result->main();
    const DmmDecoder::DmmChannel sub = second(*result);
    qInfo() << hexString << ": " << main.dval << main.val << main.unit << result->range << main.special << result->hold
            << result->showBar << main.id << result->error << sub.dval << sub.val << sub.unit << sub.id;

    if (expected.contains("dval"))    check("dval",    main.dval,         expected["dval"]);
    if (expected.contains("val"))     check("val",     main.val,          expected["val"]);
    if (expected.contains("unit"))    check("unit",    main.unit,         expected["unit"]);
    if (expected.contains("range"))   check("range",   result->range,     expected["range"]);
    if (expected.contains("special")) check("special", main.special,      expected["special"]);
    if (expected.contains("hold"))    check("hold",    result->hold,      expected["hold"]);
    if (expected.contains("showBar")) check("showBar", result->showBar,   expected["showBar"]);
    if (expected.contains("id"))      check("id",      main.id,           expected["id"]);
    if (expected.contains("error"))   check("error",   result->error,     expected["error"]);
    if (expected.contains("dval2"))   check("dval2",   sub.dval,          expected["dval2"]);
    if (expected.contains("val2"))    check("val2",    sub.val,           expected["val2"]);
    if (expected.contains("unit2"))   check("unit2",   sub.unit,          expected["unit2"]);
    if (expected.contains("id2"))     check("id2",     sub.id,            expected["id2"]);

    // the golden part (kern_spezifikation §11.2): the port each value lands
    // on behind ReadingAdapter
    if (expected.contains("port") || expected.contains("port2"))
    {
      ReadingAdapter adapter(decoder->getType());
      const QList<PortSample> samples = adapter.adapt(*result, 0);
      if (expected.contains("port"))
        check("port", samples.value(0).port.toString(), expected["port"]);
      if (expected.contains("port2"))
        check("port2", samples.size() > 1 ? samples[1].port.toString() : QString(), expected["port2"]);
    }
  }

  // Frame alignment invariant: feeding N readings' worth of frames through the
  // same ring buffer FrameReader uses must yield exactly N detections. The
  // per-case loop above retries the same frame up to 5 times, so it cannot tell
  // a correct guard from one that silently drops readings.
  //
  // Some meters transmit every telegram more than once (the UT803 and UT70B send
  // each one twice - see docs/protocols/sources/UT803.log and UT70B.log), and their
  // decoders deliberately match only the last copy so one measurement yields one
  // reading. Such fixtures declare "framesPerReading" so this check expects the
  // deduplication instead of flagging it.
  if (!tests.isEmpty())
  {
    const QByteArray frame = parseHexStringToByteArray(tests[0].toObject()["hex"].toString());
    const size_t bytesToRead = decoder->getPacketLength();
    const int framesPerReading = root.contains("framesPerReading") ? root["framesPerReading"].toInt() : 1;
    const int readings = 3;

    char fifo[FIFO_LENGTH];
    memset(fifo, 0, sizeof(fifo));
    size_t length = 0;
    int detected = 0;

    for (int c = 0; c < readings * framesPerReading; ++c)
    {
      for (int idx = 0; idx < frame.size(); ++idx)
      {
        fifo[length] = frame[idx];

        if (decoder->checkFormat(fifo, length))
        {
          ++detected;
          length = 0;
        }
        else
          length = (length + 1) % FIFO_LENGTH;
      }
    }

    if (detected != readings)
    {
      qWarning() << "[frame alignment] fed" << readings * framesPerReading
                 << "frames of the declared packet length" << bytesToRead
                 << "at" << framesPerReading << "frame(s) per reading, expected"
                 << readings << "detections but got" << detected;
      failed++;
    }
  }

  return failed == 0 ? 0 : 1;
}

