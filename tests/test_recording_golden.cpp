// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The recorder against a stream of readings with given times, the way the
// MeterController feeds it: the export on the sample time's grid, the
// integral and the start triggers. Recorded with the store as it sampled
// the main value ten times a second; the store that keeps every reading has
// to give the same.
//
// QTDMM_WRITE_GOLDEN=<dir> writes the reference files there instead of
// comparing.
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>

#include "core/readingadapter.h"
#include "core/stalerule.h"
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

static QString readFile(const QString &path)
{
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// The MeterController around a store, on a clock of its own: readings
// arrive at given times (ms from the start of the stream), the controller's
// clock ticks every 100 ms in between. A reading at a tick's time comes just
// after the tick, the start just before it: then the ten samples a second
// are the time-weighted mean of the readings, and the store that keeps
// every reading has to give the same numbers.
class Player
{
public:
  explicit Player(RecordingStore &store) : m_store(store)
  {
    m_store.setClock([this] { return m_now; }, [this] { return wall(m_now); });
  }

  static QDateTime wall(qint64 t) { return QDateTime(QDate(2026, 10, 4), QTime(12, 0, 0)).addMSecs(t); }

  /// Runs the clock up to @p t, the tick at @p t included.
  void advance(qint64 t) { run(t, true); }

  /// A main reading at @p t: @p text "OL" is an overload, @p value in SI.
  void reading(qint64 t, double value, const QString &text, const QString &unit = "V", const QString &special = "DC")
  {
    advance(t);
    Reading r = ReadingAdapter::reading(m_adapter.adaptValue(value, text, unit, special, "AUTO", false, true, false, 0,
                                                             wall(t).toMSecsSinceEpoch()));
    if (!r.overload)
      r.value = value;
    r.t = t;
    // MeterController::publish()
    m_dval = r.value;
    m_stale.arrived(t + kOffset);
    m_store.setReading(r);
  }

  void start(qint64 t)
  {
    run(t, false);
    m_store.start();
    advance(t);
  }

  void stop(qint64 t)
  {
    advance(t);
    m_store.stop();
  }

  qint64 now() const { return m_now; }

private:
  void run(qint64 t, bool inclusive)
  {
    // the first tick after m_now (floor: the times may be negative)
    qint64 tick = (m_now >= 0 ? m_now / 100 : (m_now - 99) / 100) * 100;
    if (tick <= m_now && m_ticked >= tick)
      tick += 100;
    for (; inclusive ? tick <= t : tick < t; tick += 100)
    {
      m_now = tick;
      m_ticked = tick;
      // MeterController::timerEvent()
      m_store.setStaleAfter(int(m_stale.maxAgeMs()));
      m_store.addValue(m_stale.stale(tick + kOffset) ? qQNaN() : m_dval);
    }
    m_now = t;
  }

  // StaleRule takes a negative time for "nothing yet"
  static constexpr qint64 kOffset = 100000;

  RecordingStore &m_store;
  ReadingAdapter  m_adapter;
  StaleRule       m_stale;
  qint64          m_now = -2000;
  qint64          m_ticked = -2000;   ///< the last tick that ran
  double          m_dval = qQNaN();
};

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  const QString data = argc > 1 ? argv[1] : QString();
  const QString writeTo = qEnvironmentVariable("QTDMM_WRITE_GOLDEN");

  // --- 1. the export on the grid: 2 readings a second, a prefix change
  //        (mV -> V), an overload section, a pause longer than the stale
  //        limit; sample time 1 s ---
  {
    RecordingStore store;
    store.setCapacity(1000);
    store.setSampleTime(10);
    store.setUnit("V");
    Player p(store);
    p.reading(-500, 0.5, "500.0", "mV");   // before the start: the first sample's value
    p.start(0);
    int k = 0;
    for (qint64 t = 200; t < 10000; t += 500, ++k)   // a ramp through the range change
    {
      const double v = 0.5 + 0.06 * k;
      if (v < 1.0)
        p.reading(t, v, QString::number(v * 1000, 'f', 1), "mV");
      else
        p.reading(t, v, QString::number(v, 'f', 3), "V");
    }
    for (qint64 t = 10200; t < 13000; t += 500)
      p.reading(t, 0, "OL");
    for (qint64 t = 13200; t < 20000; t += 500)
      p.reading(t, 2.0 + std::sin(t / 1000.0), QString::number(2.0 + std::sin(t / 1000.0), 'f', 3));
    // nothing from 20 s to 28 s: stale after 3 intervals of 0.5 s
    for (qint64 t = 28200; t < 35000; t += 500)
      p.reading(t, 3.0 - (t - 28200) / 10000.0, QString::number(3.0 - (t - 28200) / 10000.0, 'f', 3));
    p.stop(35050);

    QTemporaryDir dir;
    const QString out = writeTo.isEmpty() ? dir.path() + "/grid.csv" : writeTo + "/golden_grid.csv";
    QString err;
    check(store.write(out, &err), "grid: write failed: " + err);
    if (writeTo.isEmpty())
    {
      const QString want = readFile(data + "/golden_grid.csv");
      const QString got = readFile(out);
      check(!want.isEmpty(), "grid: no reference file in " + data);
      check(got == want, "grid: the export differs from golden_grid.csv:\n" + got);
    }
  }

  // --- 2. the integral at a regular rate: one reading a second, sample time
  //        1 s, threshold 0.5; it falls back to 0 at or below it ---
  {
    RecordingStore store;
    store.setCapacity(1000);
    store.setSampleTime(10);
    store.setIntegrationThreshold(0.5);
    Player p(store);
    const double values[] = { 1, 2, 3, 0.25, 1, 1, 4 };
    // the value of sample k arrives at the start of its sample time
    p.reading(-1000, values[0], "1.000");
    p.start(0);
    for (int i = 1; i < 7; ++i)
      p.reading((i - 1) * 1000, values[i], QString::number(values[i], 'f', 3));
    p.stop(6050);
    QStringList got;
    for (int i = 0; i < store.count(); ++i)
      got << QString::number(store.at(i).integral);
    // per sample: the running sum of the values above the threshold
    check(got.join(' ') == "1 3 6 0 1 2 6", "integral: got " + got.join(' '));
  }

  // --- 3. start triggers: when the recording starts (ms of the stream) ---
  auto startedAt = [](RecordingStore &store) { return Player::wall(0).msecsTo(store.startDateTime()); };
  {
    RecordingStore store;
    store.setStartMode(RecordingStore::Raising);
    store.setThresholds(0, 1.0);
    Player p(store);
    p.reading(130, 0.2, "0.200");
    p.reading(630, 0.5, "0.500");
    p.advance(900);
    check(!store.isRunning(), "rising: started below the threshold");
    p.reading(1130, 1.2, "1.200");
    p.advance(1250);
    check(store.isRunning(), "rising: not started");
    // sampled ten times a second: on the next tick after the reading
    check(startedAt(store) >= 1130 && startedAt(store) <= 1230,
          QString("rising: started at %1 ms, the reading came at 1130").arg(startedAt(store)));
  }
  {
    RecordingStore store;
    store.setStartMode(RecordingStore::Falling);
    store.setThresholds(1.0, 0);
    Player p(store);
    p.reading(130, 2.0, "2.000");
    p.reading(630, 1.5, "1.500");
    p.reading(1130, 0.8, "0.800");
    p.advance(1250);
    check(store.isRunning() && startedAt(store) >= 1130 && startedAt(store) <= 1230,
          QString("falling: started at %1 ms, the reading came at 1130").arg(startedAt(store)));
  }
  {
    RecordingStore store;
    store.setStartMode(RecordingStore::Time);
    store.setStartTime(Player::wall(5000).time());
    Player p(store);
    for (qint64 t = 130; t < 8000; t += 500)
      p.reading(t, 1, "1.000");
    check(store.isRunning() && startedAt(store) >= 5000 && startedAt(store) < 7000,
          QString("clock time: started at %1 ms, expected from 5000 on").arg(startedAt(store)));
  }
  {
    RecordingStore store;
    store.setExternal(true, false, 2.0);
    int fired = 0;
    qint64 firedAt = -1;
    Player p(store);
    QObject::connect(&store, &RecordingStore::externalTriggered, [&] { ++fired; firedAt = p.now(); });
    p.reading(-470, 1.0, "1.000");
    p.start(0);
    p.reading(30, 1.5, "1.500");
    p.reading(530, 2.5, "2.500");
    p.reading(1030, 1.0, "1.000");
    p.reading(1530, 3.0, "3.000");
    p.advance(2000);
    check(fired == 1 && firedAt >= 530 && firedAt <= 630,
          QString("external: fired %1 times, at %2 ms, the crossing came at 530").arg(fired).arg(firedAt));
  }

  if (failed)
  {
    qWarning() << failed << "golden recording check(s) failed";
    return 1;
  }
  qInfo() << "All golden recording tests passed.";
  return 0;
}
