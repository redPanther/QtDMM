// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTime>
#include <QVector>
#include <cmath>

#include "core/reading.h"
#include "recording/recordingfile.h"
#include "core/sampletypes.h"

/// One stored sample of the recorder.
///
/// Besides the value it keeps what the meter showed for it (the full tuple
/// of the core's Sample), so the same store can later feed the readings
/// table, SCPI and an export with mode and quality.
struct RecordedPoint
{
  qint64  msecs = 0;        ///< since the start of the recording
  double  value = 0;        ///< SI base units, the mean over the sample time; NaN = none (a gap)
  double  integral = 0;     ///< running sum above the integration threshold, raw; NaN in a gap
  Quality quality = Quality::Valid;   ///< worst over the sample time
  quint32 flags = 0;        ///< SampleFlag, of the newest reading in the sample time
  QString text;             ///< as the meter showed it ("-006.52", "OL")
  QString prefix;           ///< SI prefix of the unit as shown ("m")
  double  rangeFull = NAN;  ///< full scale of the range in SI, NaN = unknown
};

/// One row of the readings series: a reading as the meter delivered it,
/// whichever value it was and whether or not a recording runs.
struct LoggedReading
{
  qint64  when = 0;         ///< ms since the epoch, wall clock
  double  value = 0;        ///< SI base units; NaN for an overload
  Quality quality = Quality::Valid;
  quint32 flags = 0;        ///< SampleFlag
  QString text;             ///< as the meter showed it ("-006.52", "OL")
  QString unit;             ///< as shown, with the SI prefix ("mV")
  PortKey port;             ///< what was measured: voltage.dc, resistance, ...
  QString range;            ///< "AUTO", "MANU" or the meter's range text
  int     id = 0;           ///< 0 = main value, 1.. = secondary values
  quint32 alarmArgb = 0;    ///< an alarm raised on this reading (QColor::rgba()), 0 = none
  QString alarmName;

  bool    hold() const { return flags & SampleFlag::Hold; }
};

/// The recorder without a widget: the recorded curve and everything that
/// decides what goes into it.
///
/// It gets the current main value ten times a second (addValue(), the
/// MeterController's sample clock) and the readings themselves (setReading())
/// for their mode, text and quality. While recording it averages the values
/// over the sample time and keeps the samples in a ring of capacity()
/// entries; a full ring drops the oldest (shifted). Recording starts by hand,
/// at a clock time or when the value crosses a threshold (StartMode), and
/// stops by hand or after the recording length. The integral (running sum
/// above a threshold), the external program threshold and the alarm marks
/// live here too.
///
/// Next to the recording the store keeps a second series, the readings:
/// every reading of every value at full resolution, with its own capacity and
/// its own pause, independent of whether a recording runs (the readings
/// table). Its signals come in pairs around every change, so a table model
/// can turn them into begin/end calls one to one.
///
/// GraphWidget shows the store and follows its signals; it keeps only what is
/// a matter of display (window, zoom, scale, colours, the integral's scale
/// and offset). Only QtCore, so it can be tested on its own.
///
/// Times are in tenths of a second, like the settings: a sample time of 5
/// is one sample per 0.5 s.
class RecordingStore : public QObject
{
  Q_OBJECT
public:
  /// How recording is started; the same numbers as GraphWidget::SampleMode.
  enum StartMode
  {
    Manual = 0,   ///< start()
    Time,         ///< at setStartTime()
    Raising,      ///< when the value rises through the raising threshold
    Falling       ///< when the value falls through the falling threshold
  };

  /// An alarm mark at a sample.
  struct Mark
  {
    int     index;   ///< the sample, as for at()
    quint32 color;   ///< ARGB, as QColor::rgba()
    QString name;
  };

  explicit RecordingStore(QObject *parent = nullptr);

  /// @name Settings
  /// @{
  /// Sample time in tenths of a second (>= 1). The capacity is not
  /// recounted here: the graph sets it from its seconds (setCapacity()).
  void        setSampleTime(int tenths);
  int         sampleTime() const { return m_sampleTime; }
  /// Samples the ring holds. Growing keeps everything; shrinking below the
  /// stored count keeps the oldest capacity - 1 samples.
  void        setCapacity(int samples);
  int         capacity() const { return m_capacity; }
  /// Recording duration in tenths of a second after which recording stops
  /// on its own (0 = until stopped).
  void        setSampleLength(int tenths) { m_sampleLength = tenths; }
  void        setStartMode(StartMode mode) { m_mode = mode; }
  StartMode   startMode() const { return m_mode; }
  /// Clock time for StartMode::Time.
  void        setStartTime(const QTime &time) { m_startTime = time; }
  void        setThresholds(double falling, double raising);
  void        setFallingThreshold(double v) { m_fallingThreshold = v; }
  void        setRaisingThreshold(double v) { m_raisingThreshold = v; }
  double      fallingThreshold() const { return m_fallingThreshold; }
  double      raisingThreshold() const { return m_raisingThreshold; }
  /// External program: externalTriggered() once per recording when the
  /// value crosses @p threshold in the given direction.
  void        setExternal(bool on, bool falling, double threshold);
  void        setExternalThreshold(double v) { m_externalThreshold = v; }
  bool        externalOn() const { return m_startExternal; }
  double      externalThreshold() const { return m_externalThreshold; }
  /// Values at or below it reset the integral to 0.
  void        setIntegrationThreshold(double v) { m_integrationThreshold = v; }
  double      integrationThreshold() const { return m_integrationThreshold; }
  /// Unit of the recorded values, without SI prefix ("V").
  void        setUnit(const QString &baseUnit) { m_unit = baseUnit; }
  QString     unit() const { return m_unit; }
  /// A reading older than this (ms) makes the sample Stale.
  void        setStaleAfter(int ms) { m_staleMs = ms; }
  /// @}

