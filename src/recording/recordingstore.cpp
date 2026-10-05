// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "recording/recordingstore.h"

#include <QCoreApplication>

#include "core/siprefix.h"

RecordingStore::RecordingStore(QObject *parent) :
  QObject(parent),
  m_monotonic(&Sample::now),
  m_wall([] { return QDateTime::currentDateTime(); })
{
  m_start = m_wall();
  m_t0 = m_monotonic();
  m_poll.setInterval(1000);
  connect(&m_poll, &QTimer::timeout, this, &RecordingStore::poll);
  m_poll.start();
}

void RecordingStore::setClock(std::function<qint64()> monotonic, std::function<QDateTime()> wall)
{
  m_monotonic = std::move(monotonic);
  m_wall = std::move(wall);
}

void RecordingStore::setSampleTime(int tenths)
{
  if (tenths >= 1)
    m_sampleTime = tenths;
}

void RecordingStore::setMaxDuration(int seconds)
{
  m_maxMs = qint64(qMax(1, seconds)) * 1000;
  trim();
}

void RecordingStore::setThresholds(double falling, double raising)
{
  m_fallingThreshold = falling;
  m_raisingThreshold = raising;
}

qint64 RecordingStore::duration() const
{
  if (m_running)
    return elapsed();
  if (m_stopT >= 0)
    return m_stopT;
  return m_series.isEmpty() ? 0 : m_series.last().t;
}

qint64 RecordingStore::origin() const
{
  return qMax<qint64>(0, duration() - m_maxMs);
}

int RecordingStore::remainingLength() const
{
  if (m_sampleLength <= 0)
    return 0;
  return int(qMax<qint64>(0, m_sampleLength - (duration() - m_preUsed) / 100));
}

void RecordingStore::setPreTrigger(int ms)
{
  m_preMs = qMax(0, ms);
  if (m_preMs == 0)
    m_preBuffer.clear();
}

void RecordingStore::bufferPre(const RawPoint &p)
{
  if (!m_preBuffer.isEmpty() && p.gap() && m_preBuffer.last().gap())
    return;
  m_preBuffer.append(p);
  // what holds at the start of the pre-trigger time stays, older goes
  const qint64 from = p.t - m_preMs;
  int drop = 0;
  while (drop + 1 < m_preBuffer.size() && m_preBuffer[drop + 1].t <= from)
    drop++;
  drop = qMax(drop, int(m_preBuffer.size()) - kMaxPoints);
  if (drop > 0)
    m_preBuffer.remove(0, drop);
}

