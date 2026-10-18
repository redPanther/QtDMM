// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "recording/recordingfile.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <cmath>

#include "core/siprefix.h"
#include "recording/spreadsheet.h"

namespace
{
QString tr(const char *text)
{
  return QCoreApplication::translate("RecordingFile", text);
}

void setError(QString *error, const QString &text)
{
  if (error)
    *error = text;
}
}

double RecordingFile::unitScaleFactor(const QString &unit, const QString &baseUnit)
{
  if (unit != baseUnit && unit.endsWith(baseUnit))
    return SiPrefix::factor(unit.left(unit.size() - baseUnit.size()));
  return 1.0;
}

namespace
{
const char *stateText(CellState state)
{
  switch (state)
  {
    case CellState::Ok:               return "ok";
    case CellState::Overload:         return "OL";
    case CellState::NegativeOverload: return "-OL";
    case CellState::Gap:              return "gap";
    case CellState::None:             break;
  }
  return "";
}

std::optional<CellState> stateFrom(const QString &text)
{
  for (CellState s : { CellState::Ok, CellState::Overload, CellState::NegativeOverload, CellState::Gap })
    if (text.compare(QLatin1String(stateText(s)), Qt::CaseInsensitive) == 0)
      return s;
  if (text.isEmpty())
    return CellState::None;
  return std::nullopt;
}

// the value of a state: an overload's NaN carries its sign
double stateValue(CellState state, double value)
{
  switch (state)
  {
    case CellState::Ok:               return value;
    case CellState::NegativeOverload: return std::copysign(qQNaN(), -1.0);
    default:                          return qQNaN();
  }
}

// one value, unit and status of a row: what the column group of the file holds
struct Cell
{
  double value = qQNaN();
  QString unit;
  CellState state = CellState::None;
};

// "1.25", "-3", "nan" (a gap or an overload), empty (not there)
std::optional<Cell> cellFrom(const QString &value, const QString &unit, const QString *status)
{
  static const QRegularExpression number(R"(^-?\d+(?:\.\d+)?$)");
  Cell c;
  c.unit = unit;
  if (value.isEmpty())
  {
    if (status && !status->isEmpty())
      return std::nullopt;
    return c;
  }
  const bool nan = value.compare("nan", Qt::CaseInsensitive) == 0;
  if (!nan && !number.match(value).hasMatch())
    return std::nullopt;
  if (status)
  {
    const std::optional<CellState> st = stateFrom(*status);
    if (!st || *st == CellState::None || (*st == CellState::Ok) == nan)
      return std::nullopt;
    c.state = *st;
  }
  else
    c.state = nan ? CellState::Gap : CellState::Ok;
  c.value = nan ? qQNaN() : value.toDouble();
  return c;
}
}

std::optional<Recording> RecordingFile::read(const QString &path, QString *error)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
  {
    setError(error, tr("Cannot open file."));
    return std::nullopt;
  }
  QTextStream ts(&file);

  QString line = ts.readLine();
  if (line.isNull())
  {
    setError(error, tr("Oops! Seems not to be a valid file"));
    return std::nullopt;
  }
  // the columns of each value: value;unit, since 26.3 value;unit;status
  bool withStatus = false;
  int groups = 1;
  bool header = false;
  if (line.startsWith("timestamp"))   // the CSV header
  {
    const QStringList names = line.split(';');
    withStatus = names.value(4) == QLatin1String("status");
    groups = withStatus ? qMax(1, int(names.size() - 2) / 3) : 1;
    header = true;
    line = ts.readLine();
    if (line.isNull())
    {
      setError(error, tr("File contains only header"));
      return std::nullopt;
    }
  }

  static const QRegularExpression reLegacy(
    R"(^(?<day>\d{2})\.(?<month>\d{2})\.(?<year>\d{4})\t(?<hour>\d{2}):(?<minute>\d{2}):(?<second>\d{2})(?::(?<ms>\d{1,3}))?\t(?<value>-?\d+(?:\.\d+)?|nan)\t(?<unit>.*)$)",
    QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression reTime(
    R"(^(?<year>\d{4})-(?<month>\d{2})-(?<day>\d{2})T(?<hour>\d{2}):(?<minute>\d{2}):(?<second>\d{2})[,\.](?<ms>\d{1,3})$)");
  static const QRegularExpression reDelta(R"(^(?:-?\d+(?:\.\d+)?|nan)$)", QRegularExpression::CaseInsensitiveOption);

  const bool isLegacy = reLegacy.match(line).hasMatch();
  // a CSV without the header: the columns say which format
  if (!isLegacy && !header && line.count(';') >= 4)
  {
    withStatus = true;
    groups = qMax(1, int(line.count(';') - 1) / 3);
  }
  Recording rec;
  rec.extra.resize(groups - 1);
  QDateTime end;
  auto when = [](const QRegularExpressionMatch &m)
  {
    return QDateTime(QDate(m.captured("year").toInt(), m.captured("month").toInt(), m.captured("day").toInt()),
                     QTime(m.captured("hour").toInt(), m.captured("minute").toInt(), m.captured("second").toInt(),
                           m.captured("ms").toInt()));
  };
  do
  {
    if (!line.trimmed().isEmpty())
    {
      QDateTime at;
      QList<Cell> cells;
      bool valid = true;
      if (isLegacy)
      {
        const QRegularExpressionMatch match = reLegacy.match(line);
        const std::optional<Cell> c = match.hasMatch() ? cellFrom(match.captured("value"), match.captured("unit"), nullptr)
                                                       : std::nullopt;
        valid = c.has_value();
        if (valid)
        {
          at = when(match);
          cells << *c;
        }
      }
      else
      {
        const QStringList f = line.split(';');
        const QRegularExpressionMatch match = reTime.match(f.value(0));
        valid = match.hasMatch() && reDelta.match(f.value(1)).hasMatch()
                && (withStatus ? f.size() == 2 + 3 * groups : f.size() >= 4);
        for (int g = 0; valid && g < groups; ++g)
        {
          // before the status the unit was all the rest of the row
          const QString unit = withStatus ? f.value(3 + 3 * g) : f.mid(3).join(';');
          const QString status = f.value(4 + 3 * g);
          const std::optional<Cell> c = cellFrom(f.value(2 + 3 * g), unit, withStatus ? &status : nullptr);
          valid = c && (g > 0 || c->state != CellState::None);
          if (valid)
            cells << *c;
        }
        if (valid)
          at = when(match);
      }
      if (!valid)
      {
        setError(error, tr("Oops! Seems not to be a valid file"));
        return std::nullopt;
      }
      if (rec.values.isEmpty())
      {
        // the recording's unit is the first row's without its SI prefix;
        // every row is scaled to that base unit
        rec.unit = SiPrefix::split(cells[0].unit).baseUnit;
        rec.start = at;
      }
      end = at;
      // the old tab-separated export was always on the grid, often to the
      // second only
      if (!isLegacy)
        rec.times << rec.start.msecsTo(at);
      rec.values << stateValue(cells[0].state, cells[0].value * unitScaleFactor(cells[0].unit, rec.unit));
      if (withStatus)
        rec.states << cells[0].state;
      for (int g = 1; g < groups; ++g)
      {
        RecordingColumn &col = rec.extra[g - 1];
        const Cell &c = cells[g];
        if (col.unit.isEmpty() && c.state != CellState::None)
          col.unit = SiPrefix::split(c.unit).baseUnit;
        col.values << stateValue(c.state, c.value * unitScaleFactor(c.unit, col.unit));
        col.states << c.state;
      }
    }
    line = ts.readLine();
  }
  while (!line.isNull());

  // Sample time from the span and the row count. (Integer seconds divided by
  // the row count and then by ten, the old formula, came out as 0 -> 1 for
  // anything sampled slower than every 0.1 s.)
  const int count = rec.values.size();
  const qint64 spanMs = rec.start.msecsTo(end);
  rec.sampleTimeTenths = count > 1 ? qMax(1, qRound(spanMs / 100.0 / (count - 1))) : 1;
  return rec;
}

