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

  // --- 3. the limits: a recording keeps all of it, from its start (a
  //        recording longer than the old maximum length lost its start);
  //        beyond the most points the oldest go in steps (appended(true)
  //        says so), and the origin moves to what is left ---
  {
    RecordingStore store;
    TestClock clock;
    clock.attach(store);
    store.setLiveWidth(2);   // Live's width, nothing to do with a recording
    store.start();
    for (int i = 0; i < 100; i++)
      store.setReading(clock.at(i * 100, reading(i, QString::number(i), "DC")));
    check(store.count() == 100 && store.origin() == 0 && store.series().first().value == 0,
          QString("keeps all: %1 points, origin %2").arg(store.count()).arg(store.origin()));
  }
  {
    RecordingStore store;
    TestClock clock;
    clock.attach(store);
    store.setMaxPoints(20);
    QSignalSpy appended(&store, &RecordingStore::appended);
    store.start();
    for (int i = 0; i < 100; i++)
      store.setReading(clock.at(i * 100, reading(i, QString::number(i), "DC")));
    check(store.duration() == 9900 && store.origin() == store.series().first().t && store.origin() >= 7900,
          QString("limit: duration %1 origin %2").arg(store.duration()).arg(store.origin()));
    check(store.count() <= 20,
          QString("limit: %1 points from %2").arg(store.count()).arg(store.series().first().t));
    int shifts = 0;
    for (const auto &args : appended)
      shifts += args.first().toBool() ? 1 : 0;
    check(shifts > 0, QString("limit: %1 shifts").arg(shifts));
    check(store.series().last().value == 99, "limit: the newest is kept");
  }

  // --- 3b. a fast meter keeps every reading (50 a second); at the most a
  //         series keeps, the oldest go ---
  {
    RecordingStore store;
    TestClock clock;
    clock.attach(store);
    store.start();
    for (int i = 0; i < 100; i++)
      store.setReading(clock.at(i * 20, reading(i, QString::number(i), "DC")));
    check(store.count() == 100, QString("fast: %1 of 100 readings").arg(store.count()));

    Reading r = reading(1, "1.000", "DC");
    store.setReadingsPaused(true);   // the table's series is not what this is about
    for (int i = 100; i < RecordingStore::kMaxPoints + RecordingStore::kMaxPoints / 50; i++)
    {
      clock.now = r.t = i * 20;   // without the wall clock: QDateTime is slow
      store.setReading(r);
    }
    check(store.count() <= RecordingStore::kMaxPoints + RecordingStore::kMaxPoints / 100
            && store.count() >= RecordingStore::kMaxPoints && store.series().first().t > 0,
          QString("most: %1 readings kept, from %2 ms").arg(store.count()).arg(store.series().first().t));
  }

  // --- 3c. pre-trigger: a recording started by a threshold reaches back by
  //         the pre-trigger time, with the readings of that time; a mark
  //         shows the trigger, the recording length counts from it ---
  {
    RecordingStore store;
    TestClock clock;
    clock.attach(store);
    store.setStartMode(RecordingStore::Raising);
    store.setThresholds(0, 5);
    store.setPreTrigger(2000);
    store.setSampleLength(10);   // 1 s after the trigger
    store.setReadingsPaused(true);
    store.live();                // the triggers wait in Live
    for (int i = 0; i < 10; i++)   // 0 .. 4.5 s, below the threshold
      store.setReading(clock.at(i * 500, reading(1 + i * 0.1, "1", "DC")));
    store.setReading(clock.at(5000, reading(6, "6.000", "DC")));   // the trigger
    check(store.isRunning(), "pre-trigger: not started");
    QStringList t;
    for (int i = 0; i < store.count(); ++i)
      t << QString::number(store.series().at(i).t);
    check(t.join(' ') == "0 500 1000 1500 2000", "pre-trigger: points at " + t.join(' '));
    check(store.series().first().value == 1.6 && store.series().last().value == 6,
          QString("pre-trigger: from %1 to %2").arg(store.series().first().value).arg(store.series().last().value));
    check(store.startDateTime() == TestClock::wall(3000), "pre-trigger: the recording starts 2 s before the trigger");
    check(store.marks().size() == 1 && store.marks().first().t == 2000 && store.marks().first().color == RecordingStore::kTriggerColor,
          "pre-trigger: a mark at the trigger");
    store.setReading(clock.at(5500, reading(6, "6.000", "DC")));
    check(store.isRunning() && store.remainingLength() == 5, QString("pre-trigger: %1 tenths left").arg(store.remainingLength()));
    store.setReading(clock.at(6000, reading(6, "6.000", "DC")));
    check(!store.isRunning() && store.duration() == 3000, QString("pre-trigger: stopped after %1 ms").arg(store.duration()));

    // a value gone stale before the trigger is a gap in it
    RecordingStore gap;
    TestClock gc;
    gc.attach(gap);
    gap.setStartMode(RecordingStore::Falling);
    gap.setThresholds(0, 0);
    gap.setPreTrigger(10000);
    gap.setStaleAfter(1500);
    gap.live();
    gap.setReading(gc.at(0, reading(3, "3", "DC")));
    gc.now = 2000;
    gap.setStale(true);
    gap.setReading(gc.at(4000, reading(2, "2", "DC")));
    gap.setReading(gc.at(4500, reading(-1, "-1", "DC")));   // the trigger
    check(gap.count() == 4 && gap.series().at(1).gap() && gap.series().at(1).t == 1500,
          QString("pre-trigger: %1 points, the gap at %2").arg(gap.count()).arg(gap.series().at(1).t));

    // a manual start does not reach back
    RecordingStore manual;
    TestClock mc;
    mc.attach(manual);
    manual.setPreTrigger(2000);
    manual.setReading(mc.at(0, reading(1, "1", "DC")));
    manual.start();
    check(manual.count() == 1 && manual.marks().isEmpty(), "pre-trigger: not for a manual start");
  }

  // --- 4. marks by time: now while recording; a mark goes with the
  //        readings before what the store keeps ---
  {
    RecordingStore store;
    TestClock clock;
    clock.attach(store);
    store.setMaxPoints(20);
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
    check(f.open(QIODevice::ReadOnly | QIODevice::Text), "raw: cannot open file: " + raw1);
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
    check(f2.open(QIODevice::ReadOnly | QIODevice::Text), "raw: cannot open file: " + raw2);
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
    trig.live();
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

  // --- 5d. waiting for a trigger: another function is no crossing (0 V, then
  //          1000 Ohm over a threshold of 5), and the pre-trigger readings of
  //          the function before go ---
  {
    auto rd = [](const QString &text, const QString &unit, const QString &special)
    {
      static ReadingAdapter adapter;
      return ReadingAdapter::reading(adapter.adaptValue(text.toDouble(), text, unit, special, "AUTO", false, true,
                                                        false, 0, QDateTime::currentMSecsSinceEpoch()));
    };
    RecordingStore store;
    TestClock clock;
    clock.attach(store);
    store.setStartMode(RecordingStore::Raising);
    store.setThresholds(0, 5);
    store.setPreTrigger(2000);
    store.live();
    store.setReading(clock.at(0, rd("0.000", "V", "DC")));
    store.setReading(clock.at(500, rd("1000", "Ohm", "OH")));
    check(!store.isRunning(), "trigger: V -> Ohm crosses nothing");
    store.setReading(clock.at(1000, rd("2", "Ohm", "OH")));
    store.setReading(clock.at(1500, rd("6", "Ohm", "OH")));   // a crossing in Ohm
    check(store.isRunning(), "trigger: a crossing in the new function starts it");
    QStringList v;
    for (int i = 0; i < store.count(); ++i)
      v << QString::number(store.series().at(i).value);
    check(v.join(' ') == "1000 2 6", "trigger: only Ohm before the trigger, got " + v.join(' '));
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

  // --- 6. the states: View at first; Live runs through the live window and
  //        is never dirty; a recording starts from it cleared and ends in
  //        View; the triggers wait in Live only ---
  {
    RecordingStore store;
    TestClock clock;
    clock.attach(store);
    QList<RecordingStore::State> states;
    QObject::connect(&store, &RecordingStore::stateChanged, [&](RecordingStore::State st) { states << st; });
    QSignalSpy running(&store, &RecordingStore::runningChanged);
    check(store.state() == RecordingStore::View, "states: a new store views");
    store.setReading(clock.at(0, reading(1, "1", "DC")));
    check(store.count() == 0, "states: View records nothing");

    // the live window: the recording length, or the graph's window
    store.setLiveWidth(60);
    store.setSampleLength(20);   // 2 s
    check(store.liveWindow() == 2000, QString("live window: %1 ms, expected the length").arg(store.liveWindow()));
    store.setPreTrigger(5000);
    check(store.liveWindow() == 5000, "live window: at least the pre-trigger");
    store.setPreTrigger(0);
    store.setSampleLength(0);
    check(store.liveWindow() == 60000, "live window: until stopped - the graph's window");
    store.setSampleLength(20);

    clock.now = 100;
    store.live();
    check(store.state() == RecordingStore::Live && states == QList<RecordingStore::State>{ RecordingStore::Live },
          "live: Live, said once");
    check(running.isEmpty() && !store.isRunning(), "live: is no recording");
    check(store.count() == 1 && store.series().first().value == 1, "live: starts with the value shown");
    for (int i = 1; i <= 100; ++i)   // 10 s
      store.setReading(clock.at(100 + i * 100, reading(i, QString::number(i), "DC")));
    check(!store.dirty(), "live: nothing unsaved");
    check(store.duration() == 10000 && store.origin() == 8000,
          QString("live: covers %1 ms from %2 on").arg(store.duration()).arg(store.origin()));
    check(store.series().first().t <= 8000 && store.series().first().t >= 8000 - 1100,
          QString("live: keeps from %1 ms, the window and a step of slack").arg(store.series().first().t));
    check(store.remainingLength() == 0, "live: no length left to show");

    // saved while live: the window up to now, and still live
    const Recording rec = store.toRecording(true);
    check(!rec.values.isEmpty() && rec.values.last() == 100 && rec.start == TestClock::wall(100 + 8000),
          QString("live export: %1 values from %2").arg(rec.values.size()).arg(rec.start.toString("hh:mm:ss.zzz")));
    QTemporaryDir dir;
    check(store.write(dir.filePath("live.csv")) && store.state() == RecordingStore::Live,
          "live export: written, Live goes on");

    // another function in Live: the window starts anew, no recording stopped
    QSignalSpy changed(&store, &RecordingStore::functionChanged);
    static ReadingAdapter ohms;
    Reading ohm = ReadingAdapter::reading(ohms.adaptValue(1000, "1000", "Ohm", "OH", "AUTO", false, true, false, 0,
                                                          QDateTime::currentMSecsSinceEpoch()));
    store.setReading(clock.at(10200, ohm));
    check(store.state() == RecordingStore::Live && changed.isEmpty() && store.count() == 1
            && store.series().first().value == 1000,
          QString("live: another function starts the window anew, %1 points").arg(store.count()));

    // a recording starts cleared, from the value shown, and ends in View
    store.start();
    check(store.state() == RecordingStore::Record && store.isRunning() && running.size() == 1,
          "record: started");
    check(store.count() == 1 && store.series().first().t == 0, "record: cleared, the value shown first");
    store.setReading(clock.at(10700, ohm));
    check(store.dirty(), "record: unsaved");
    check(store.remainingLength() == 15, QString("record: %1 tenths left").arg(store.remainingLength()));
    store.setReading(clock.at(12300, ohm));   // past the length
    check(store.state() == RecordingStore::View && running.size() == 2 && store.dirty(),
          "record: the length ends it in View, unsaved");
    const int kept = store.count();
    store.setReading(clock.at(12500, ohm));
    check(store.count() == kept && store.duration() == 2000, "view: stands");
    store.stop();
    check(running.size() == 2, "view: stop says nothing");

    // back to Live: cleared, nothing unsaved
    store.live();
    check(store.state() == RecordingStore::Live && !store.dirty() && store.count() == 1 && store.marks().isEmpty(),
          "live again: cleared");
    check(states == QList<RecordingStore::State>({ RecordingStore::Live, RecordingStore::Record, RecordingStore::View,
                                                   RecordingStore::Live }),
          "states: Live, Record, View, Live");

    // stopped by hand: View, unsaved; a load views too
    store.start();
    store.setReading(clock.at(13000, ohm));
    store.stop();
    check(store.state() == RecordingStore::View && store.dirty(), "stop: View, unsaved");
    store.live();
    Recording file;
    file.start = TestClock::wall(0);
    file.sampleTimeTenths = 10;
    file.values = { 1, 2, 3 };
    store.load(file);
    check(store.state() == RecordingStore::View && !store.dirty() && store.count() == 3, "load: View");
  }

  // --- 6b. the triggers wait in Live, not in View: a recording viewed is
  //         not overwritten; the pre-trigger reaches into the live window ---
  {
    RecordingStore store;
    TestClock clock;
    clock.attach(store);
    store.setStartMode(RecordingStore::Raising);
    store.setThresholds(0, 5);
    store.setReading(clock.at(0, reading(1, "1", "DC")));
    store.setReading(clock.at(500, reading(6, "6", "DC")));
    check(!store.isRunning(), "trigger: not in View");
    store.setStartMode(RecordingStore::Time);
    store.setStartTime(TestClock::wall(1000).time());
    clock.now = 1000;
    store.poll();
    check(!store.isRunning(), "clock time: not in View");

    // Live: the crossing starts it; the pre-trigger takes the window's readings
    store.setStartMode(RecordingStore::Raising);
    store.setPreTrigger(1000);
    store.setSampleLength(100);   // 10 s
    clock.now = 2000;
    store.live();
    for (int i = 1; i <= 20; ++i)   // 2 s at 1 V
      store.setReading(clock.at(2000 + i * 100, reading(1, "1", "DC")));
    store.setReading(clock.at(4100, reading(6, "6", "DC")));
    check(store.isRunning() && store.marks().size() == 1 && store.marks().first().t == 1000,
          "pre-trigger from Live: started, the mark 1 s in");
    check(store.series().first().t == 0 && store.series().last().value == 6 && store.series().last().t == 1000,
          QString("pre-trigger from Live: %1 points, the crossing at %2 ms")
            .arg(store.count()).arg(store.series().last().t));
    check(store.startDateTime() == TestClock::wall(3100), "pre-trigger from Live: starts 1 s before the trigger");
    // the length counts from the trigger
    check(store.remainingLength() == 100, QString("pre-trigger from Live: %1 tenths left").arg(store.remainingLength()));
    // ended: no trigger until Live again
    store.stop();
    store.setReading(clock.at(5000, reading(1, "1", "DC")));
    store.setReading(clock.at(5100, reading(6, "6", "DC")));
    check(store.state() == RecordingStore::View, "trigger: not again in View after a recording");
  }

  if (failed)
  {
    qWarning() << failed << "RecordingStore check(s) failed";
    return 1;
  }
  qInfo() << "All RecordingStore tests passed.";
  return 0;
}