QVector<GridPoint> RecordingStore::grid(int tenths) const
{
  QVector<GridPoint> out;
  const int n = m_series.count();
  // a loaded file on this grid: its rows are the steps already (each one the
  // mean over the step before it), holding them on would shift them by one
  if (m_loadedGrid > 0 && m_loadedGrid == tenths)
  {
    out.reserve(n);
    for (int i = 0; i < n; ++i)
    {
      const RawPoint &p = m_series.at(i);
      out.append({ p.t, p.value, p.integral, p.quality });
    }
    return out;
  }

  const qint64 step = qint64(qMax(1, tenths)) * 100;
  const qint64 end = duration();
  const qint64 kFirst = (origin() + step - 1) / step;
  const qint64 kLast = end / step;
  if (kLast < kFirst)
    return out;
  out.reserve(int(qMin<qint64>(kLast - kFirst + 1, 10000000)));

  // the worse of two qualities: an overload says more than a gap
  auto worse = [](Quality a, Quality b)
  {
    if (a == Quality::Overload || b == Quality::Overload)
      return Quality::Overload;
    return a == Quality::Stale || b == Quality::Stale ? Quality::Stale : Quality::Valid;
  };
  // the value at the moment t: the last reading before it (one that comes
  // at t counts from t on), the first one at the start
  auto before = [&](qint64 t)
  {
    const int j = m_series.lowerBound(t);
    return j > 0 ? j - 1 : j < n && m_series.at(j).t == t ? j : -1;
  };
  // the integral at time t: the last point's, plus its value since then
  auto integralAt = [&](qint64 t)
  {
    const int j = before(t);
    if (j < 0 || m_series.at(j).gap())
      return qQNaN();
    const RawPoint &p = m_series.at(j);
    return p.value > m_integrationThreshold ? p.integral + p.value * double(t - p.t) / 1000.0 : p.integral;
  };

  for (qint64 k = kFirst; k <= kLast; ++k)
  {
    GridPoint g;
    g.t = k * step;
    g.integral = integralAt(g.t);
    if (k == 0)
    {
      // the first step is the value at the start
      const int j = before(0);
      g.value = j < 0 ? qQNaN() : m_series.at(j).value;
      g.quality = j < 0 ? Quality::Stale : m_series.at(j).quality;
    }
    else
    {
      // the mean over (t - step, t], each value weighted with how long it held
      const qint64 a = g.t - step, b = g.t;
      double sum = 0, held = 0;
      Quality q = Quality::Valid;
      int j = m_series.holding(a);
      if (j < 0)
      {
        q = Quality::Stale;   // before the first reading: no value
        j = 0;
      }
      for (; j < n && m_series.at(j).t < b; ++j)
      {
        const RawPoint &p = m_series.at(j);
        const qint64 until = j + 1 < n ? m_series.at(j + 1).t : qMax(end, p.t);
        const qint64 from = qMax(p.t, a), to = qMin(until, b);
        if (to <= from)
          continue;
        if (p.gap())
          q = worse(q, p.quality == Quality::Overload ? Quality::Overload : Quality::Stale);
        else
        {
          sum += p.value * double(to - from);
          held += double(to - from);
        }
      }
      g.value = held > 0 ? sum / held : qQNaN();
      // no value without a reason: the main value went stale
      g.quality = held > 0 || q != Quality::Valid ? q : Quality::Stale;
    }
    out.append(g);
  }
  return out;
}

Recording RecordingStore::toRecording(bool raw) const
{
  Recording rec;
  rec.sampleTimeTenths = m_sampleTime;
  rec.unit = m_unit;
  if (raw)
  {
    // from the reading that holds at the origin on
    const int n = m_series.count();
    const int first = qMax(0, m_series.holding(origin()));
    const qint64 t0 = first < n ? m_series.at(first).t : 0;
    rec.start = m_start.addMSecs(t0);
    rec.values.reserve(n - first);
    rec.times.reserve(n - first);
    for (int i = first; i < n; ++i)
    {
      rec.values << m_series.at(i).value;
      rec.times << m_series.at(i).t - t0;
    }
    return rec;
  }
  const QVector<GridPoint> steps = grid(m_sampleTime);
  rec.start = m_start.addMSecs(steps.isEmpty() ? 0 : steps.first().t);
  rec.values.reserve(steps.size());
  for (const GridPoint &g : steps)
    rec.values << g.value;
  return rec;
}

bool RecordingStore::write(const QString &path, QString *error, bool raw)
{
  if (!RecordingFile::writeAny(toRecording(raw), path, error))
    return false;
  m_dirty = false;
  return true;
}

void RecordingStore::load(const Recording &rec)
{
  m_running = false;
  m_start = rec.start;
  m_unit = rec.unit;
  m_unitPending = false;
  m_recordPort = PortKey();
  m_sampleTime = qMax(1, rec.sampleTimeTenths);
  m_series.clear();
  m_series.reserve(int(rec.values.size()));
  m_marks.clear();
  const bool onGrid = rec.onGrid();
  for (int i = 0; i < rec.values.size(); i++)
  {
    RawPoint p;
    p.t = onGrid ? qint64(i) * m_sampleTime * 100 : rec.timeAt(i);
    p.value = rec.values[i];
    p.quality = std::isfinite(p.value) ? Quality::Valid : Quality::Stale;
    appendPoint(p);
  }
  // a file on its own grid: each row is already the mean over the step
  // before it
  m_loadedGrid = onGrid ? m_sampleTime : 0;
  m_stopT = -1;
  // the store keeps all of it
  if (duration() > m_maxMs)
    m_maxMs = (duration() / 1000 + 1) * 1000;
  m_dirty = false;
  Q_EMIT marksChanged();
  Q_EMIT loaded();
}

