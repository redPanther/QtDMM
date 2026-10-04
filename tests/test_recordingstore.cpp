// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// RecordingStore on its own: QCoreApplication only, no widgets. What it
// records against a stream of readings over time (the grid export, the
// integral, the triggers) is in test_recording_golden; this is the rest:
// the points and their quality, the limits, marks by time, load, gaps, one
// function per recording and the readings series.
#include <QCoreApplication>
#include <QDebug>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <cmath>

#include "core/readingadapter.h"
#include "recording/recordingstore.h"

static int failed = 0;

static void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAILED:" << what;
    failed++;
  }
}

// A reading as the MeterController makes it: through ReadingAdapter
static Reading reading(double value, const QString &text, const QString &special, bool hold = false,
                       const QString &range = "AUTO")
{
  static ReadingAdapter adapter;
  Reading r = ReadingAdapter::reading(adapter.adaptValue(value, text, "mV", special, range, hold, true, false, 0,
                                                         QDateTime::currentMSecsSinceEpoch()));
  r.value = value;   // the tests give SI values together with display texts of their own
  return r;
}

// A clock of the test's own for a store: ms on the core clock, the wall
// clock from a fixed day
struct TestClock
{
  qint64 now = 0;
  static QDateTime wall(qint64 t) { return QDateTime(QDate(2026, 10, 4), QTime(12, 0, 0)).addMSecs(t); }
  void attach(RecordingStore &store)
  {
    store.setClock([this] { return now; }, [this] { return wall(now); });
  }
  /// @p r as it arrives at @p t (the clock moves there)
  Reading at(qint64 t, Reading r)
  {
    now = t;
    r.t = t;
    r.msecs = wall(t).toMSecsSinceEpoch();
    return r;
  }
};

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);

  // --- 1. the points: value, flags and quality of each main reading, with
  //        its time; secondary readings (id != 0) are not recorded ---
  {
    RecordingStore store;
    TestClock clock;
    clock.attach(store);
    store.start();
    store.setReading(clock.at(250, reading(0.01234, "12.34", "DC")));
    Reading second = reading(50, "50.00", "AC", false, "MANU");
    second.id = 1;
    store.setReading(clock.at(260, second));
    check(store.count() == 1, QString("point: expected 1, got %1").arg(store.count()));
    const RawPoint &p = store.series().last();
    check(p.t == 250 && p.value == 0.01234, QString("point: t %1 value %2").arg(p.t).arg(p.value));
    check(p.flags == (SampleFlag::DC | SampleFlag::Autorange), QString("point: flags 0x%1").arg(p.flags, 0, 16));
    check(p.quality == Quality::Valid, "point: quality not Valid");

    // a new recording starts with the value the meter shows
    store.start();
    check(store.count() == 1 && store.series().first().t == 0 && store.series().first().value == 0.01234,
          "start: the first point is the current value");
    store.setReading(clock.at(500, reading(0.005, "5.000", "ACDC", true, "MANU")));
    check(store.series().last().flags == (SampleFlag::AC | SampleFlag::DC | SampleFlag::Hold),
          QString("point: ACDC + hold flags 0x%1").arg(store.series().last().flags, 0, 16));
    // nothing for a while: no start value
    clock.now = 10000;
    store.start();
    check(store.count() == 0, "start: a stale value is no start value");
  }

  // --- 2. quality: an overload is a point without a value, Overload; the
  //        value going stale a point without one, Stale, where the last
  //        one stopped holding; the grid takes the worst in each step ---
  {
    RecordingStore store;
    TestClock clock;
    clock.attach(store);
    store.setSampleTime(10);
    store.setStaleAfter(1500);
    store.start();
    store.setReading(clock.at(100, reading(1, "1.000", "DC")));
    store.setReading(clock.at(600, reading(0, "OL", "DC")));
    check(store.series().last().gap() && store.series().last().quality == Quality::Overload,
          "quality: OL should be a gap point, Overload");
    store.setReading(clock.at(900, reading(1, "1.000", "DC")));
    clock.now = 2500;
    store.setStale(true);
    check(store.series().last().gap() && store.series().last().quality == Quality::Stale
            && store.series().last().t == 2400,
          QString("quality: stale gap at %1, expected 2400").arg(store.series().last().t));
    store.setStale(true);   // once
    check(store.count() == 4, QString("quality: %1 points, expected 4").arg(store.count()));
    store.setReading(clock.at(4100, reading(2, "2.000", "DC")));
    clock.now = 5000;
    store.stop();
    const QVector<GridPoint> g = store.grid(10);
    QStringList q;
    for (const GridPoint &p : g)
      q << QString::number(int(p.quality));
    // 0: nothing yet, 1: the OL, 2: valid, 3: stale from 2.4 s, 4: none, 5: 2 V from 4.1 s
    check(g.size() == 6 && g[0].quality == Quality::Stale && g[1].quality == Quality::Overload
            && g[2].quality == Quality::Valid && g[3].quality == Quality::Stale && std::isnan(g[4].value)
            && g[5].value == 2 && g[5].quality == Quality::Stale,
          "quality: grid qualities " + q.join(' '));
    check(g[2].value == 1, QString("quality: step 2 the held 1 V, got %1").arg(g[2].value));
  }

  // --- 3. the limits: the store keeps the last maxDuration(); older points
  //        go in steps (appended(true) says so), the one holding at the
  //        origin stays ---
  {
    RecordingStore store;
    TestClock clock;
    clock.attach(store);
    store.setMaxDuration(2);
    QSignalSpy appended(&store, &RecordingStore::appended);
    store.start();
    for (int i = 0; i < 100; i++)
      store.setReading(clock.at(i * 100, reading(i, QString::number(i), "DC")));
    check(store.duration() == 9900 && store.origin() == 7900,
          QString("limit: duration %1 origin %2").arg(store.duration()).arg(store.origin()));
    check(store.series().first().t <= store.origin() && store.count() <= 32,
          QString("limit: %1 points from %2").arg(store.count()).arg(store.series().first().t));
    int shifts = 0;
    for (const auto &args : appended)
      shifts += args.first().toBool() ? 1 : 0;
    check(shifts > 0 && shifts < 10, QString("limit: %1 shifts").arg(shifts));
    check(store.series().last().value == 99, "limit: the newest is kept");
  }

  // --- 4. marks by time: now while recording; a mark goes with the
  //        readings before what the store keeps ---
  {
    RecordingStore store;
    TestClock clock;
    clock.attach(store);
    store.setMaxDuration(2);
    store.start();
    store.setReading(clock.at(100, reading(1, "1", "DC")));
    clock.now = 350;
    store.addMark(0xffff0000u, "alarm");
    check(store.marks().size() == 1 && store.marks().first().t == 350, "marks: not at the time it came");
    QSignalSpy changed(&store, &RecordingStore::marksChanged);
    for (int i = 5; i < 60; i++)
      store.setReading(clock.at(i * 100, reading(i, QString::number(i), "DC")));
    check(store.marks().isEmpty() && changed.size() == 1, "marks: did not go with the old readings");
  }

  // --- 5. load and write: an import replaces the readings, a write clears
  //        dirty, the unit and start time go with the recording; a loaded
  //        file on its own grid exports as it was ---
  {
    Recording rec;
    rec.start = QDateTime(QDate(2026, 9, 30), QTime(21, 0, 0));
    rec.sampleTimeTenths = 10;
    rec.unit = "V";
    rec.values = { 1.0, 2.0, qQNaN(), 3.0 };

    RecordingStore store;
    store.setMaxDuration(1);   // too small: load widens it
    store.setUnit("V");
    store.load(rec);
    check(store.count() == 4 && store.series().last().value == 3.0 && store.series().last().t == 3000, "load: points");
    check(store.sampleTime() == 10 && store.startDateTime() == rec.start, "load: sample time or start");
    check(!store.dirty(), "load: an import is not unsaved data");
    const Recording again = store.toRecording();
    check(again.values.size() == 4 && again.values[1] == 2.0 && std::isnan(again.values[2]) && again.start == rec.start,
          "load: exported as it came");

    TestClock clock;
    clock.attach(store);
    store.start();
    store.setReading(clock.at(0, reading(7, "7.000", "DC")));
    store.stop();
    check(store.dirty(), "write: recording should be dirty");
    QTemporaryDir dir;
    QString err;
    check(store.write(dir.path() + "/r.csv", &err), "write: " + err);
    check(!store.dirty(), "write: still dirty after writing");
    const Recording back = store.toRecording();
    check(back.unit == "V" && back.values.size() == 1 && back.values.first() == 7, "write: recording content");
  }

  // --- 5a. the export of every reading: each at its time, a gap a row of
  //         its own; it imports with its times, and exports again the same;
  //         the grid of an imported one holds each value to the next ---
  {
    RecordingStore store;
    TestClock clock;
    clock.attach(store);
    store.setUnit("V");
    store.setSampleTime(10);
    store.start();
    store.setReading(clock.at(130, reading(1.5, "1.500", "DC")));
    store.setReading(clock.at(610, reading(2.5, "2.500", "DC")));
    store.setReading(clock.at(1090, reading(0, "OL", "DC")));
    store.setReading(clock.at(1570, reading(0.004, "4.000", "DC")));
    clock.now = 2000;
    store.stop();
    QTemporaryDir dir;
    const QString raw1 = dir.path() + "/raw1.csv", raw2 = dir.path() + "/raw2.csv";
    QString err;
    check(store.write(raw1, &err, true), "raw: write: " + err);
    QFile f(raw1);
    f.open(QIODevice::ReadOnly);
    const QString text = QString::fromUtf8(f.readAll());
    check(text == "timestamp;time (s);value;unit\n"
                  "2026-10-04T12:00:00,130;0;1.5;V\n"
                  "2026-10-04T12:00:00,610;0.48;2.5;V\n"
                  "2026-10-04T12:00:01,090;0.96;nan;V\n"
                  "2026-10-04T12:00:01,570;1.44;4;mV\n",
          "raw: the file is\n" + text);

    const std::optional<Recording> rec = RecordingFile::read(raw1, &err);
    check(rec && rec->times.size() == 4 && rec->times[3] == 1440 && !rec->onGrid(), "raw: read with its times");
    RecordingStore back;
    if (rec)
      back.load(*rec);
    check(back.count() == 4 && back.series().at(1).t == 480 && back.series().at(2).gap(), "raw: loaded at its times");
    check(back.write(raw2, &err, true), "raw: write again: " + err);
    QFile f2(raw2);
    f2.open(QIODevice::ReadOnly);
    check(QString::fromUtf8(f2.readAll()) == text, "raw: export, import, export is not the same file");
    // on the grid of 0.5 s: the value at the start, then each step's mean
    QStringList g;
    for (const GridPoint &p : back.grid(5))
      g << QString::number(p.value);
    check(g.join(' ') == "1.5 1.54 2.5", "raw: grid of the import got " + g.join(' '));

    // an old export on its grid stays one: the gaps where they were
    const std::optional<Recording> legacy = RecordingFile::read(QString(argc > 1 ? argv[1] : "") + "/legacy_nan.txt", &err);
    check(legacy.has_value(), "legacy: read: " + err);
    if (legacy)
    {
      RecordingStore old;
      old.load(*legacy);
      const Recording out = old.toRecording();
      bool same = out.values.size() == legacy->values.size();
      for (int i = 0; same && i < out.values.size(); ++i)
        same = (std::isnan(out.values[i]) && std::isnan(legacy->values[i])) || out.values[i] == legacy->values[i];
      check(same, "legacy: the export of an old file has its values and gaps");
    }
  }

  // --- 5b. gaps and the integral: over time (V s) for the values above
  //         the threshold; a gap adds nothing, the integral carries over
  //         it; the start triggers compare with the last value there was ---
  {
    RecordingStore store;
    TestClock clock;
    clock.attach(store);
    store.setIntegrationThreshold(0);
    store.start();
    store.setReading(clock.at(0, reading(2, "2.000", "DC")));
    store.setReading(clock.at(1000, reading(4, "4.000", "DC")));
    check(store.series().last().integral == 2, QString("integral: 2 V for 1 s, got %1").arg(store.series().last().integral));
    store.setReading(clock.at(1500, reading(0, "OL", "DC")));
    check(std::isnan(store.series().last().integral), "integral: a gap has none");
    store.setReading(clock.at(3000, reading(6, "6.000", "DC")));
    check(store.series().last().integral == 4, QString("integral: carried over the gap, 2 + 4 x 0.5, got %1")
                                                  .arg(store.series().last().integral));
    store.setReading(clock.at(4000, reading(-1, "-1.000", "DC")));
    check(store.series().last().integral == 0, "integral: at or below the threshold it is 0");

    RecordingStore trig;
    TestClock tc;
    tc.attach(trig);
    trig.setStartMode(RecordingStore::Raising);
    trig.setThresholds(0, 1);
    trig.setReading(tc.at(0, reading(0, "0.000", "DC")));
    trig.setReading(tc.at(500, reading(0, "OL", "DC")));
    check(!trig.isRunning(), "gap: an OL crosses no threshold");
    trig.setReading(tc.at(1000, reading(2, "2.000", "DC")));
    check(trig.isRunning(), "gap: 0, OL, 2 crosses 1");
    check(trig.count() == 1 && trig.series().first().value == 2 && trig.series().first().t == 0,
          "trigger: the reading that crossed is the first point");
  }

  // --- 5c. one recording, one function: another port (V DC -> Ohm, DC ->
  //          AC) or unit (°C -> °F) stops it and says so; a prefix (mV ->
  //          V), an overload or hold do not. The samples keep their unit ---
  {
    auto rd = [](const QString &text, const QString &unit, const QString &special)
    {
      static ReadingAdapter adapter;
      return ReadingAdapter::reading(adapter.adaptValue(text.toDouble(), text, unit, special, "AUTO", false, true,
                                                        false, 0, QDateTime::currentMSecsSinceEpoch()));
    };
    RecordingStore store;
    store.setUnit("V");
    QSignalSpy changed(&store, &RecordingStore::functionChanged);
    store.start();
    store.setReading(rd("12.34", "mV", "DC"));
    store.setReading(rd("1.234", "V", "DC"));   // a prefix
    store.setReading(rd("OL", "V", "DC"));
    store.setReading(rd("OL", "", ""));         // an overload without a unit
    check(store.isRunning() && changed.isEmpty(), "function: mV -> V, OL: the recording goes on");

    store.setUnit("Ohm");                       // MeterController: unitChanged before the reading
    check(store.unit() == "V", "function: the samples keep their unit");
    store.setReading(rd("4.700", "kOhm", "OH"));
    check(!store.isRunning() && changed.size() == 1, "function: V DC -> Ohm stops the recording");
    if (!changed.isEmpty())
      check(changed[0][0].toString() == "Voltage DC (V)" && changed[0][1].toString() == "Resistance (Ω)",
            "function: says what changed, got " + changed[0][0].toString() + " -> " + changed[0][1].toString());
    check(store.count() == 4 && store.unit() == "V" && store.toRecording().unit == "V",
          QString("function: the stopped recording (%1 points) and its export stay in V").arg(store.count()));
    store.start();
    check(store.unit() == "Ohm", "function: the next recording is in Ohm");

    store.setUnit("V");
    store.start();
    store.setReading(rd("1.000", "V", "DC"));
    store.setReading(rd("1.000", "V", "AC"));
    check(!store.isRunning() && changed.size() == 2, "function: DC -> AC is another function");

    store.setUnit("C");
    store.start();
    store.setReading(rd("21.5", "C", "TE"));
    store.setReading(rd("70.7", "dF", "TE"));
    check(!store.isRunning() && changed.size() == 3, "function: °C -> °F (one port, two units) stops it");

    store.start();
    store.setReading(rd("21.5", "C", "TE"));
    store.stop();
    store.setReading(rd("1.000", "V", "DC"));
    check(changed.size() == 3, "function: no recording, nothing to stop");
  }

  // --- 2. the readings series: every reading of every value, whether or
  //        not a recording runs, with its own capacity and pause ---
  {
    RecordingStore store;
    int inserted = 0, removed = 0, cleared = 0;
    QObject::connect(&store, &RecordingStore::readingsInserted, [&] { ++inserted; });
    QObject::connect(&store, &RecordingStore::readingsRemoved, [&] { ++removed; });
    QObject::connect(&store, &RecordingStore::readingsCleared, [&] { ++cleared; });

    Reading second = reading(50, "50.00", "AC", false, "MANU");
    second.id = 1;
    store.setReading(reading(0.01234, "12.34", "DC", true));
    store.setReading(second);
    store.setReading(reading(0, "OL", "DC"));
    check(!store.isRunning() && store.count() == 0, "readings: no recording must be needed");
    check(store.readingCount() == 3 && inserted == 3,
          QString("readings: expected 3 rows, got %1").arg(store.readingCount()));
    const LoggedReading &first = store.readingAt(0);
    check(first.text == "12.34" && first.unit == "mV" && first.port.toString() == "voltage.dc" && first.range == "AUTO" &&
          first.hold() && first.value == 0.01234 && first.id == 0,
          "readings: the full tuple of the first row");
    check(store.readingAt(1).id == 1 && store.readingAt(1).range == "MANU", "readings: a secondary value is a row too");
    check(store.readingAt(2).quality == Quality::Overload, "readings: OL is quality Overload");

    // pause: the readings series stops, the recording does not
    store.setReadingsPaused(true);
    store.start();
    store.setReading(reading(1, "1.000", "DC"));
    check(store.readingCount() == 3, "readings: a paused series takes nothing");
    check(store.count() == 2, "readings: pausing the series must not stop the recording (start value and 1 V)");
    store.setReadingsPaused(false);
    // and the other way round: a stopped recording does not stop the series
    store.stop();
    store.setReading(reading(2, "2.000", "DC"));
    check(store.readingCount() == 4, "readings: a stopped recording must not stop the series");

    // capacity: the oldest go, one removal per change
    store.setReadingCapacity(3);
    check(store.readingCount() == 3 && removed == 1 && store.readingAt(0).id == 1,
          "readings: shrinking drops the oldest");
    store.setReading(reading(3, "3.000", "DC"));
    check(store.readingCount() == 3 && removed == 2 && store.readingAt(2).text == "3.000",
          "readings: a full series drops one for each new reading");
    store.setReadingCapacity(0);
    check(store.readingCapacity() == 1 && store.readingCount() == 1, "readings: capacity is at least 1");

    // marks
    store.markLastReading(0xffd82222u, "Alarm 1");
    check(store.readingAt(0).alarmArgb == 0xffd82222u && store.readingAt(0).alarmName == "Alarm 1",
          "readings: the newest row gets the mark");

    // clear: the recording is left alone
    store.setReadingCapacity(100);
    store.clearReadings();
    check(store.readingCount() == 0 && cleared == 1, "readings: clear");
    check(store.count() == 2, "readings: clearing the series must not clear the recording");
    store.clearReadings();
    check(cleared == 1, "readings: clearing an empty series says nothing");
    // logReading() feeds the series only, not the recording
    store.logReading(reading(5, "5.000", "AC"));
    check(store.readingCount() == 1, "readings: logReading");
  }

  if (failed)
  {
    qWarning() << failed << "RecordingStore check(s) failed";
    return 1;
  }
  qInfo() << "All RecordingStore tests passed.";
  return 0;
}
