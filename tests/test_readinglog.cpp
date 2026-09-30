// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ReadingLog: the readings table model without its widget. Ring behaviour,
// model signals (QAbstractItemModelTester watches the contracts), the
// statistics line, tab text for the clipboard and the CSV export.

#include <QtCore>
#include <QAbstractItemModelTester>

#include "readinglog.h"
#include "reading.h"
#include "recordingstore.h"

static int failed = 0;

static void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAIL:" << what;
    ++failed;
  }
}

static ReadingLog::Entry entry(double dval, const QString &val, const QString &unit, int secs, int id = 0)
{
  ReadingLog::Entry e;
  e.when = QDateTime(QDate(2026, 9, 21), QTime(14, 3, 5, 250)).addSecs(secs);
  e.dval = dval;
  e.val = val;
  e.unit = unit;
  e.special = "DC";
  e.range = "AUTO";
  e.id = id;
  return e;
}

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  ReadingLog log;
  QAbstractItemModelTester tester(&log, QAbstractItemModelTester::FailureReportingMode::Warning);

  int inserted = 0, removed = 0, resets = 0;
  QObject::connect(&log, &QAbstractItemModel::rowsInserted, [&] { ++inserted; });
  QObject::connect(&log, &QAbstractItemModel::rowsRemoved, [&] { ++removed; });
  QObject::connect(&log, &QAbstractItemModel::modelReset, [&] { ++resets; });

  // --- 1. rows and cells ---
  check(log.rowCount() == 0 && log.columnCount() == ReadingLog::ColumnCount, "empty model");
  log.append(entry(1.234, "1.234", "V", 0));
  log.append(entry(0.0122, "12.2", "mV", 1));
  log.append(entry(0, " OL ", "V", 2));
  log.append(entry(50.0, "50.0", "Hz", 3, 1));   // a secondary value
  check(log.rowCount() == 4 && inserted == 4, "four rows appended");
  check(log.isSingleDay() && log.data(log.index(0, ReadingLog::Time)).toString() == "14:03:05.250",
        "time cell without the date while all rows are from one day: " + log.data(log.index(0, ReadingLog::Time)).toString());
  check(log.data(log.index(1, ReadingLog::Value)).toString() == "12.2", "value cell");
  check(log.data(log.index(1, ReadingLog::Unit)).toString() == "mV", "unit cell");
  check(log.data(log.index(2, ReadingLog::Value)).toString() == "OL", "overload cell is trimmed");
  check(log.data(log.index(3, ReadingLog::Mode)).toString().startsWith("2nd"), "secondary value marked");
  check(log.data(log.index(1, ReadingLog::Value), ReadingLog::DvalRole).toDouble() == 0.0122, "dval role");
  check(log.headerData(ReadingLog::Value, Qt::Horizontal).toString() == "Value", "header");
  {
    // a meter whose secondary display is off sends an empty second value:
    // the display clears it, the log does not take it
    Reading off;
    off.id = 1;
    off.msecs = QDateTime::currentMSecsSinceEpoch();
    const int before = log.rowCount();
    log.appendReading(off);
    check(log.rowCount() == before, "an empty second value is no row");
  }

  // --- 1b. a day change brings the date into every row, and it goes again
  //         when the rows of the first day have been dropped ---
  {
    ReadingLog days;
    int timeChanges = 0;
    QObject::connect(&days, &QAbstractItemModel::dataChanged, [&](const QModelIndex &tl, const QModelIndex &br)
    {
      if (tl.column() == ReadingLog::Time && br.column() == ReadingLog::Time && tl.row() == 0)
        ++timeChanges;
    });
    days.append(entry(1.0, "1.000", "V", 0));
    days.append(entry(2.0, "2.000", "V", 86400));   // the next day
    check(!days.isSingleDay() && timeChanges == 1, "the second day switches the dates on");
    check(days.data(days.index(0, ReadingLog::Time)).toString() == "2026-09-21 14:03:05.250", "time cell with date");
    check(days.data(days.index(1, ReadingLog::Time)).toString() == "2026-09-22 14:03:05.250", "next day's cell");
    days.setMaxRows(1);
    check(days.isSingleDay() && timeChanges == 2, "one day left: the dates go");
    check(days.data(days.index(0, ReadingLog::Time)).toString() == "14:03:05.250", "time only again");
    days.append(entry(3.0, "3.000", "V", 90000));
    days.clear();
    check(days.isSingleDay(), "cleared log is single-day");
  }

  // --- 2. statistics skip overloads and secondary values ---
  ReadingLog::Stats s = log.stats();
  check(s.count == 4 && s.numeric == 2, QString("stats count %1 numeric %2").arg(s.count).arg(s.numeric));
  check(qFuzzyCompare(s.min, 0.0122) && qFuzzyCompare(s.max, 1.234), "stats min/max");
  check(qFuzzyCompare(s.mean, (1.234 + 0.0122) / 2), "stats mean");
  check(s.unit == "V", "stats unit is the newest main reading's base unit, not the 2nd value's: " + s.unit);

  // --- 3. ring: the oldest rows go ---
  log.setMaxRows(3);
  check(log.rowCount() == 3 && removed == 1, "shrinking maxRows trims the oldest");
  check(log.data(log.index(0, ReadingLog::Value)).toString() == "12.2", "first row is now the second reading");
  log.append(entry(2.0, "2.000", "V", 4));
  check(log.rowCount() == 3 && removed == 2, "append at the limit drops one");
  check(log.data(log.index(2, ReadingLog::Value)).toString() == "2.000", "newest row is last");
  log.setMaxRows(0);
  check(log.maxRows() == 1 && log.rowCount() == 1, "maxRows is at least 1");
  log.setMaxRows(1000);

  // --- 3b. paused: readings pass by ---
  log.setPaused(true);
  log.append(entry(9.0, "9.000", "V", 5));
  check(log.rowCount() == 1 && log.isPaused(), "paused log drops the reading");
  log.setPaused(false);
  log.append(entry(9.0, "9.000", "V", 5));
  check(log.rowCount() == 2, "resumed log takes readings again");

  // --- 3c. the meter's fixed-width text without leading zeros, as the
  //          meter shows it (table, copy and export) ---
  log.append(entry(0.0, "000.00", "mV", 6));
  log.append(entry(-0.0293, "-029.30", "mV", 7));
  check(log.data(log.index(2, ReadingLog::Value)).toString() == "0.00", "000.00 -> 0.00");
  check(log.data(log.index(3, ReadingLog::Value)).toString() == "-29.30", "-029.30 -> -29.30");

  // --- 4. clipboard text ---
  log.clear();
  check(log.rowCount() == 0 && resets == 1, "clear resets");
  log.append(entry(1.0, "1.000", "V", 0));
  log.append(entry(2.0, "2.000", "V", 1));
  const QString text = log.toText();
  const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
  check(lines.size() == 3 && lines[0].startsWith("Time\tValue\tUnit"), "tab text has a header: " + lines.value(0));
  check(lines[2] == "2026-09-21 14:03:06.250\t2.000\tV\tDC\tAUTO\t", "tab text row: " + lines.value(2));
  check(log.toText({1}).split('\n', Qt::SkipEmptyParts).size() == 2, "selected rows only");

  // --- 5. CSV export ---
  QTemporaryDir tmp;
  const QString path = tmp.filePath("readings.csv");
  QString error;
  check(log.write(path, &error), "write succeeds: " + error);
  QFile f(path);
  check(f.open(QIODevice::ReadOnly | QIODevice::Text), "csv opens");
  const QStringList csv = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
  check(csv.size() == 3 && csv[0] == "timestamp;value;unit;mode;range;hold", "csv header: " + csv.value(0));
  check(csv[1] == "2026-09-21T14:03:05,250;1.000;V;DC;AUTO;0", "csv row: " + csv.value(1));
  ReadingLog empty;
  check(!empty.write(path, &error) && !error.isEmpty(), "empty log refuses to export");
  check(!log.write(tmp.filePath("no/such/dir/x.csv"), &error), "unwritable path fails");

  // --- 6. spreadsheet export ---
  {
    log.markLast(QColor("#d82222"), "Low voltage");
    QString err;
    check(log.writeAny(tmp.filePath("r.xlsx"), &err), "xlsx: " + err);
    check(log.writeAny(tmp.filePath("r.ods"), &err), "ods: " + err);
    check(log.writeAny(tmp.filePath("r2.csv"), &err), "csv through writeAny: " + err);
    check(!empty.writeAny(tmp.filePath("e.ods"), &err), "empty refuses");
  }

  // --- 7. a thin model over a store: two logs of one store see the same
  //         rows, the limit and the pause are the store's, and the table
  //         follows a series that is independent of the recording ---
  {
    RecordingStore store;
    ReadingLog mine;
    mine.append(entry(1.0, "1.000", "V", 0));   // goes to its own store
    mine.setStore(&store);
    check(mine.rowCount() == 0 && mine.store() == &store, "setStore: the log shows the new store, empty");
    mine.setMaxRows(2);
    check(store.readingCapacity() == 2, "setStore: the limit is the store's");
    QAbstractItemModelTester shared(&mine, QAbstractItemModelTester::FailureReportingMode::Warning);

    ReadingLog other;
    other.setStore(&store);
    int rowsSeen = 0;
    QObject::connect(&other, &QAbstractItemModel::rowsInserted, [&] { ++rowsSeen; });

    Reading r;
    r.value = 0.0122;
    r.text = "12.2";
    r.unit = "mV";
    r.special = "DC";
    r.range = "AUTO";
    r.msecs = QDateTime(QDate(2026, 9, 21), QTime(14, 3, 5, 250)).toMSecsSinceEpoch();
    store.setReading(r);   // the recorder's feed, as the MeterController makes it
    store.setReading(r);
    store.setReading(r);
    check(mine.rowCount() == 2 && other.rowCount() == 2 && rowsSeen == 3,
          QString("shared store: %1 and %2 rows").arg(mine.rowCount()).arg(other.rowCount()));
    check(mine.data(mine.index(0, ReadingLog::Value)).toString() == "12.2" &&
          mine.data(mine.index(0, ReadingLog::Time)).toString() == "14:03:05.250",
          "shared store: the cells come from the store's row");

    mine.markLast(QColor("#d82222"), "Alarm 1");
    check(other.data(other.index(1, 0), Qt::ToolTipRole).toString() == "Alarm 1" &&
          other.data(other.index(1, 0), Qt::BackgroundRole).value<QColor>().alpha() == 70,
          "shared store: the mark shows in the other view");

    other.setPaused(true);
    store.setReading(r);
    check(mine.rowCount() == 2 && mine.isPaused(), "shared store: the pause is the store's");
    other.setPaused(false);
    mine.clear();
    check(other.rowCount() == 0 && store.readingCount() == 0, "shared store: clear empties it for both");
  }

  if (failed == 0)
    qInfo() << "All reading log tests passed.";
  else
    qWarning() << failed << "reading log test(s) failed.";
  return failed == 0 ? 0 : 1;
}
