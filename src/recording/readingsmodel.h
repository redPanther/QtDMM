// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractTableModel>
#include <QColor>
#include <QDateTime>
#include <QVector>

#include "core/sampletypes.h"

/// Every reading the meter sent, one row each, as a table model: the raw
/// protocol of a session next to the recorder's time-gridded graph. A thin
/// model over the readings series of a RecordingStore (the newest maxRows()
/// readings, a ring): it holds no rows of its own, knows min/max/mean of
/// what the store holds and writes itself as CSV. Without setStore() it uses
/// a store of its own. No widgets here, so it can be tested; ReadingsWidget
/// shows it.
struct Reading;
class RecordingStore;

class ReadingsModel : public QAbstractTableModel
{
  Q_OBJECT
public:
  /// One reading as MeterConnection::value() delivered it.
  struct Entry
  {
    QDateTime when;
    double dval = 0;      ///< SI base units, for statistics and sorting
    QString val;          ///< as displayed ("1.234", "OL")
    QString unit;         ///< with prefix ("mV")
    PortKey port;         ///< what was measured: voltage.dc, resistance, ...
    quint32 flags = 0;    ///< SampleFlag: AC, DC, Diode, Hold, ...
    QString range;        ///< "AUTO", "MANU" or empty
    bool hold = false;
    int id = 0;           ///< 0 = main display, 1+ = secondary values
    QColor alarmColor;    ///< set when an alarm raised on this reading
    QString alarmName;
  };

  enum Column { Time, Value, Unit, Mode, Range, Hold, ColumnCount };
  /// Qt::UserRole on any cell gives the row's dval as double.
  static constexpr int DvalRole = Qt::UserRole;

  /// Statistics over the numeric (non-overload) main readings in the log.
  struct Stats
  {
    int count = 0;        ///< rows in the log, overloads included
    int numeric = 0;      ///< rows min/max/mean are computed from: the main readings of the newest one's
                          ///< function (port and unit), without overloads
    double min = 0, max = 0, mean = 0;
    QString unit;         ///< unit of the newest main reading, prefix stripped
  };

  explicit ReadingsModel(QObject *parent = nullptr);

  /// Shows the readings of @p store (the MeterController's recorder). The
  /// row limit and the pause are the store's: set maxRows() after this. Not
  /// owned; should it be deleted first, the log shows its own empty store again.
  void setStore(RecordingStore *store);
  RecordingStore *store() const { return m_store; }

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

  /// Appends a row; the oldest one goes when the log is full. Dropped
  /// while paused.
  void append(const Entry &entry);
  /// Paused: readings pass by without being logged (the pause button).
  bool isPaused() const;
  void setPaused(bool paused);
  void clear();
  /// An alarm raised on the newest reading: the row gets the colour.
  void markLast(const QColor &color, const QString &name);

public Q_SLOTS:
  /// A reading from the MeterController.
  void appendReading(const Reading &reading);

public:
  Entry entry(int row) const;

  int maxRows() const;
  /// Rows to keep, at least 1; trims the log when it shrinks.
  void setMaxRows(int rows);

  Stats stats() const;

  /// Text of the rows @p rows (all when empty) - tab separated with a
  /// header, as a spreadsheet pastes it.
  QString toText(const QList<int> &rows = {}) const;
  /// CSV export: `timestamp;value;unit;mode;range;hold`, ISO timestamps
  /// with milliseconds, the value as displayed (with the SI prefix on the
  /// unit). Returns false with @p error on failure or when empty.
  bool write(const QString &path, QString *error = nullptr) const;
  /// write() for .csv, an Excel/OpenDocument sheet for .xlsx/.ods (real
  /// date and number cells, plus an Alarm column).
  bool writeAny(const QString &path, QString *error = nullptr) const;

  /// "2026-09-21 14:03:05.250", the time with its date (clipboard, and
  /// the Time column once the rows span more than one day).
  static QString formatTime(const QDateTime &when);
  /// True while all rows are from the same day: the Time column then shows
  /// only the time of day ("14:03:05.250").
  bool isSingleDay() const { return m_singleDay; }
  /// The Mode column: the decoders' codes as words ("OH" -> "Resistance").
  /// The Mode column: "DC", "AC+DC", "Diode", "Resistance", ...
  static QString modeText(const PortKey &port, quint32 flags);

private:
  RecordingStore *m_own;     ///< used until setStore(), and after that store is gone
  RecordingStore *m_store;
  bool m_singleDay = true;
  void connectStore();
  bool allOneDay() const;
  /// Recomputes m_singleDay after rows came or went; when it flips, the
  /// whole Time column changes its text.
  void updateSingleDay();
};
