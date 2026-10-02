// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "recording/readinglog.h"
#include "core/reading.h"
#include "recording/recordingstore.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <cmath>

#include "core/siprefix.h"
#include "recording/spreadsheet.h"

ReadingLog::ReadingLog(QObject *parent) :
  QAbstractTableModel(parent), m_own(new RecordingStore(this)), m_store(m_own)
{
  connectStore();
}

// The store's signals come in pairs around each change, as the model wants them.
void ReadingLog::connectStore()
{
  connect(m_store, &RecordingStore::readingsAboutToInsert, this,
          [this](int first, int last) { beginInsertRows(QModelIndex(), first, last); });
  connect(m_store, &RecordingStore::readingsInserted, this, [this] { endInsertRows(); updateSingleDay(); });
  connect(m_store, &RecordingStore::readingsAboutToRemove, this,
          [this](int first, int last) { beginRemoveRows(QModelIndex(), first, last); });
  connect(m_store, &RecordingStore::readingsRemoved, this, [this] { endRemoveRows(); updateSingleDay(); });
  connect(m_store, &RecordingStore::readingsAboutToClear, this, [this] { beginResetModel(); });
  connect(m_store, &RecordingStore::readingsCleared, this, [this]
  {
    m_singleDay = true;
    endResetModel();
  });
  connect(m_store, &RecordingStore::readingMarked, this, [this](int row)
  {
    Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1), {Qt::BackgroundRole, Qt::ToolTipRole});
  });
  // a store from outside may go first (the main window's views are deleted
  // one after the other): the table falls back to its own, empty one
  if (m_store != m_own)
    connect(m_store, &QObject::destroyed, this, [this]
    {
      beginResetModel();
      m_store = m_own;
      m_own->clearReadings();   // not connected yet: rows from before setStore()
      connectStore();
      m_singleDay = allOneDay();
      endResetModel();
    });
}

void ReadingLog::setStore(RecordingStore *store)
{
  if (!store || store == m_store)
    return;
  beginResetModel();
  m_store->disconnect(this);
  m_store = store;
  connectStore();
  m_singleDay = allOneDay();
  endResetModel();
}

bool ReadingLog::isPaused() const
{
  return m_store->readingsPaused();
}

void ReadingLog::setPaused(bool paused)
{
  m_store->setReadingsPaused(paused);
}

int ReadingLog::maxRows() const
{
  return m_store->readingCapacity();
}

ReadingLog::Entry ReadingLog::entry(int row) const
{
  const LoggedReading &r = m_store->readingAt(row);
  Entry e;
  e.when = QDateTime::fromMSecsSinceEpoch(r.when);
  e.dval = r.value;
  e.val = r.text;
  e.unit = r.unit;
  e.port = r.port;
  e.flags = r.flags;
  e.range = r.range;
  e.hold = r.hold();
  e.id = r.id;
  if (r.alarmArgb)
    e.alarmColor = QColor::fromRgba(r.alarmArgb);
  e.alarmName = r.alarmName;
  return e;
}

int ReadingLog::rowCount(const QModelIndex &parent) const
{
  return parent.isValid() ? 0 : m_store->readingCount();
}

int ReadingLog::columnCount(const QModelIndex &parent) const
{
  return parent.isValid() ? 0 : ColumnCount;
}

QString ReadingLog::formatTime(const QDateTime &when)
{
  return when.toString("yyyy-MM-dd HH:mm:ss.zzz");
}

QString ReadingLog::modeText(const PortKey &port, quint32 flags)
{
  if (flags & SampleFlag::Diode)
    return tr("Diode");
  const QString coupling = couplingText(flags);
  if (!coupling.isEmpty())
    return coupling == QLatin1String("AC+DC") ? tr("AC+DC") : coupling;
  switch (port.quantity)
  {
    case Quantity::Unknown:
    case Quantity::Voltage:
    case Quantity::Current:     return QString();
    case Quantity::Continuity:  return tr("Continuity");
    case Quantity::Resistance:  return tr("Resistance");
    case Quantity::Capacitance: return tr("Capacitance");
    case Quantity::Frequency:   return tr("Frequency");
    case Quantity::Temperature: return tr("Temperature");
    default:                    return Quantities::name(port.quantity);
  }
}

