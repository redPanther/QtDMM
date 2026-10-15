// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ScpiServer: the command interpreter (process()) on program messages the
// way lxi-tools, sigrok's scpi-dmm and hand-written scripts send them, then
// the same over a real TCP connection on localhost, and MdnsResponder's
// answer to a DNS-SD question round-tripped through MdnsMessage::parse.

#include <QtCore>
#include <QtNetwork>
#include <QtTest>

#include "service/mdnsbrowser.h"
#include "service/mdnsresponder.h"
#include "service/scpiserver.h"

static int failed = 0;

static void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAIL:" << what;
    ++failed;
  }
}

static QString ask(ScpiServer &s, const char *msg)
{
  return QString::fromUtf8(s.process(msg)).trimmed();
}

static ScpiServer::Reading reading(double v, const QString &unit, const QString &port = "voltage.dc")
{
  ScpiServer::Reading r;
  r.value = v;
  r.unit = unit;
  r.port = PortKey::fromString(port);
  r.range = "AUTO";
  r.valid = true;
  r.msecs = QDateTime::currentMSecsSinceEpoch();
  return r;
}

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  ScpiServer s;
  s.setModel("UNI-T UT61E");

  // --- 1. common commands ---
  check(ask(s, "*IDN?") == QString("QtDMM,UNI-T UT61E,0,%1").arg(APP_VERSION), "*IDN? " + ask(s, "*IDN?"));
  check(ask(s, "*idn?") == ask(s, "*IDN?"), "case does not matter");
  check(ask(s, "*OPC?") == "1" && ask(s, "*TST?") == "0", "*OPC? *TST?");
  check(s.process("*CLS").isEmpty(), "a command without query answers nothing");
  check(ask(s, "SYST:ERR?") == "0,\"No error\"", "error queue empty");

  // --- 2. readings ---
  check(ask(s, "READ?") == "9.91E+37", "READ? without meter: NaN " + ask(s, "READ?"));
  check(ask(s, "SYSTem:ERRor?").startsWith("-230,"), "... and -230 stale in the queue");
  check(ask(s, "SYST:ERR?").startsWith("-230,"), "(one per query)");
  check(ask(s, "SYST:ERR?") == "0,\"No error\"", "queue drained");

  s.setConnected(true);
  s.setReading(0, reading(1.2345, "V"));
  check(ask(s, "READ?") == "+1.234500E+00", "READ? " + ask(s, "READ?"));
  check(ask(s, "FETC?") == "+1.234500E+00" && ask(s, "MEAS?") == "+1.234500E+00"
        && ask(s, "MEASure:VOLTage:DC?") == "+1.234500E+00" && ask(s, "SENS:DATA?") == "+1.234500E+00"
        && ask(s, "VAL1?") == "+1.234500E+00", "FETCh? MEASure? MEAS:VOLT:DC? SENS:DATA? VAL1?");
  check(ask(s, "UNIT?") == "\"V\"", "UNIT? " + ask(s, "UNIT?"));
  check(ask(s, "CONF?") == "\"VOLT:DC AUTO\"", "CONF? " + ask(s, "CONF?"));
  check(ask(s, "READ2?") == "9.91E+37", "second value not there yet");
  s.setReading(1, reading(0.05, "A", "current.ac"));
  check(ask(s, "READ2?") == "+5.000000E-02" && ask(s, "VAL2?") == "+5.000000E-02", "READ2? VAL2?");
  check(ask(s, "UNIT2?") == "\"A\"" && ask(s, "CONF2?") == "\"CURR:AC AUTO\"", "UNIT2? CONF2?");
  s.process("*CLS");
  check(ask(s, "READ3?").isEmpty() && ask(s, "SYST:ERR?").startsWith("-114,"), "READ3? is out of range");
  s.process("*CLS");

  ScpiServer::Reading ol = reading(qQNaN(), "Ohm", "resistance");   // an overload has no value
  ol.overload = true;
  s.setReading(0, ol);
  check(ask(s, "READ?") == "9.9E+37", "overload is +INF " + ask(s, "READ?"));
  ol.value = -qQNaN();   // "-OL": the adapter keeps the sign on the NaN
  s.setReading(0, ol);
  check(ask(s, "READ?") == "-9.9E+37" && ask(s, "MEAS:RES?") == "-9.9E+37", "-OL is -INF " + ask(s, "READ?"));
  ol.value = qQNaN();
  s.setReading(0, ol);
  check(ask(s, "CONF?") == "\"RES AUTO\"", "resistance");
  check(ask(s, "STAT:QUES?") == "1", "questionable: overload bit " + ask(s, "STAT:QUES?"));
  ScpiServer::Reading old = reading(3.3, "V");
  old.msecs -= ScpiServer::kStaleMs + 1;
  s.setReading(0, old);
  check(ask(s, "READ?") == "9.91E+37" && ask(s, "STAT:QUES:COND?") == "8", "a stale reading is NaN, bit 3");
  old.overload = true;
  old.hold = true;
  s.setReading(0, old);
  check(ask(s, "STAT:QUES?") == "8", "a stale reading's overload and hold are gone " + ask(s, "STAT:QUES?"));
  s.setConnected(false);   // the link lost, QtDMM reconnects
  check(ask(s, "STAT:QUES?") == "12" && ask(s, "INP?") == "0", "lost: bit 2, INP? 0 " + ask(s, "STAT:QUES?"));
  s.setConnected(true);
  s.process("*CLS");
  s.setReading(0, reading(-2.5e-3, "V"));
  check(ask(s, "READ?") == "-2.500000E-03", "negative, small");

  // --- 3. several commands in one message, the prefix rule ---
  check(ask(s, "*IDN?;READ?;UNIT?") == QString("QtDMM,UNI-T UT61E,0,%1;-2.500000E-03;\"V\"").arg(APP_VERSION),
        "three queries, one line " + ask(s, "*IDN?;READ?;UNIT?"));
  check(ask(s, "SYST:ERR?;VERS?") == "0,\"No error\";1999.0", "second unit continues at SYST: " + ask(s, "SYST:ERR?;VERS?"));
  check(ask(s, ":SYST:ERR?;:READ?") == "0,\"No error\";-2.500000E-03", "absolute headers");
  check(s.process("*RST;*CLS").isEmpty(), "two commands, no query, no answer");
  check(ask(s, "FOO?").isEmpty() && ask(s, "SYST:ERR?").startsWith("-113,"), "unknown header -> -113");
  check(s.process("READ").isEmpty() && ask(s, "SYST:ERR?").startsWith("-100,"), "READ without ? -> -100");
  check(s.process("").isEmpty() && s.process("   ").isEmpty(), "empty message");

  // --- 4. recorder and connection through signals ---
  int starts = 0, stops = 0;
  QList<bool> connects;
  QObject::connect(&s, &ScpiServer::startRecording, [&] { ++starts; });
  QObject::connect(&s, &ScpiServer::stopRecording, [&] { ++stops; });
  QObject::connect(&s, &ScpiServer::connectRequested, [&](bool on) { connects << on; });
  s.process("INIT");
  s.process("INITiate:IMMediate");
  s.process("ABOR");
  check(starts == 2 && stops == 1, "INIT/ABOR");
  s.setRecording(true);
  check(ask(s, "STAT:OPER?") == "16", "operation: measuring while recording");
  s.process("INP OFF");
  s.process("INPut:STATe 1");
  s.process("INP");
  check(connects == QList<bool>({false, true}) && ask(s, "SYST:ERR?").startsWith("-109,"), "INPut ON/OFF, missing parameter");
  check(ask(s, "INP?") == "1", "INP? reports connected");
  s.process("*CLS");

  // screen dump as a definite-length block
  check(ask(s, "HCOP:SDUM:DATA?").isEmpty() && ask(s, "SYST:ERR?").startsWith("-240,"), "no screenshot source -> -240");
  s.setScreenshotSource([](const QByteArray &format) { return QByteArray("<") + format + ">"; });
  check(s.process("HCOPy:SDUMp:DATA?") == "#15<PNG>\n", "block: " + QString::fromUtf8(s.process("HCOP:SDUM:DATA?")));
  check(s.process("DISP:DATA?") == "#15<PNG>\n", "DISPlay:DATA? alias");
  s.process("HCOP:SDUM:DATA:FORM BMP");
  check(ask(s, "HCOP:SDUM:DATA:FORM?") == "BMP" && s.process("HCOP:SDUM:DATA?") == "#15<BMP>\n", "format switch");
  s.process("HCOP:SDUM:DATA:FORM GIF");
  check(ask(s, "SYST:ERR?").startsWith("-224,") && ask(s, "HCOP:SDUM:DATA:FORM?") == "BMP", "unknown format rejected");
  check(ScpiServer::block(QByteArray(1234, 'x')).startsWith("#41234x"), "block header for 1234 bytes");
  s.setScreenshotSource([](const QByteArray &) { return QByteArray(100000, char(0xAB)); });
  const QByteArray big = s.process("*OPC?;HCOP:SDUM:DATA?");
  check(big.size() == 2 + 8 + 100000 + 1 && big.startsWith("1;#6100000") && quint8(big[10]) == 0xAB, "binary survives, mixed with text");
  s.setScreenshotSource(nullptr);

  // error queue overflow
  for (int i = 0; i < 25; ++i)
    s.process("NOPE");
  check(ask(s, "SYST:ERR:COUN?") == "20", "queue holds 20");
  QString last;
  for (int i = 0; i < 20; ++i)
    last = ask(s, "SYST:ERR?");
  check(last.startsWith("-350,"), "last entry is the overflow " + last);

  // device clear (VXI-11, HiSLIP) leaves the error queue alone, only *CLS empties it
  s.process("NOPE");
  {
    const ScpiEndpoint ep = s.endpoint();
    if (ep.deviceClear)
      ep.deviceClear();
  }
  check(ask(s, "*STB?") == "4" && ask(s, "SYST:ERR?").startsWith("-113,"), "device clear keeps the error queue");

  // --- 4b. QtDMM's own commands and CALCulate ---
  {
    ScpiServer q;
    // nothing to ask yet: empty answers, no crash
    check(ask(q, "CALC:AVER:COUN?") == "0" && ask(q, "CALC:AVER:MIN?") == "9.91E+37", "min/max without source");
    check(ask(q, "QTDMM:REC?") == "IDLE,0.000,0,\"\"" && ask(q, "QTDMM:ALAR?") == "0", "recording, alarms without source");
    check(ask(q, "QTDMM:INST?") == "\"\"" && ask(q, "QTDMM:KEY:CAT?") == "\"\"", "instances, keys without source");

    ScpiServer::Sources src;
    src.stats = [] { return ScpiServer::Stats{ 3, 0.1, 2.5, 1.2 }; };
    src.recording = []
    {
      ScpiServer::RecordingState r;
      r.mode = ScpiServer::RecordingState::Record;
      r.seconds = 42.25;
      r.count = 170;
      r.unit = "V";
      return r;
    };
    int asked = 0;
    src.recordingData = [&asked](int n)
    {
      asked = n;
      return QList<ScpiServer::Point>{ { 0, 1.5, false }, { 0.5, std::nan(""), false }, { 1, std::nan(""), true } };
    };
    QList<ScpiServer::AlarmState> alarms = { { "Over \"5\"", ScpiServer::AlarmState::Raised },
                                             { "Low", ScpiServer::AlarmState::Off } };
    src.alarms = [&alarms] { return alarms; };
    QStringList pressed;
    src.keys = [] { return QStringList{ "range", "hold" }; };
    src.pressKey = [&pressed](const QString &k) { pressed << k; return k != "range"; };
    src.instances = [] { return QList<ScpiServer::Instance>{ { "u", 12.5, "V", true }, { "i", 0, "", false } }; };
    q.setSources(src);
    int clears = 0, acks = 0;
    QStringList marks;
    QObject::connect(&q, &ScpiServer::resetMinMaxRequested, [&] { ++clears; });
    QObject::connect(&q, &ScpiServer::acknowledgeAlarmsRequested, [&] { ++acks; });
    QObject::connect(&q, &ScpiServer::markRequested, [&](const QString &t) { marks << t; });

    check(ask(q, "CALC:AVER:MIN?;MAX?;AVER?;COUN?") == "+1.000000E-01;+2.500000E+00;+1.200000E+00;3",
          "CALCulate:AVERage " + ask(q, "CALC:AVER:MIN?;MAX?;AVER?;COUN?"));
    q.process("CALCulate:AVERage:CLEar");
    check(clears == 1, "CALC:AVER:CLEar resets min/max");
    q.process("CALC:AVER:CLE?");
    check(ask(q, "SYST:ERR?").startsWith("-100,"), "CLEar is no query");

    check(ask(q, "QTDMM:RECord?") == "REC,42.250,170,\"V\"", "QTDMM:RECord? " + ask(q, "QTDMM:REC?"));
    const QByteArray data = q.process("QTDMM:REC:DATA? 500");
    const QByteArray csv = "seconds,value\n0.000,+1.500000E+00\n0.500,nan\n1.000,9.9E+37\n";
    check(asked == 500 && data == ScpiServer::block(csv) + '\n', "RECord:DATA? block " + QString::fromUtf8(data));
    q.process("QTDMM:REC:DATA?");
    check(asked == 100, "100 readings by default");
    q.process("QTDMM:REC:DATA? 99999");
    check(asked == ScpiServer::kMaxDataPoints, "at most 10000");
    q.process("QTDMM:REC:DATA? MAX");
    check(asked == ScpiServer::kMaxDataPoints, "MAX");
    check(q.process("QTDMM:REC:DATA? 0").isEmpty() && ask(q, "SYST:ERR?").startsWith("-222,"), "0 readings: out of range");
    check(q.process("QTDMM:REC:DATA? lots").isEmpty() && ask(q, "SYST:ERR?").startsWith("-104,"), "not a number");

    check(ask(q, "QTDMM:ALARm?") == "1" && ask(q, "QTDMM:ALAR:LIST?") == "\"Over \"\"5\"\"\",RAIS,\"Low\",OFF",
          "alarms " + ask(q, "QTDMM:ALAR:LIST?"));
    check(ask(q, "STAT:OPER?") == "512", "operation bit 9 while an alarm is raised " + ask(q, "STAT:OPER?"));
    alarms[0].level = ScpiServer::AlarmState::Acknowledged;
    check(ask(q, "QTDMM:ALAR?") == "1" && ask(q, "STAT:OPER?") == "512", "acknowledged counts as raised");
    q.process("QTDMM:ALARm:ACKnowledge");
    check(acks == 1, "ALARm:ACKnowledge");

    q.process("QTDMM:MARK \"Load on\"");
    q.process("QTDMM:MARK");
    check(marks == QStringList({ "Load on", "SCPI" }), "MARK with and without text");
    marks.clear();
    q.process("QTDMM:MARK 'a;b'");
    q.process("QTDMM:MARK \"x;y\";:QTDMM:MARK 'it''s \"z\"'");
    check(marks == QStringList({ "a;b", "x;y", "it's \"z\"" }) && ask(q, "SYST:ERR?") == "0,\"No error\"",
          "a ; in single or double quotes stays in the string " + marks.join('|'));

    check(ask(q, "QTDMM:KEY:CATalog?") == "\"RANGE\",\"HOLD\"", "KEY:CATalog?");
    q.process("QTDMM:KEY \"HOLD\"");
    q.process("QTDMM:KEY hold");
    check(pressed == QStringList({ "hold", "hold" }) && ask(q, "SYST:ERR?") == "0,\"No error\"", "KEY quoted or not");
    q.process("QTDMM:KEY \"PEAK\"");
    check(ask(q, "SYST:ERR?").startsWith("-224,"), "a key the meter does not have");
    q.process("QTDMM:KEY RANGE");
    check(ask(q, "SYST:ERR?").startsWith("-240,"), "a key not sent");
    q.process("QTDMM:KEY");
    check(ask(q, "SYST:ERR?").startsWith("-109,"), "KEY without key");

    check(ask(q, "QTDMM:INSTances?") == "\"u\",+1.250000E+01,\"V\",\"i\",9.91E+37,\"\"", "instances " + ask(q, "QTDMM:INST?"));
    q.process("QTDMM:NOPE?");
    check(ask(q, "SYST:ERR?").startsWith("-113,"), "unknown QTDMM header");

    src.keys = [] { return QStringList(); };
    q.setSources(src);
    q.process("QTDMM:KEY HOLD");
    check(ask(q, "SYST:ERR?").startsWith("-241,"), "a meter without keys");
  }

  // --- 5. over TCP ---
  // a port the system picks: a fixed one (and the next) may be in use on the
  // build machine - a client's ephemeral port, another job
  check(s.start(QHostAddress::LocalHost, 0), "listens on localhost " + s.errorString());
  ScpiServer second;
  check(second.start(QHostAddress::LocalHost, s.port()) && second.port() > s.port() && second.port() < s.port() + 10,
        QString("second instance moves to a later port: %1 after %2").arg(second.port()).arg(s.port()));
  second.stop();
  {
    QTcpSocket c;
    c.connectToHost(QHostAddress::LocalHost, s.port());
    check(c.waitForConnected(2000), "client connects");
    c.write("*IDN?\r\nREAD?\n");
    // server and client share this thread: let the event loop run
    QByteArray got;
    for (int i = 0; i < 50 && !got.contains("E-03\n"); ++i)
    {
      QTest::qWait(20);
      got += c.readAll();
    }
    check(got == QString("QtDMM,UNI-T UT61E,0,%1\n-2.500000E-03\n").arg(APP_VERSION).toUtf8(),
          "answers over the socket: " + QString::fromUtf8(got));
    c.write("*CLS");   // no terminator: nothing happens
    c.waitForBytesWritten(500);
    QTest::qWait(100);
    check(s.clientCount() == 1, "one client");
    c.disconnectFromHost();
    if (c.state() != QAbstractSocket::UnconnectedState)
      c.waitForDisconnected(1000);
    QTest::qWait(100);
    check(s.clientCount() == 0, "client gone");
  }
  s.stop();

  // --- 6. mDNS responder: the answer to a browse ---
  MdnsResponder responder;
  // The answer is built without touching the network. start() is only needed
  // because the service and host names come from it - and it would announce
  // this test run to the whole network, so it stays off unless
  // QTDMM_TEST_MDNS is set (and it is never set in CI).
  QMap<QString, QString> txt;
  txt["model"] = "UNI-T UT61E";
  const bool live = qEnvironmentVariableIsSet("QTDMM_TEST_MDNS")
                    ? responder.start("_scpi-raw._tcp", "QtDMM test", 5025, txt)
                    : responder.prepare("_scpi-raw._tcp", "QtDMM test", 5025, txt);
  if (live)
  {
    const QByteArray q = MdnsMessage::query("_scpi-raw._tcp.local");
    const QByteArray a = responder.answer(q, QHostAddress("192.168.1.20"));
    const QList<MdnsMessage::Record> records = MdnsMessage::parse(a);
    check(records.size() == 4, QString("PTR, SRV, TXT, A (%1)").arg(records.size()));
    MdnsBrowser browser;
    // the browser only assembles what it asked for
    browser.browse("_scpi-raw._tcp.local", 10);
    browser.handlePacket(a);
    browser.stop();
    check(browser.services().size() == 1, "the browser sees one service");
    if (!browser.services().isEmpty())
    {
      const MdnsBrowser::Service svc = browser.services().first();
      check(svc.instance == "QtDMM test" && svc.port == 5025 && svc.host == responder.hostName().toLower()
            && svc.address == QHostAddress("192.168.1.20") && svc.txt.value("model") == "UNI-T UT61E",
            "instance, port, host, address, txt: " + svc.instance + " " + svc.host);
    }
    check(responder.answer(MdnsMessage::query("_other._tcp.local"), QHostAddress("192.168.1.20")).isEmpty(),
          "no answer for another service");
    const QList<MdnsMessage::Record> enumeration =
      MdnsMessage::parse(responder.answer(MdnsMessage::query("_services._dns-sd._udp.local"), QHostAddress("192.168.1.20")));
    check(enumeration.size() == 1 && enumeration.first().target == "_scpi-raw._tcp.local", "service enumeration");
    // the goodbye carries ttl 0
    const QList<MdnsMessage::Record> bye = MdnsMessage::parse(responder.answer(q, QHostAddress("192.168.1.20"), 0));
    check(bye.size() == 4 && bye.first().ttl == 0, "goodbye packet");
    responder.stop();
  }
  else
    qInfo() << "no multicast interface, responder not exercised";
  if (!qEnvironmentVariableIsSet("QTDMM_TEST_MDNS"))
    qInfo() << "mDNS announcement not sent (set QTDMM_TEST_MDNS to announce on the network)";

  if (failed)
    qWarning() << failed << "check(s) failed";
  else
    qInfo() << "all checks passed";
  return failed ? 1 : 0;
}
