// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/devicespecs.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace DeviceSpecs
{
namespace
{
/// "<vendor> <model>" -> its data, from all files in the resources
const QHash<QString, QJsonObject> &all()
{
  static const QHash<QString, QJsonObject> specs = []
  {
    QHash<QString, QJsonObject> map;
    const QDir dir(QStringLiteral(":/specs"));
    for (const QString &name : dir.entryList({ QStringLiteral("*.json") }, QDir::Files))
    {
      QFile file(dir.filePath(name));
      if (!file.open(QIODevice::ReadOnly))
        continue;
      const QJsonObject spec = QJsonDocument::fromJson(file.readAll()).object();
      for (const QJsonValue &model : spec.value("models").toArray())
        map.insert(model.toString(), spec);
    }
    return map;
  }();
  return specs;
}

QString esc(const QJsonValue &v)
{
  return v.toString().toHtmlEscaped();
}
}

QJsonObject find(const QString &vendor, const QString &model)
{
  // "UT61E+ (UT-D07B Bluetooth) *" is the UT61E+
  QString base = model;
  base.remove(QRegularExpression(QStringLiteral("\\s*\\*$")));
  base.remove(QRegularExpression(QStringLiteral("\\s*\\(.*\\)$")));
  return all().value(vendor + ' ' + base);
}

QString html(const QJsonObject &spec)
{
  QString out;
  QString last;
  for (const QJsonValue &v : spec.value("tables").toArray())
  {
    const QJsonObject table = v.toObject();
    // a function split into several tables (other input impedance or fuse)
    // has its heading once
    const QString name = table.value("table").toString();
    if (name != last)
    {
      out += QString("<h3>%1</h3>").arg(name.toHtmlEscaped());
      last = name;
    }
    QStringList facts;
    if (table.value("input_impedance").isString())
      facts << QCoreApplication::translate("DeviceSpecs", "Input impedance: %1").arg(esc(table.value("input_impedance")));
    if (table.value("overload_protection").isString())
      facts << QCoreApplication::translate("DeviceSpecs", "Overload protection: %1").arg(esc(table.value("overload_protection")));
    if (table.value("page").isDouble())
      facts << QCoreApplication::translate("DeviceSpecs", "manual p. %1").arg(table.value("page").toInt());
    if (!facts.isEmpty())
      out += QString("<p>%1</p>").arg(facts.join(" &middot; "));
    const QString range = QCoreApplication::translate("DeviceSpecs", "Range");
    const QString resolution = QCoreApplication::translate("DeviceSpecs", "Resolution");
    const QString accuracy = QCoreApplication::translate("DeviceSpecs", "Accuracy ±(% of reading + digits)");
    out += QString("<table border=\"1\" cellspacing=\"0\" cellpadding=\"3\"><tr><th>%1</th><th>%2</th><th>%3</th></tr>")
             .arg(range.toHtmlEscaped(), resolution.toHtmlEscaped(), accuracy.toHtmlEscaped());
    for (const QJsonValue &r : table.value("ranges").toArray())
    {
      const QJsonObject row = r.toObject();
      QStringList bands;
      for (const QJsonValue &a : row.value("accuracy").toArray())
      {
        const QJsonObject band = a.toObject();
        bands << (band.value("freq_range").isString() ? esc(band.value("freq_range")) + ": " : QString())
                 + esc(band.value("accuracy"));
      }
      out += QString("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
               .arg(esc(row.value("range")), esc(row.value("resolution")), bands.join("<br>"));
    }
    out += "</table>";
    QStringList notes;
    for (const QJsonValue &n : table.value("notes").toArray())
      notes << "<li>" + esc(n) + "</li>";
    if (!notes.isEmpty())
      out += "<ul>" + notes.join(QString()) + "</ul>";
  }
  out += QString("<p><small>%1</small></p>")
           .arg(QCoreApplication::translate("DeviceSpecs", "From the manual, transcribed by <a href=\"https://github.com/antoinecellerier/dmm-tools\">dmm-tools</a> "
                   "(%1, GPL-3.0-or-later) - thank you. Check the manual before relying on it.")
                  .arg(esc(spec.value("source"))));
  return out;
}
}
