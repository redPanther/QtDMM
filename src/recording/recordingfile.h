// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QVector>
#include <optional>

/// What a value is in a row of a recording file (the column `status`).
enum class CellState : quint8
{
  Ok,                 ///< a number (`ok`)
  Overload,           ///< the meter showed OL (`OL`)
  NegativeOverload,   ///< the meter showed -OL (`-OL`)
  Gap,                ///< no value: stale, off or ended (`gap`)
  NoValue,            ///< the meter showed no number, "----" (`none`)
  Fault,              ///< the meter showed a fault word, "FUSE" (`fault`)
  None                ///< the value is not there yet in this row (empty cells)
};

/// Another value of the meter in a recording (a second display): its
/// unit and, row by row as Recording::values, its values and states.
struct RecordingColumn
{
  int id = 0;                 ///< the value (1 = value 2 ...); 0 = by its place
  QString unit;               ///< without SI prefix
  QVector<double> values;     ///< NaN for anything but Ok
  QVector<CellState> states;  ///< one per row
};

/// A recording as it goes to and comes from a CSV file: when it started, how
/// often it sampled, its unit and the values in that unit - on the grid of
/// the sample time, or each at its own time (times) - and the meter's other
/// values on the same rows.
struct Recording
{
  QDateTime start;
  int sampleTimeTenths = 1;   ///< sample interval in tenths of a second (>= 1)
  QString unit;               ///< the graph's unit, without SI prefix ("V")
  QVector<double> values;
  /// The state of each value; empty = Ok where the value is a number, else
  /// Gap. An overload's NaN carries the sign of -OL.
  QVector<CellState> states;
  /// ms since start for each value; empty = value i at i x the sample time
  QVector<qint64> times;
  /// The other values, a column group each (value 2, value 3 ...).
  QList<RecordingColumn> extra;

  /// The time of value @p i, ms since start.
  qint64 timeAt(int i) const { return times.isEmpty() ? qint64(i) * sampleTimeTenths * 100 : times[i]; }
  CellState stateAt(int i) const
  {
    if (!states.isEmpty())
      return states[i];
    return values[i] == values[i] ? CellState::Ok : CellState::Gap;   // NaN, without <cmath>
  }
  /// Whether the values lie on the grid of the sample time (to the ms the
  /// files keep).
  bool onGrid() const
  {
    for (int i = 0; i < times.size(); ++i)
      if (qAbs(times[i] - qint64(i) * sampleTimeTenths * 100) > 1)
        return false;
    return true;
  }
};

/// The CSV file formats of the recorder, free of widgets and settings so they
/// can be tested on their own. GraphWidget hands a Recording in and out.
///
/// Written: `timestamp;time (s);value;unit;status` with ISO 8601 timestamps
/// (`2026-09-21T14:03:05,250`), the value with an SI prefix on the unit, the
/// status `ok`, `OL`, `-OL` or `gap` (the value then `nan`). Each other value
/// adds `;value 2;unit 2;status 2` and so on; its cells are empty in the rows
/// before it began.
/// Read: that format, the one before it without status
/// (`timestamp;time (s);value;unit`, `nan` a gap), and the legacy
/// tab-separated export of QtDMM < 0.9
/// (`21.09.2026<TAB>14:03:05[:250]<TAB>value<TAB>unit`). The sample time is
/// derived from the time span and the row count; a CSV row keeps its time
/// (Recording::times).
namespace RecordingFile
{
/// Reads @p path; on failure returns nothing and sets @p error (translated).
std::optional<Recording> read(const QString &path, QString *error = nullptr);
/// Writes @p recording to @p path; false with @p error on failure or when
/// there is nothing to write.
bool write(const Recording &recording, const QString &path, QString *error = nullptr);
/// The same columns as an Excel (.xlsx) or OpenDocument (.ods) sheet, with
/// real date and number cells (SpreadsheetWriter); the format follows the
/// suffix. Falls back to CSV for any other suffix.
bool writeAny(const Recording &recording, const QString &path, QString *error = nullptr);

/// Factor that brings a value in @p unit to @p baseUnit: 1e-3 for "mV" and
/// "V", 1.0 when the units are the same or unrelated. Older exports wrote the
/// ASCII "u" for micro; SiPrefix::factor() takes both.
double unitScaleFactor(const QString &unit, const QString &baseUnit);
}