namespace
{
// the header of the columns of each value: value, unit, status, then value 2 ...
QStringList columnNames(int groups, const QStringList &names)
{
  QStringList out;
  for (int g = 0; g < groups; ++g)
    for (const QString &name : names)
      out << (g ? QString("%1 %2").arg(name).arg(g + 1) : name);
  return out;
}
}

bool RecordingFile::write(const Recording &recording, const QString &path, QString *error)
{
  if (recording.values.isEmpty())
  {
    setError(error, tr("Nothing to export."));
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    setError(error, tr("Cannot open file."));
    return false;
  }

  QTextStream ts(&file);
  ts << "timestamp;time (s);" << columnNames(1 + int(recording.extra.size()), { "value", "unit", "status" }).join(';') << '\n';
  auto cell = [&](double v, CellState state, const QString &unit)
  {
    if (state == CellState::None)
      return QString(";;");
    QString prefix;
    const QString value = state == CellState::Ok ? SiPrefix::format(v, unit, &prefix) : QString("nan");
    return QString("%1;%2;%3").arg(value, prefix + unit, QLatin1String(stateText(state)));
  };
  for (int i = 0; i < recording.values.size(); ++i)
  {
    const qint64 ms = recording.timeAt(i);
    const QDateTime dt = recording.start.addMSecs(ms);
    ts << dt.toString("yyyy-MM-ddTHH:mm:ss,zzz") << ';' << QString::number(ms / 1000.0, 'g', 12) << ';'
       << cell(recording.values[i], recording.stateAt(i), recording.unit);
    for (const RecordingColumn &col : recording.extra)
      ts << ';' << cell(col.values.value(i, qQNaN()), col.states.value(i, CellState::None), col.unit);
    ts << '\n';
  }
  return true;
}

bool RecordingFile::writeAny(const Recording &recording, const QString &path, QString *error)
{
  const auto format = SpreadsheetWriter::formatForFile(path);
  if (!format)
    return write(recording, path, error);
  if (recording.values.isEmpty())
  {
    setError(error, tr("Nothing to export."));
    return false;
  }
  SpreadsheetWriter sheet(tr("Recording"));
  sheet.setHeader(QStringList{ tr("timestamp"), tr("time (s)") } + columnNames(1 + int(recording.extra.size()), { tr("value"), tr("unit"), tr("status") }));
  // a value that is no number is an empty cell: a spreadsheet has no NaN
  auto cells = [](QList<QVariant> &row, double v, CellState state, const QString &unit)
  {
    const bool there = state != CellState::None;
    row << (state == CellState::Ok ? QVariant(v) : QVariant()) << (there ? QVariant(unit) : QVariant())
        << (there ? QVariant(QString::fromLatin1(stateText(state))) : QVariant());
  };
  for (int i = 0; i < recording.values.size(); ++i)
  {
    const qint64 ms = recording.timeAt(i);
    QList<QVariant> row{ recording.start.addMSecs(ms), ms / 1000.0 };
    cells(row, recording.values[i], recording.stateAt(i), recording.unit);
    for (const RecordingColumn &col : recording.extra)
      cells(row, col.values.value(i, qQNaN()), col.states.value(i, CellState::None), col.unit);
    sheet.addRow(row);
  }
  return sheet.write(path, *format, error);
}