QVariant ReadingLog::data(const QModelIndex &index, int role) const
{
  if (!index.isValid() || index.row() >= m_store->readingCount())
    return QVariant();
  const LoggedReading &e = m_store->readingAt(index.row());

  if (role == DvalRole)
    return e.value;
  if (role == Qt::BackgroundRole && e.alarmArgb)
  {
    QColor c = QColor::fromRgba(e.alarmArgb);
    c.setAlpha(70);
    return c;
  }
  if (role == Qt::ToolTipRole && !e.alarmName.isEmpty())
    return e.alarmName;   // the row is marked in the alarm's colour; "Alarm 1", not "Alarm Alarm 1"
  if (role == Qt::TextAlignmentRole)
    return int(index.column() == Value ? Qt::AlignRight | Qt::AlignVCenter : Qt::AlignLeft | Qt::AlignVCenter);
  if (role != Qt::DisplayRole)
    return QVariant();

  switch (index.column())
  {
    case Time:
    {
      const QDateTime when = QDateTime::fromMSecsSinceEpoch(e.when);
      return m_singleDay ? when.toString("HH:mm:ss.zzz") : formatTime(when);
    }
    case Value: return SiPrefix::withoutLeadingZeros(e.text);   // "000.00" -> "0.00", as the meter shows it
    case Unit:  return e.unit;
    case Mode:  return e.id > 0 ? (tr("2nd") + " " + modeText(e.port, e.flags)).trimmed() : modeText(e.port, e.flags);
    case Range: return e.range;
    case Hold:  return e.hold() ? tr("HOLD") : QString();
  }
  return QVariant();
}

QVariant ReadingLog::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (role != Qt::DisplayRole)
    return QVariant();
  if (orientation == Qt::Vertical)
    return section + 1;
  switch (section)
  {
    case Time:  return tr("Time");
    case Value: return tr("Value");
    case Unit:  return tr("Unit");
    case Mode:  return tr("Mode");
    case Range: return tr("Range");
    case Hold:  return tr("Hold");
  }
  return QVariant();
}

void ReadingLog::append(const Entry &entry)
{
  static const QRegularExpression letters("[A-Za-z]");
  Reading r;
  r.value = entry.dval;
  r.text = entry.val;
  r.unit = entry.unit;
  r.port = entry.port;
  r.flags = entry.flags | (entry.hold ? SampleFlag::Hold : 0u);
  r.range = entry.range;
  r.hold = entry.hold;
  r.id = entry.id;
  r.overload = entry.val.contains(letters);
  r.msecs = entry.when.toMSecsSinceEpoch();
  m_store->logReading(r);
}

bool ReadingLog::allOneDay() const
{
  // the rows are in time order: first and last tell whether a day changed
  const int count = m_store->readingCount();
  return count == 0 || entry(0).when.date() == entry(count - 1).when.date();
}

void ReadingLog::updateSingleDay()
{
  const bool single = allOneDay();
  if (single == m_singleDay)
    return;
  m_singleDay = single;
  const int count = m_store->readingCount();
  if (count)
    Q_EMIT dataChanged(index(0, Time), index(count - 1, Time), {Qt::DisplayRole});
}

void ReadingLog::markLast(const QColor &color, const QString &name)
{
  m_store->markLastReading(color.rgba(), name);
}

void ReadingLog::clear()
{
  m_store->clearReadings();
}

void ReadingLog::setMaxRows(int rows)
{
  m_store->setReadingCapacity(rows);
}