// "Voltage DC (V)", "Temperature (°F)": what a recording measures, for the UI
QString RecordingStore::describe(const PortKey &port, const QString &baseUnit)
{
  QString text = Quantities::name(port.quantity);
  if (port.defining & (SampleFlag::AC | SampleFlag::DC))
    text += ' ' + couplingText(port.defining);
  const QString unit = SiPrefix::displayText(baseUnit);
  if (!unit.isEmpty())
    text += QString(" (%1)").arg(unit);
  return text;
}

void RecordingStore::setUnit(const QString &baseUnit)
{
  // the readings keep the unit they were recorded in: switching the meter
  // from V to Ohm after a recording must not relabel it
  if (count() > 0)
  {
    m_nextUnit = baseUnit;
    m_unitPending = baseUnit != m_unit;
  }
  else
  {
    m_unit = baseUnit;
    m_unitPending = false;
  }
}

void RecordingStore::setReading(const Reading &reading)
{
  logReading(reading);
  if (reading.id != 0)
    return;
  m_reading = reading;
  m_haveReading = true;
  const double value = reading.overload ? qQNaN() : reading.value;

  // one recording, one quantity: the first value says which; another port
  // (V DC -> Ohm, DC -> AC) or another unit (°C -> °F) stops it. A prefix
  // (mV -> V) is no change, and an overload or a value without a known
  // quantity says nothing about the function.
  if (m_running && !reading.overload && reading.port.quantity != Quantity::Unknown)
  {
    if (!m_recordPort.isValid())
    {
      m_recordPort = reading.port;
      m_recordBaseUnit = reading.baseUnit;
    }
    else if (reading.port != m_recordPort || reading.baseUnit != m_recordBaseUnit)
    {
      const QString from = describe(m_recordPort, m_recordBaseUnit);
      stop();
      Q_EMIT functionChanged(from, describe(reading.port, reading.baseUnit));
      return;
    }
  }

  // waiting for a trigger: another function crosses no threshold (0 V, then
  // 1000 Ohm), and the readings kept for the pre-trigger time were another one
  if (!m_running && !reading.overload && reading.port.quantity != Quantity::Unknown)
  {
    if (m_armedPort.isValid() && (reading.port != m_armedPort || reading.baseUnit != m_armedBaseUnit))
    {
      m_lastValValid = false;
      m_preBuffer.clear();
    }
    m_armedPort = reading.port;
    m_armedBaseUnit = reading.baseUnit;
  }

  if (preTriggerArmed())
  {
    RawPoint p;
    p.t = reading.t;
    p.value = value;
    p.quality = reading.overload ? Quality::Overload : Quality::Valid;
    p.flags = reading.flags;
    bufferPre(p);
  }

  // a trigger that starts the recording makes this reading its first value
  // (after the pre-trigger's)
  const bool wasRunning = m_running;
  if (trigger(value) || !wasRunning || !m_running)
    return;

  const qint64 t = reading.t - m_t0;
  if (lengthReached(t))
    return;
  RawPoint p;
  p.t = t;
  p.value = value;
  p.quality = reading.overload ? Quality::Overload : Quality::Valid;
  p.flags = reading.flags;
  append(p);
}

bool RecordingStore::trigger(double val)
{
  bool started = false;
  if (!m_running && (m_mode == Raising || m_mode == Falling))
  {
    const bool crossed = m_mode == Raising
                           ? m_lastValValid && m_lastVal < m_raisingThreshold && val >= m_raisingThreshold
                           : m_lastValValid && m_lastVal > m_fallingThreshold && val <= m_fallingThreshold;
    if (crossed)
    {
      Q_EMIT alert();
      begin(m_preMs);
      started = true;
    }
  }

  // a gap (NaN: overload) crosses nothing: the triggers compare with the
  // last value there was
  if (std::isfinite(val))
  {
    m_lastValValid = true;
    m_lastVal = val;
  }
  return started;
}

