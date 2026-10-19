// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pictures of the kinds of instrument made by cmake/device_symbols.cmake:
// manufacturer-colors.json in step with the device table (every "match" a
// vendor of addConfig, "types" the kinds its models are), every model finds
// a picture - its manufacturer's, else the neutral one - and every picture
// made is a valid SVG with no placeholder colour left.
//
// Argument: assets/devices/manufacturer-colors.json

#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSvgRenderer>

#include "device/devicetype.h"
#include "device/dmmdecoder.h"
#include "ui/devicesymbol.h"
#include "testutil.h"

int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  if (argc < 2)
  {
    qWarning() << "usage: test_devicesymbol <manufacturer-colors.json>";
    return 2;
  }
  QFile file(QString::fromLocal8Bit(argv[1]));
  check(file.open(QIODevice::ReadOnly), "the colours file opens");
  const QJsonObject manufacturers = QJsonDocument::fromJson(file.readAll()).object().value("manufacturers").toObject();
  check(manufacturers.contains("default"), "a default entry");

  // the kinds each vendor's models are
  QHash<QString, QSet<QString>> kinds;
  for (const DmmDecoder::DMMInfo &info : DmmDecoder::getDeviceConfigurations())
    kinds[info.vendor].insert(DeviceType::id(DeviceType::of(info)));

  // 1. the file in step with the device table
  for (auto it = manufacturers.begin(); it != manufacturers.end(); ++it)
  {
    const QJsonObject m = it.value().toObject();
    QSet<QString> expected;
    for (const QJsonValue &v : m.value("match").toArray())
    {
      check(kinds.contains(v.toString()), it.key() + ": \"" + v.toString() + "\" is no vendor of the device table");
      expected += kinds.value(v.toString());
    }
    if (it.key() == "default")
      continue;
    QSet<QString> listed;
    for (const QJsonValue &v : m.value("types").toArray())
      listed.insert(v.toString());
    const QStringList missing = QStringList((expected - listed).values());
    const QStringList extra = QStringList((listed - expected).values());
    check(missing.isEmpty() && extra.isEmpty(),
          it.key() + ": \"types\" - add " + missing.join(", ") + "; remove " + extra.join(", "));
    const QJsonObject classes = m.value("device_classes").toObject();
    for (const QString &kind : classes.keys())
      check(listed.contains(kind), it.key() + ": colours for \"" + kind + "\", a kind it does not have");
  }

  // 2. every model a picture: its manufacturer's when it has colours
  for (const DmmDecoder::DMMInfo &info : DmmDecoder::getDeviceConfigurations())
  {
    const DeviceType::Type type = DeviceType::of(info);
    const QString path = DeviceSymbol::path(info.vendor, type);
    const QString folder = DeviceSymbol::folder(info.vendor);
    check(!path.isEmpty() && !path.endsWith("unknown.svg"), info.name + ": a picture, got " + path);
    if (!folder.isEmpty())
      check(path == ":/devices/" + folder + "/" + DeviceType::id(type) + ".svg", info.name + ": its manufacturer's, got " + path);
    else
      check(path == ":/devices/" + DeviceType::id(type) + ".svg", info.name + ": the neutral one, got " + path);
    check(DeviceSymbol::path(info.vendor, type, DeviceSymbol::Design::Dark).endsWith("_dark.svg"), info.name + ": dark twin");
  }
  check(DeviceSymbol::folder("Uni-Trend") == "uni_t" && DeviceSymbol::folder("No such vendor").isEmpty(), "folders");
  check(!DeviceSymbol::pixmap("Uni-Trend", DeviceType::HandheldDmm, QSize(20, 20), 2.0).isNull()
          && !DeviceSymbol::unknown(QSize(20, 20)).isNull(), "drawn, also at 20 px for the sidebar");

  // 3. every picture made: valid, no placeholder left, the outline as the design wants
  int files = 0;
  QDirIterator it(":/devices", { "*.svg" }, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext())
  {
    const QString path = it.next();
    QFile svg(path);
    svg.open(QIODevice::ReadOnly);
    const QByteArray data = svg.readAll().toUpper();
    ++files;
    check(QSvgRenderer(path).isValid(), path + ": valid SVG");
    for (const char *placeholder : { "#FF00FF", "#00FFFF", "#FFFF00" })
      check(!data.contains(placeholder), path + ": " + placeholder + " left");
    check(data.contains(path.endsWith("_dark.svg") ? "#D0D4D8" : "#3A3D41"), path + ": the outline of its design");
  }
  check(files > 100, QString("pictures made: %1").arg(files));

  // 4. a kind with colours of its own: Keysight's bench meters in grey
  {
    QFile bench(":/devices/keysight/bench_dmm.svg");
    QFile hand(":/devices/keysight/handheld_dmm.svg");
    if (bench.open(QIODevice::ReadOnly) && hand.open(QIODevice::ReadOnly))
    {
      const QByteArray b = bench.readAll().toUpper(), h = hand.readAll().toUpper();
      check(!b.contains("#F1792E") && h.contains("#F1792E"), "Keysight: bench meters grey, handhelds orange");
    }
    else
      check(false, "Keysight's pictures are there");
  }

  if (failed)
    qWarning() << failed << "check(s) failed";
  else
    qInfo() << "all checks passed";
  return failed ? 1 : 0;
}
