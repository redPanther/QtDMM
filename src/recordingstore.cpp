// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "recordingstore.h"

RecordingStore::RecordingStore(QObject *parent) :
  QObject(parent),
  m_start(QDateTime::currentDateTime())
{
  m_ring.resize(m_capacity);
}

void RecordingStore::setSampleTime(int tenths)
{
  if (tenths >= 1)
    m_sampleTime = tenths;
}

void RecordingStore::linearize()
{
  if (m_head == 0)
    return;
  QVector<RecordedPoint> ring(m_ring.size());
  for (int i = 0; i < m_count; i++)
    ring[i] = at(i);
  m_ring.swap(ring);
  m_head = 0;
}

void RecordingStore::setCapacity(int samples)
{
  samples = qMax(1, samples);
  if (samples == m_capacity)
    return;
  linearize();
  m_ring.resize(samples);
  m_capacity = samples;
  // as the graph always did: a full ring keeps its oldest samples and makes
  // room for one more
  if (m_count >= m_capacity)
    m_count = m_capacity - 1;
}

void RecordingStore::setThresholds(double falling, double raising)
{
  m_fallingThreshold = falling;
  m_raisingThreshold = raising;
}

void RecordingStore::setExternal(bool on, bool falling, double threshold)
{
  m_startExternal = on;
  m_externalFalling = falling;
  m_externalThreshold = threshold;
}

QList<RecordingStore::Mark> RecordingStore::marks() const
{
  QList<Mark> list;
  for (const auto &m : m_marks)
  {
    Mark mark = m.second;
    mark.index = int(m.first - m_firstSeq);
    list << mark;
  }
  return list;
}

Recording RecordingStore::toRecording() const
{
  Recording rec;
  rec.start = m_start;
  rec.sampleTimeTenths = m_sampleTime;
  rec.unit = m_unit;
  rec.values.reserve(m_count);
  for (int i = 0; i < m_count; i++)
    rec.values << at(i).value;
  return rec;
}

bool RecordingStore::write(const QString &path, QString *error)
{
  if (!RecordingFile::writeAny(toRecording(), path, error))
    return false;
  m_dirty = false;
  return true;
}

void RecordingStore::load(const Recording &rec)
{
  m_start = rec.start;
  m_sampleTime = qMax(1, rec.sampleTimeTenths);
  const int cnt = int(rec.values.size());
  if (cnt > m_capacity)
    setCapacity(cnt + 1);
  m_head = 0;
  m_firstSeq = 0;
  m_marks.clear();
  for (int i = 0; i < cnt; i++)
  {
    RecordedPoint p;
    p.msecs = qint64(i) * m_sampleTime * 100;
    p.value = rec.values[i];
    m_ring[i] = p;
  }
  m_count = cnt;
  m_sampleCounter = cnt;
  m_dirty = false;
  Q_EMIT marksChanged();
  Q_EMIT loaded();
}

Quality RecordingStore::currentQuality() const
{
  if (!m_haveReading)
    return Quality::Stale;
  if (m_reading.overload)
    return Quality::Overload;
  if (QDateTime::currentMSecsSinceEpoch() - m_reading.msecs > m_staleMs)
    return Quality::Stale;
  return Quality::Valid;
}

void RecordingStore::setReading(const Reading &reading)
{
  logReading(reading);
  if (reading.id != 0)
    return;
  m_reading = reading;
  m_haveReading = true;
}

void RecordingStore::start()
{
  m_sampleCounter = 0;
  m_sum = 0;
  m_sumCount = 0;
  clear();
  m_running = true;
  m_remainingLength = m_sampleLength;
  m_start = QDateTime::currentDateTime();
  m_externalStarted = false;

  Q_EMIT progressChanged();
  Q_EMIT runningChanged(true);
}

void RecordingStore::stop()
{
  m_running = false;
  Q_EMIT progressChanged();
  Q_EMIT runningChanged(false);
}

void RecordingStore::clear()
{
  m_head = 0;
  m_count = 0;
  m_firstSeq = 0;
  const bool hadMarks = !m_marks.isEmpty();
  m_marks.clear();
  m_start = QDateTime::currentDateTime();
  m_first = true;
  m_dirty = false;
  m_periodQuality = Quality::Valid;
  Q_EMIT cleared();
  if (hadMarks)
    Q_EMIT marksChanged();
}