void RecordingStore::setStale(bool stale)
{
  if (stale && preTriggerArmed() && !m_preBuffer.isEmpty() && !m_preBuffer.last().gap())
  {
    RawPoint p;
    p.t = qMin(m_preBuffer.last().t + m_staleMs, qMax(m_preBuffer.last().t, m_monotonic()));
    p.value = qQNaN();
    p.quality = Quality::Stale;
    bufferPre(p);
    return;
  }
  if (!stale || !m_running || m_series.isEmpty() || m_series.last().gap())
    return;
  // the last value held until the stale limit, no longer
  RawPoint p;
  p.t = qMin(m_series.last().t + m_staleMs, qMax(m_series.last().t, elapsed()));
  p.value = qQNaN();
  p.quality = Quality::Stale;
  append(p);
}

void RecordingStore::poll()
{
  if (!m_running && m_mode == Time)
  {
    // a timer may miss the exact second
    const int diff = m_startTime.secsTo(m_wall().time());
    if (diff >= 0 && diff < 2)
    {
      Q_EMIT alert();
      start();
    }
  }
  if (m_running && !lengthReached(elapsed()))
    Q_EMIT progressChanged();
}

bool RecordingStore::lengthReached(qint64 t)
{
  // counted from the trigger, after the pre-trigger time
  const qint64 limit = qint64(m_sampleLength) * 100 + m_preUsed;
  if (!m_running || m_sampleLength <= 0 || t < limit)
    return false;
  Q_EMIT alert();
  stop();
  m_stopT = limit;
  return true;
}

void RecordingStore::start()
{
  begin(0);
}

void RecordingStore::begin(qint64 preMs)
{
  const QVector<RawPoint> pre = preMs > 0 ? m_preBuffer : QVector<RawPoint>();
  m_preBuffer.clear();
  clear();
  m_running = true;
  m_stopT = -1;
  m_preUsed = 0;

  if (!pre.isEmpty())
  {
    // the recording reaches back by the pre-trigger time, or as far as
    // there were readings
    const qint64 back = qBound<qint64>(0, m_t0 - pre.first().t, preMs);
    m_t0 -= back;
    m_start = m_start.addMSecs(-back);
    m_preUsed = back;
    for (RawPoint p : pre)
    {
      p.t = qMax<qint64>(0, p.t - m_t0);
      append(p);
    }
    m_marks.append(Mark { back, kTriggerColor, QCoreApplication::translate("RecordingStore", "Trigger") });
    Q_EMIT marksChanged();
  }
  // the first value is the one the meter shows at the start
  else if (m_haveReading && m_monotonic() - m_reading.t <= m_staleMs)
  {
    RawPoint p;
    p.value = m_reading.overload ? qQNaN() : m_reading.value;
    p.quality = m_reading.overload ? Quality::Overload : Quality::Valid;
    p.flags = m_reading.flags;
    append(p);
  }

  Q_EMIT progressChanged();
  Q_EMIT runningChanged(true);
}

void RecordingStore::stop()
{
  if (m_running)
    m_stopT = elapsed();
  m_running = false;
  Q_EMIT progressChanged();
  Q_EMIT runningChanged(false);
}

void RecordingStore::clear()
{
  m_series.clear();
  m_recordPort = PortKey();
  m_recordBaseUnit.clear();
  if (m_unitPending)
  {
    m_unit = m_nextUnit;
    m_unitPending = false;
  }
  const bool hadMarks = !m_marks.isEmpty();
  m_marks.clear();
  m_t0 = m_monotonic();
  m_start = m_wall();
  m_stopT = m_running ? -1 : 0;
  m_loadedGrid = 0;
  m_integral = 0;
  m_dirty = false;
  Q_EMIT cleared();
  if (hadMarks)
    Q_EMIT marksChanged();
}