ReadingLog::Stats ReadingLog::stats() const
{
  static const QRegularExpression letters("[A-Za-z]");
  Stats s;
  s.count = m_store->readingCount();
  double sum = 0;
  for (int i = 0; i < s.count; ++i)
  {
    const LoggedReading &e = m_store->readingAt(i);
    if (e.id != 0 || e.text.contains(letters) || !std::isfinite(e.value))   // secondary values, OL
      continue;
    if (s.numeric == 0)
      s.min = s.max = e.value;
    s.min = qMin(s.min, e.value);
    s.max = qMax(s.max, e.value);
    sum += e.value;
    ++s.numeric;
  }
  if (s.numeric)
    s.mean = sum / s.numeric;
  // the unit of the newest main reading (second values have their own)
  for (int i = s.count - 1; i >= 0; --i)
    if (m_store->readingAt(i).id == 0)
    {
      s.unit = SiPrefix::split(m_store->readingAt(i).unit).baseUnit;
      break;
    }
  return s;
}

QString ReadingLog::toText(const QList<int> &rows) const
{
  QStringList lines;
  QStringList header;
  for (int c = 0; c < ColumnCount; ++c)
    header << headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
  lines << header.join('\t');

  QList<int> which = rows;
  if (which.isEmpty())
    for (int r = 0; r < m_store->readingCount(); ++r)
      which << r;
  std::sort(which.begin(), which.end());
  for (int r : which)
  {
    if (r < 0 || r >= m_store->readingCount())
      continue;
    QStringList cells;
    // the date stays in the copy: pasted elsewhere the rows lose their context
    cells << formatTime(QDateTime::fromMSecsSinceEpoch(m_store->readingAt(r).when));
    for (int c = Time + 1; c < ColumnCount; ++c)
      cells << data(index(r, c), Qt::DisplayRole).toString();
    lines << cells.join('\t');
  }
  return lines.join('\n') + '\n';
}

bool ReadingLog::write(const QString &path, QString *error) const
{
  auto fail = [&](const QString &text)
  {
    if (error)
      *error = text;
    return false;
  };
  if (m_store->readingCount() == 0)
    return fail(tr("Nothing to export."));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    return fail(tr("Cannot open file."));
  QTextStream ts(&file);
  ts << "timestamp;value;unit;mode;range;hold\n";
  for (int i = 0; i < m_store->readingCount(); ++i)
  {
    const Entry e = entry(i);
    ts << QString("%1;%2;%3;%4;%5;%6\n")
            .arg(e.when.toString("yyyy-MM-ddTHH:mm:ss,zzz"), SiPrefix::withoutLeadingZeros(e.val), e.unit,
                 e.id > 0 ? ("2nd " + modeText(e.port, e.flags)).trimmed() : modeText(e.port, e.flags), e.range, e.hold ? "1" : "0");
  }
  return true;
}

bool ReadingLog::writeAny(const QString &path, QString *error) const
{
  const auto format = SpreadsheetWriter::formatForFile(path);
  if (!format)
    return write(path, error);
  if (m_store->readingCount() == 0)
  {
    if (error)
      *error = tr("Nothing to export.");
    return false;
  }
  SpreadsheetWriter sheet(tr("Readings"));
  sheet.setHeader({tr("Time"), tr("Value"), tr("Unit"), tr("Mode"), tr("Range"), tr("Hold"), tr("Alarm")});
  for (int i = 0; i < m_store->readingCount(); ++i)
  {
    const Entry e = entry(i);
    const QString val = SiPrefix::withoutLeadingZeros(e.val);
    bool numeric = false;
    const double number = val.toDouble(&numeric);
    sheet.addRow({e.when, numeric ? QVariant(number) : QVariant(val), e.unit,
                  e.id > 0 ? ("2nd " + modeText(e.port, e.flags)).trimmed() : modeText(e.port, e.flags), e.range, e.hold ? tr("HOLD") : QString(), e.alarmName});
  }
  return sheet.write(path, *format, error);
}

void ReadingLog::appendReading(const Reading &r)
{
  m_store->logReading(r);
}
