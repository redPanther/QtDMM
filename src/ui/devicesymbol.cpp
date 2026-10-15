// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/devicesymbol.h"

#include <QFile>
#include <QPainter>
#include <QSvgRenderer>

namespace DeviceSymbol
{
namespace
{
QPixmap render(const QString &name, QSize box, qreal dpr, const Colors &colors)
{
  QFile file(QStringLiteral(":/devices/%1.svg").arg(name));
  if (!file.open(QIODevice::ReadOnly))
  {
    file.setFileName(QStringLiteral(":/devices/unknown.svg"));
    if (!file.open(QIODevice::ReadOnly))
      return {};
  }
  QByteArray svg = file.readAll();
  // the placeholders, as the templates write them
  for (const char *p : { "#FF00FF", "#ff00ff" })
    svg.replace(p, colors.primary.name().toLatin1());
  for (const char *p : { "#00FFFF", "#00ffff" })
    svg.replace(p, colors.secondary.name().toLatin1());
  QSvgRenderer renderer(svg);
  if (!renderer.isValid())
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
}

Colors defaultColors()
{
  return { QColor("#8A9096"), QColor("#4C4F54") };
}

QPixmap pixmap(DeviceType::Type type, QSize box, qreal devicePixelRatio, const Colors &colors)
{
  return render(DeviceType::id(type), box, devicePixelRatio, colors);
}

QPixmap unknown(QSize box, qreal devicePixelRatio, const Colors &colors)
{
  return render(QStringLiteral("unknown"), box, devicePixelRatio, colors);
}
}
