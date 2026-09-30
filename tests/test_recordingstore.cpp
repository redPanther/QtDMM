// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// RecordingStore on its own: QCoreApplication only, no widgets. The
// recorder's behaviour (triggers, averaging, integral, length) is covered
// through DMMGraph in test_graph (5c-5i); this is what the store adds:
// the full sample (quality, flags, text), the ring, marks by sample, load.
#include <QCoreApplication>
#include <QDebug>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "recordingstore.h"

static int failed = 0;

static void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAILED:" << what;
    failed++;
  }
}

static Reading reading(double value, const QString &text, const QString &special, bool hold = false,
                       const QString &range = "AUTO")
{
  Reading r;
  r.value = value;
  r.text = text;
  r.unit = "mV";
  r.prefix = "m";
  r.baseUnit = "V";
  r.special = special;
  r.range = range;
  r.hold = hold;
  r.overload = text.contains(QLatin1String("OL"));
  r.msecs = QDateTime::currentMSecsSinceEpoch();
  return r;
}

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);

  // --- 1. the full sample: text, prefix and flags of the newest main
  //        reading, quality Valid; secondary readings (id != 0) are ignored
  {
    RecordingStore store;
    store.setCapacity(100);
    store.start();
    store.setReading(reading(0.01234, "12.34", "DC"));
    Reading second = reading(50, "50.00", "AC", false, "MANU");
    second.id = 1;
    store.setReading(second);
    store.addValue(0.01234);
    check(store.count() == 1, "sample: not stored");
    const RecordedPoint &p = store.at(0);
    check(p.text == "12.34" && p.prefix == "m", QString("sample: text '%1' prefix '%2'").arg(p.text, p.prefix));
    check(p.flags == (SampleFlag::DC | SampleFlag::Autorange), QString("sample: flags 0x%1").arg(p.flags, 0, 16));
    check(p.quality == Quality::Valid, "sample: quality not Valid");
    check(qIsNaN(p.rangeFull), "sample: range full scale should be unknown (NaN)");

    store.setReading(reading(0.005, "5.000", "ACDC", true, "MANU"));
    store.addValue(0.005);
    check(store.last().flags == (SampleFlag::AC | SampleFlag::DC | SampleFlag::Hold),
          QString("sample: ACDC + hold flags 0x%1").arg(store.last().flags, 0, 16));
    store.setReading(reading(0.6, "0.600", "DI"));
    store.addValue(0.6);
    check(store.last().flags == (SampleFlag::Diode | SampleFlag::Autorange), "sample: diode flag");
  }

  // --- 2. quality: overload anywhere in the sample time makes the sample
  //        Overload; no reading at all is Stale; a stale reading too ---
  {
    RecordingStore store;
    store.setCapacity(100);
    store.setSampleTime(3);
    store.start();
    store.addValue(1);   // first sample, no reading yet
    check(store.last().quality == Quality::Stale, "quality: no reading should be Stale");

    store.setReading(reading(1, "1.000", "DC"));
    store.addValue(1);
    store.setReading(reading(0, "OL", "DC"));
    store.addValue(1);
    store.setReading(reading(1, "1.000", "DC"));
    store.addValue(1);   // closes the period that saw the OL
    check(store.count() == 2 && store.last().quality == Quality::Overload,
          "quality: an OL in the sample time should make it Overload");
    store.addValue(1);
    store.addValue(1);
    store.addValue(1);
    check(store.count() == 3 && store.last().quality == Quality::Valid, "quality: back to Valid after the OL");

    Reading old = reading(1, "1.000", "DC");
    old.msecs -= 10000;
    store.setReading(old);
    store.addValue(1);
    store.addValue(1);
    store.addValue(1);
    check(store.last().quality == Quality::Stale, "quality: a reading older than the timeout should be Stale");
  }

  // --- 3. the ring: time stamps go on counting past the wrap, at(0) is the
  //        oldest, nothing is copied (the shift signal says so) ---
  {
    RecordingStore store;
    store.setCapacity(4);
    store.setSampleTime(5);   // 0.5 s
    QSignalSpy appended(&store, &RecordingStore::appended);
    store.start();
    for (int i = 0; i < 6 * 5; i++)
      store.addValue(i / 5);   // one value per sample: 0,0,0,0,0,1,1,...
    check(store.count() == 4, QString("ring: count %1, expected 4").arg(store.count()));
    QStringList ms, vals;
    for (int i = 0; i < store.count(); i++)
    {
      ms << QString::number(store.at(i).msecs);
      vals << QString::number(store.at(i).value);
    }
    check(ms.join(' ') == "1000 1500 2000 2500", QString("ring: msecs '%1'").arg(ms.join(' ')));
    check(vals.join(' ') == "1.2 2.2 3.2 4.2", QString("ring: values '%1'").arg(vals.join(' ')));
    int shifts = 0;
    for (const auto &args : appended)
      shifts += args.first().toBool() ? 1 : 0;
    check(appended.size() == 6 && shifts == 2, QString("ring: %1 appended, %2 shifted").arg(appended.size()).arg(shifts));

    // growing keeps the samples in order
    store.setCapacity(10);
    check(store.count() == 4 && store.at(0).msecs == 1000 && store.last().msecs == 2500, "ring: grow lost the order");
  }

  // --- 4. marks by sample: the index follows the ring, the mark falls off
  //        with its sample ---
  {
    RecordingStore store;
    store.setCapacity(3);
    store.start();
    store.addValue(1);
    store.addValue(2);
    store.addMark(0xffff0000u, "alarm");
    check(store.marks().size() == 1 && store.marks().first().index == 1, "marks: not on the newest sample");
    store.addValue(3);
    store.addValue(4);   // shift
    check(store.marks().value(0).index == 0, "marks: index did not follow the shift");
    QSignalSpy changed(&store, &RecordingStore::marksChanged);
    store.addValue(5);   // its sample leaves
    check(store.marks().isEmpty() && changed.size() == 1, "marks: did not fall off with its sample");
  }

  // --- 5. load and write: an import replaces the samples, a write clears
  //        dirty, the unit and start time go with the recording ---
  {
    Recording rec;
    rec.start = QDateTime(QDate(2026, 9, 30), QTime(21, 0, 0));
    rec.sampleTimeTenths = 10;
    rec.unit = "V";
    rec.values = { 1.0, 2.0, 3.0 };

    RecordingStore store;
    store.setCapacity(2);   // too small: load widens it
    store.setUnit("V");
    store.load(rec);
    check(store.count() == 3 && store.last().value == 3.0 && store.last().msecs == 2000, "load: samples");
    check(store.sampleTime() == 10 && store.startDateTime() == rec.start, "load: sample time or start");
    check(!store.dirty(), "load: an import is not unsaved data");

    store.start();
    store.addValue(7);
    check(store.dirty(), "write: recording should be dirty");
    QTemporaryDir dir;
    QString err;
    check(store.write(dir.path() + "/r.csv", &err), "write: " + err);
    check(!store.dirty(), "write: still dirty after writing");
    const Recording back = store.toRecording();
    check(back.unit == "V" && back.values.size() == 1 && back.values.first() == 7, "write: recording content");
  }

  // --- 2. the readings series: every reading of every value, whether or
  //        not a recording runs, with its own capacity and pause ---
  {
    RecordingStore store;
    store.setCapacity(100);
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
    check(first.text == "12.34" && first.unit == "mV" && first.special == "DC" && first.range == "AUTO" &&
          first.hold() && first.value == 0.01234 && first.id == 0,
          "readings: the full tuple of the first row");
    check(store.readingAt(1).id == 1 && store.readingAt(1).range == "MANU", "readings: a secondary value is a row too");
    check(store.readingAt(2).quality == Quality::Overload, "readings: OL is quality Overload");

    // pause: the readings series stops, the recording does not
    store.setReadingsPaused(true);
    store.start();
    store.setReading(reading(1, "1.000", "DC"));
    store.addValue(1);
    check(store.readingCount() == 3, "readings: a paused series takes nothing");
    check(store.count() == 1, "readings: pausing the series must not stop the recording");
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
    check(store.count() == 1, "readings: clearing the series must not clear the recording");
    store.clearReadings();
    check(cleared == 1, "readings: clearing an empty series says nothing");
    // logReading() feeds the series only, not the recording's sample
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