void RecordingStore::addMark(quint32 argb, const QString &name)
{
  // now while recording; else on the newest reading
  const qint64 t = m_running ? elapsed() : m_series.isEmpty() ? 0 : m_series.last().t;
  m_marks.append(Mark { t, argb, name });
  Q_EMIT marksChanged();
}

void RecordingStore::appendPoint(RawPoint p)
{
  if (!m_series.isEmpty())
  {
    const RawPoint &prev = m_series.last();
    p.t = qMax(p.t, prev.t);
    // integration: the values above the threshold over the time they held;
    // at or below it the integral is back to 0 (the first value, too), a
    // gap adds nothing and the integral carries over it
    if (!prev.gap() && prev.value > m_integrationThreshold)
      m_integral += prev.value * double(p.t - prev.t) / 1000.0;
  }
  else
    m_integral = 0;
  if (!p.gap() && p.value <= m_integrationThreshold)
    m_integral = 0;
  p.integral = p.gap() ? qQNaN() : m_integral;
  m_series.append(p);
}

void RecordingStore::append(const RawPoint &p)
{
  appendPoint(p);
  m_dirty = true;
  Q_EMIT appended(trim());
}

bool RecordingStore::trim()
{
  if (m_series.isEmpty())
    return false;
  // what holds at the origin stays, everything before it goes; in steps of
  // a twentieth, so a full store does not drop something with every reading
  const qint64 from = origin();
  const qint64 slack = qMax<qint64>(1000, m_maxMs / 20);
  int drop = 0;
  if (m_series.count() > 1 && m_series.at(1).t < from - slack)
    drop = qMax(0, m_series.holding(from));
  if (m_series.count() - drop > kMaxPoints + kMaxPoints / 100)
    drop = m_series.count() - kMaxPoints;
  if (drop <= 0)
    return false;
  m_series.removeFirst(drop);

  // the marks before what is kept fall off
  const qint64 first = qMax(from, m_series.isEmpty() ? from : m_series.first().t);
  bool dropped = false;
  for (int i = int(m_marks.size()) - 1; i >= 0; --i)
    if (m_marks[i].t < first)
    {
      m_marks.removeAt(i);
      dropped = true;
    }
  if (dropped)
    Q_EMIT marksChanged();
  return true;
}

void RecordingStore::logReading(const Reading &reading)
{
  // a secondary display that is off (an empty second value) is no reading
  if (m_readingsPaused || (reading.id > 0 && reading.text.isEmpty()))
    return;
  if (m_readings.size() >= m_readingCapacity)
    removeOldestReadings(m_readings.size() - m_readingCapacity + 1);

  LoggedReading row;
  row.when = reading.msecs;
  row.value = reading.value;
  row.quality = reading.overload ? Quality::Overload : Quality::Valid;
  row.flags = reading.flags;
  row.text = reading.text;
  row.unit = reading.unit;
  row.port = reading.port;
  row.range = reading.range;
  row.id = reading.id;

  const int at = m_readings.size();
  Q_EMIT readingsAboutToInsert(at, at);
  m_readings.append(row);
  Q_EMIT readingsInserted();
}

void RecordingStore::removeOldestReadings(int count)
{
  Q_EMIT readingsAboutToRemove(0, count - 1);
  m_readings.remove(0, count);
  Q_EMIT readingsRemoved();
}

void RecordingStore::setReadingCapacity(int rows)
{
  m_readingCapacity = qMax(1, rows);
  if (m_readings.size() > m_readingCapacity)
    removeOldestReadings(m_readings.size() - m_readingCapacity);
}

void RecordingStore::clearReadings()
{
  if (m_readings.isEmpty())
    return;
  Q_EMIT readingsAboutToClear();
  m_readings.clear();
  Q_EMIT readingsCleared();
}

void RecordingStore::markLastReading(quint32 argb, const QString &name)
{
  if (m_readings.isEmpty())
    return;
  const int row = m_readings.size() - 1;
  m_readings[row].alarmArgb = argb;
  m_readings[row].alarmName = name;
  Q_EMIT readingMarked(row);
}
