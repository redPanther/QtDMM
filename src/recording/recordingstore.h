// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QString>
#include <QTime>
#include <QVector>
#include <cmath>
#include <functional>

#include "core/reading.h"
#include "recording/recordingfile.h"
#include "recording/recordingseries.h"
#include "core/sampletypes.h"

/// A step of the grid: the recording at the sample time, for the export.
struct GridPoint
{
  qint64  t = 0;            ///< ms since the start of the recording, a multiple of the step
  double  value = 0;        ///< the time-weighted mean over the step (the value at t for the first); NaN = none
  double  integral = 0;     ///< the integral at t; NaN in a gap
  Quality quality = Quality::Valid;   ///< the worst in the step
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

/// The recorder without a widget: the recorded readings and everything that
/// decides what goes into them.
///
/// While recording it keeps every main reading (id 0) with its time, as a
/// RecordingSeries of RawPoint: setReading() from the MeterController. An
/// overload is a point without a value, and so is the moment the value went
/// stale (setStale()). The sample time is no longer the rate it records at
/// but the grid of the export: grid() gives the time-weighted mean of every
/// step. The integral is one over time (unit x s) of the values above the
/// integration threshold.
///
/// Recording starts by hand, at a clock time or when the value crosses a
/// threshold (StartMode), and stops by hand or after the recording length.
/// The store keeps the last maxDuration() of a recording (at most
/// kMaxPoints readings): older readings go (appended() says so). The
/// external program threshold and the alarm marks live here too.
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
/// Sample time and recording length are in tenths of a second, like the
/// settings: a sample time of 5 is a grid of 0.5 s.
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

  /// An alarm mark at a time of the recording.
  struct Mark
  {
    qint64  t;       ///< ms since the start of the recording
    quint32 color;   ///< ARGB, as QColor::rgba()
    QString name;
  };

  /// The most readings a series keeps; the oldest go beyond it.
  static constexpr int kMaxPoints = 2000000;
  /// The colour of the mark where a pre-triggered recording was triggered.
  static constexpr quint32 kTriggerColor = 0xff2e9b3a;

  explicit RecordingStore(QObject *parent = nullptr);

  /// @name Settings
  /// @{
  /// The grid of the export in tenths of a second (>= 1).
  void        setSampleTime(int tenths);
  int         sampleTime() const { return m_sampleTime; }
  /// How much of a recording the store keeps, in seconds (>= 1): older
  /// readings go.
  void        setMaxDuration(int seconds);
  int         maxDuration() const { return int(m_maxMs / 1000); }
  /// Recording duration in tenths of a second after which recording stops
  /// on its own (0 = until stopped).
  void        setSampleLength(int tenths) { m_sampleLength = tenths; }
  /// Pre-trigger in ms (0 = off): a recording started by a threshold
  /// (Raising, Falling) reaches back this far - the store keeps the
  /// readings of that time while it waits - and marks where the trigger
  /// came. The recording length counts from the trigger.
  void        setPreTrigger(int ms);
  int         preTrigger() const { return m_preMs; }
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
  /// Unit of the recorded values, without SI prefix ("V"). While the store
  /// holds samples their unit stays; a new one applies from the next
  /// start() or clear() on.
  void        setUnit(const QString &baseUnit);
  QString     unit() const { return m_unit; }
  /// How long a value holds before it is stale (ms, StaleRule): the gap
  /// setStale() adds starts this long after the last value.
  void        setStaleAfter(int ms) { m_staleMs = ms; }
  /// The clocks the store reads: a monotonic one in ms (the core clock,
  /// Sample::now()) and the wall clock. Tests set their own.
  void        setClock(std::function<qint64()> monotonic, std::function<QDateTime()> wall);
  /// @}