  /// @name The recording
  /// @{
  int         count() const { return m_count; }
  /// Sample @p i, 0 = the oldest in the ring.
  const RecordedPoint &at(int i) const { return m_ring[(m_head + i) % m_capacity]; }
  const RecordedPoint &last() const { return at(m_count - 1); }
  /// The sample number of at(0) since the start: grows as the full ring
  /// drops its oldest, so a sample keeps its number while its index moves.
  qint64      firstSequence() const { return m_firstSeq; }
  /// When the recording started (for an import: the file's first time stamp).
  QDateTime   startDateTime() const { return m_start; }
  bool        isRunning() const { return m_running; }
  /// Recorded data not exported yet.
  bool        dirty() const { return m_dirty; }
  void        setDirty(bool dirty) { m_dirty = dirty; }
  /// Tenths of a second left until the recording length is reached.
  int         remainingLength() const { return m_remainingLength; }
  QList<Mark> marks() const;
  /// The values for the CSV/spreadsheet export.
  Recording   toRecording() const;
  /// Writes the recording (CSV, .xlsx or .ods by suffix); clears dirty().
  bool        write(const QString &path, QString *error = nullptr);
  /// Replaces the recording by @p rec: start, sample time and values (with
  /// quality Valid and no integral). The capacity has to be set for it
  /// first; it is widened when too small.
  void        load(const Recording &rec);
  /// @}

  /// @name The readings series
  /// @{
  int         readingCount() const { return m_readings.size(); }
  /// Reading @p i, 0 = the oldest.
  const LoggedReading &readingAt(int i) const { return m_readings.at(i); }
  /// Readings to keep (at least 1); the oldest go when the series shrinks.
  void        setReadingCapacity(int rows);
  int         readingCapacity() const { return m_readingCapacity; }
  /// Paused: readings pass by without being logged. The recording is not
  /// affected, nor does its state touch the pause.
  void        setReadingsPaused(bool paused) { m_readingsPaused = paused; }
  bool        readingsPaused() const { return m_readingsPaused; }
  /// An alarm raised on the newest reading: the row gets the colour.
  void        markLastReading(quint32 argb, const QString &name);
  /// @}

public Q_SLOTS:
  /// Adds @p reading to the readings series only (dropped while paused).
  void        logReading(const Reading &reading);
  /// Empties the readings series.
  void        clearReadings();
  /// The current main value, ten times a second.
  void        addValue(double value);
  /// A reading from the MeterController; the main value's (id 0) mode,
  /// text and overload go into the next sample.
  void        setReading(const Reading &reading);
  /// Clears the recording and starts it.
  void        start();
  void        stop();
  /// Discards the recorded samples and marks; a running recording goes on.
  void        clear();
  /// A mark at the newest sample.
  void        addMark(quint32 argb, const QString &name);

Q_SIGNALS:
  /// A sample was stored (last()); @p shifted: the oldest was dropped for
  /// it, all indices moved down by one.
  void        appended(bool shifted);
  /// The samples were discarded (clear(), start()).
  void        cleared();
  /// load() replaced the recording.
  void        loaded();
  void        runningChanged(bool running);
  /// Count, remaining length or running state changed (status bar).
  void        progressChanged();
  /// The external program threshold was crossed.
  void        externalTriggered();
  /// A mark came or went.
  void        marksChanged();
  /// A trigger started or the length stopped the recording: the UI beeps.
  void        alert();

  /// The readings series, before and after each change: rows @p first to
  /// @p last are about to come or go (the oldest ones when the series is full).
  void        readingsAboutToInsert(int first, int last);
  void        readingsInserted();
  void        readingsAboutToRemove(int first, int last);
  void        readingsRemoved();
  void        readingsAboutToClear();
  void        readingsCleared();
  /// Row @p row got an alarm mark.
  void        readingMarked(int row);

private:
  RecordedPoint &slot(int i) { return m_ring[(m_head + i) % m_capacity]; }
  /// The ring in order, oldest first, starting at index 0.
  void        linearize();

  QVector<RecordedPoint> m_ring;
  int         m_capacity = 3600;
  int         m_head = 0;       ///< ring index of the oldest sample
  int         m_count = 0;
  qint64      m_firstSeq = 0;   ///< sample number (since start) of at(0)
  QList<QPair<qint64, Mark>> m_marks;   ///< by sample number

  int         m_sampleTime = 1;
  int         m_sampleLength = 0;
  int         m_remainingLength = 0;
  int         m_sampleCounter = 0;
  double      m_sum = 0;
  int         m_sumCount = 0;   ///< the finite values in m_sum
  double      m_integral = 0;   ///< the running integral, carried over gaps
  bool        m_first = true;
  bool        m_running = false;
  bool        m_dirty = false;
  QDateTime   m_start;
  QString     m_unit;

  StartMode   m_mode = Manual;
  QTime       m_startTime;
  double      m_raisingThreshold = 0;
  double      m_fallingThreshold = 0;
  double      m_lastVal = 0;
  bool        m_lastValValid = false;
  bool        m_startExternal = false;
  bool        m_externalFalling = false;
  double      m_externalThreshold = 0;
  bool        m_externalStarted = false;
  double      m_integrationThreshold = 0;

  /// The newest main reading and the worst quality in the current period.
  Reading     m_reading;
  bool        m_haveReading = false;
  Quality     m_periodQuality = Quality::Valid;
  int         m_staleMs = 3000;
  Quality     currentQuality() const;

  QList<LoggedReading> m_readings;
  int         m_readingCapacity = 10000;
  bool        m_readingsPaused = false;
  void        removeOldestReadings(int count);
};
