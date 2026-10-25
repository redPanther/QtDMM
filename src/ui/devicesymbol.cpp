// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/devicesymbol.h"

#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>

namespace DeviceSymbol
{
namespace
{
bool dark(Design design)
{
  if (design == Design::Auto)
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
  return design == Design::Dark;
}

// the first of the names that is there, with the design's suffix
QString first(const QStringList &names, Design design)
{
  const QString suffix = dark(design) ? QStringLiteral("_dark") : QString();
  for (const QString &name : names)
  {
    const QString p = QStringLiteral(":/devices/%1%2.svg").arg(name, suffix);
    if (QFile::exists(p))
      return p;
  }
  return {};
}

// the outline of the light pictures (QTDMM_SYMBOL_OUTLINE_LIGHT of device_symbols.cmake)
constexpr char kOutline[] = "#3A3D41";

QPixmap render(const QString &path, QSize box, qreal dpr, Design design = Design::Light)
{
  QByteArray svg;
  QFile file(path);
  if (!path.isEmpty() && file.open(QIODevice::ReadOnly))
    svg = file.readAll();
  if (design == Design::Flat)
    svg.replace(kOutline, "none").replace(QByteArray(kOutline).toLower(), "none");
  QSvgRenderer renderer(svg);
  if (svg.isEmpty() || !renderer.isValid())
    return {};

  QPixmap pm(box * dpr);
  pm.setDevicePixelRatio(dpr);
  pm.fill(Qt::transparent);
  // the templates are tall or wide: the largest that fits, centred
  QSizeF size = renderer.viewBoxF().size();
  size.scale(QSizeF(box), Qt::KeepAspectRatio);
  const QRectF target(QPointF((box.width() - size.width()) / 2, (box.height() - size.height()) / 2), size);
  QPainter painter(&pm);
  painter.setRenderHint(QPainter::Antialiasing);
  renderer.render(&painter, target);
  return pm;
}

// addConfig vendor -> text, a file the generator wrote (device_symbols.cmake)
QHash<QString, QString> vendorMap(const QString &resource)
{
  QHash<QString, QString> map;
  QFile file(resource);
  if (file.open(QIODevice::ReadOnly))
  {
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it)
      map.insert(it.key(), it.value().toString());
  }
  return map;
}
}

QString folder(const QString &vendor)
{
  // vendor -> folder, as the generator wrote it
  static const QHash<QString, QString> folders = vendorMap(QStringLiteral(":/devices/vendors.json"));
  return folders.value(vendor);
}

QString website(const QString &vendor)
{
  static const QHash<QString, QString> urls = vendorMap(QStringLiteral(":/devices/websites.json"));
  return urls.value(vendor);
}

QString path(const QString &vendor, DeviceType::Type type, Design design)
{
  const QString id = DeviceType::id(type);
  const QString dir = folder(vendor);
  QStringList names;
  if (!dir.isEmpty())
    names << dir + '/' + id;
  names << id << QStringLiteral("unknown");
  return first(names, design);
}

QPixmap pixmap(const QString &vendor, DeviceType::Type type, QSize box, qreal devicePixelRatio, Design design)
{
  return render(path(vendor, type, design), box, devicePixelRatio, design);
}

QPixmap unknown(QSize box, qreal devicePixelRatio, Design design)
{
  return render(first({ QStringLiteral("unknown") }, design), box, devicePixelRatio, design);
}
}