void RecordingStore::addMark(quint32 argb, const QString &name)
{
  // on the newest sample, the first one before anything was recorded
  const qint64 seq = qMax(m_firstSeq, m_firstSeq + m_count - 1);
  m_marks.append({ seq, Mark { 0, argb, name } });
  Q_EMIT marksChanged();
}

void RecordingStore::addValue(double val)
{
  if (!m_running)
  {
    bool trigger = false;
    if (m_mode == Time)
    {
      // a reading may miss the exact second
      const int diff = m_startTime.secsTo(QTime::currentTime());
      trigger = diff >= 0 && diff < 2;
    }
    else if (m_mode == Raising)
      trigger = m_lastValValid && m_lastVal < m_raisingThreshold && val >= m_raisingThreshold;
    else if (m_mode == Falling)
      trigger = m_lastValValid && m_lastVal > m_fallingThreshold && val <= m_fallingThreshold;
    if (trigger)
    {
      Q_EMIT alert();
      start();
    }
  }

  if (!m_externalStarted && m_running && m_startExternal)
  {
    const bool crossed = m_externalFalling ? m_lastVal > m_externalThreshold && val <= m_externalThreshold
                                           : m_lastVal < m_externalThreshold && val >= m_externalThreshold;
    if (crossed)
    {
      m_externalStarted = true;
      Q_EMIT externalTriggered();
    }
  }

  // a gap (NaN: overload, stale) crosses nothing: the triggers compare
  // with the last value there was
  if (std::isfinite(val))
  {
    m_lastValValid = true;
    m_lastVal = val;
  }

  if (!m_running)
    return;

  // the worst quality of the readings in this sample time
  const Quality q = currentQuality();
  if (q == Quality::Overload || (q == Quality::Stale && m_periodQuality == Quality::Valid))
    m_periodQuality = q;

  // the mean over the values there were; none at all is a gap
  if (std::isfinite(val))
  {
    m_sum += val;
    m_sumCount++;
  }

  if (0 == m_sampleCounter)
  {
    m_dirty = true;

    if (!m_first)
      val = m_sumCount > 0 ? m_sum / double(m_sumCount) : qQNaN();
    m_first = false;
    m_sum = 0.0;
    m_sumCount = 0;

    const bool shifted = m_count >= m_capacity;
    if (shifted)
    {
      // drop the oldest; the marks on it fall off with it
      m_head = (m_head + 1) % m_capacity;
      m_count--;
      m_firstSeq++;
      bool dropped = false;
      for (int i = m_marks.size() - 1; i >= 0; --i)
        if (m_marks[i].first < m_firstSeq)
        {
          m_marks.removeAt(i);
          dropped = true;
        }
      if (dropped)
        Q_EMIT marksChanged();
    }

    RecordedPoint p;
    p.msecs = (m_firstSeq + m_count) * m_sampleTime * 100;
    p.value = val;
    // integration: the running sum of the values above the threshold, back
    // to 0 at or below it (the first sample, too); a gap adds nothing and
    // has no point of its own
    if (m_count == 0)
      m_integral = 0.0;
    if (std::isfinite(val))
      m_integral = val <= m_integrationThreshold ? 0.0 : m_integral + val;
    p.integral = std::isfinite(val) ? m_integral : qQNaN();
    p.quality = m_periodQuality;
    // no value without a reason in the readings: the main value went stale
    if (!std::isfinite(val) && p.quality == Quality::Valid)
      p.quality = Quality::Stale;
    if (m_haveReading)
    {
      p.flags = m_reading.flags;
      p.text = m_reading.text;
      p.prefix = m_reading.prefix;
    }
    slot(m_count) = p;
    m_count++;
    m_periodQuality = Quality::Valid;

    Q_EMIT appended(shifted);
  }

  m_sampleCounter++;
  m_remainingLength = qMax(0, m_remainingLength - 1);

  if (m_sampleCounter == m_sampleTime)
  {
    m_sampleCounter = 0;
    Q_EMIT progressChanged();
  }

  if (0 == m_remainingLength && m_sampleLength != 0)
  {
    Q_EMIT alert();
    stop();
  }
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