  /// @name The recording
  /// @{
  const RecordingSeries &series() const { return m_series; }
  /// The readings kept (series().count()).
  int         count() const { return m_series.count(); }
  /// ms since the start the recording covers: up to now while it runs, up to
  /// the stop, or up to the last reading of a loaded one.
  qint64      duration() const;
  /// The start of what the store still keeps (ms since the start of the
  /// recording): 0 until older readings had to go.
  qint64      origin() const;
  /// When the recording started (for an import: the file's first time stamp).
  QDateTime   startDateTime() const { return m_start; }
  bool        isRunning() const { return m_running; }
  /// Recorded data not exported yet.
  bool        dirty() const { return m_dirty; }
  void        setDirty(bool dirty) { m_dirty = dirty; }
  /// Tenths of a second left until the recording length is reached.
  int         remainingLength() const;
  QList<Mark> marks() const { return m_marks; }
  /// The recording on a grid of @p tenths (the sample time): step k is the
  /// time-weighted mean over the step that ends at k x step, step 0 the value
  /// at the start. A value holds until the next reading; a step without one
  /// is NaN. A loaded recording that is on the grid already comes back as it
  /// was.
  QVector<GridPoint> grid(int tenths) const;
  /// The values for the CSV/spreadsheet export: the grid of the sample time,
  /// or with @p raw every reading at its time (a gap a NaN of its own),
  /// starting at the first.
  Recording   toRecording(bool raw = false) const;
  /// Writes the recording (CSV, .xlsx or .ods by suffix), on the grid or
  /// with @p raw every reading; clears dirty().
  bool        write(const QString &path, QString *error = nullptr, bool raw = false);
  /// Replaces the recording by @p rec: start, sample time and values, each
  /// at its time. A recording on the grid of its sample time stays one: it
  /// exports as it came.
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
  /// A reading from the MeterController: every one goes into the readings
  /// series; the main value (id 0) into the recording, the triggers and the
  /// check that the recording keeps its function.
  void        setReading(const Reading &reading);
  /// The main value went stale (MeterController::staleChanged()): the
  /// recording gets a gap from where the last value stopped holding.
  void        setStale(bool stale);
  /// The clock: the start at a clock time and the recording length.
  /// A timer calls it every second.
  void        poll();
  /// Clears the recording and starts it.
  void        start();
  void        stop();
  /// Discards the recorded readings and marks; a running recording goes on.
  void        clear();
  /// A mark at the newest reading (now, while recording).
  void        addMark(quint32 argb, const QString &name);

Q_SIGNALS:
  /// A reading was stored (series().last()); @p shifted: older ones went
  /// for it, the indices moved down.
  void        appended(bool shifted);
  /// The readings were discarded (clear(), start()).
  void        cleared();
  /// load() replaced the recording.
  void        loaded();
  void        runningChanged(bool running);
  /// Duration, remaining length or running state changed (status bar).
  void        progressChanged();
  /// The external program threshold was crossed.
  void        externalTriggered();
  /// A mark came or went.
  void        marksChanged();
  /// A trigger started or the length stopped the recording: the UI beeps.
  void        alert();
  /// The meter measures something else than the recording now (@p from
  /// "Voltage DC (V)", @p to "Resistance (Ω)", or °F after °C), so the
  /// recording stopped: one recording never mixes two quantities or units.
  void        functionChanged(const QString &from, const QString &to);

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
  static QString describe(const PortKey &port, const QString &baseUnit);
  /// ms since the start of the recording, now.
  qint64      elapsed() const { return m_monotonic() - m_t0; }
  /// Adds a point with the integral up to it.
  void        appendPoint(RawPoint p);
  /// appendPoint(), then the oldest points beyond the limits go; the signals.
  void        append(const RawPoint &p);
  /// Drops what is older than the store keeps; true when anything went.
  bool        trim();
  /// The start triggers and the external threshold on a new main value.
  bool        trigger(double value);
  /// Stops at the recording length when @p t (ms since the start) reached it.
  bool        lengthReached(qint64 t);
  /// Starts a recording; with @p preMs it reaches back into the readings
  /// kept for the pre-trigger.
  void        begin(qint64 preMs);
  /// Keeps @p p (t on the monotonic clock) for the pre-trigger.
  void        bufferPre(const RawPoint &p);
  /// Whether readings are kept for a pre-trigger now.
  bool        preTriggerArmed() const { return !m_running && m_preMs > 0 && (m_mode == Raising || m_mode == Falling); }

  RecordingSeries m_series;
  qint64      m_t0 = 0;           ///< the monotonic clock at the start
  qint64      m_stopT = -1;       ///< ms since the start when it stopped; -1 = running or never ran
  qint64      m_maxMs = 3600 * 1000;
  int         m_loadedGrid = 0;   ///< load(): the points are the steps of this grid (tenths); 0 = readings
  QList<Mark> m_marks;
  QTimer      m_poll;

  int         m_sampleTime = 1;
  int         m_sampleLength = 0;
  int         m_preMs = 0;
  qint64      m_preUsed = 0;          ///< ms the current recording reaches back before its trigger
  QVector<RawPoint> m_preBuffer;      ///< readings for the pre-trigger, t on the monotonic clock
  double      m_integral = 0;   ///< the running integral, carried over gaps
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

  /// The newest main reading: the first value of a recording that starts.
  Reading     m_reading;
  bool        m_haveReading = false;
  QString     m_nextUnit;        ///< setUnit() while readings are held
  bool        m_unitPending = false;
  PortKey     m_recordPort;      ///< what this recording measures, from its first value
  QString     m_recordBaseUnit;  ///< and in which unit (°C or °F)
  int         m_staleMs = 3000;

  std::function<qint64()>    m_monotonic;
  std::function<QDateTime()> m_wall;

  QList<LoggedReading> m_readings;
  int         m_readingCapacity = 10000;
  bool        m_readingsPaused = false;
  void        removeOldestReadings(int count);
};
