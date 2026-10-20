// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPixmap>
#include <QSize>

#include "device/devicetype.h"

/// The picture of a kind of instrument in its manufacturer's colours, drawn
/// into a box with its aspect ratio kept. The pictures are made when CMake
/// runs (cmake/device_symbols.cmake, from the templates in assets/devices and
/// manufacturer-colors.json): ":/devices/<manufacturer>/<type>.svg", for a
/// manufacturer without colours of its own or without that kind the neutral
/// ":/devices/<type>.svg", and "unknown.svg" when there is no type. Each one
/// has a "_dark" twin with a light outline for a dark design.
namespace DeviceSymbol
{
enum class Design
{
  Auto,    ///< from the application's palette: dark when its window colour is
  Light,
  Dark
};

/// The picture of @p type for a model of @p vendor (DMMInfo::vendor, as
/// addConfig has it), fitted into @p box (device independent pixels, for
/// @p devicePixelRatio), centred; transparent around it. A null pixmap when
/// not even the fallback could be read.
QPixmap pixmap(const QString &vendor, DeviceType::Type type, QSize box, qreal devicePixelRatio = 1.0,
               Design design = Design::Auto);
/// The same for no known type (manual settings): the fallback "unknown".
QPixmap unknown(QSize box, qreal devicePixelRatio = 1.0, Design design = Design::Auto);
/// The resource path pixmap() draws: the manufacturer's picture, else the
/// neutral one, else "unknown" (for the tests).
QString path(const QString &vendor, DeviceType::Type type, Design design = Design::Light);
/// The folder of @p vendor's pictures ("uni_t" for "Uni-Trend"), empty for a
/// vendor without colours of its own.
QString folder(const QString &vendor);
/// The maker's website ("https://www.uni-trend.com/" for "Uni-Trend"), from
/// manufacturer-colors.json; empty when it has none.
QString website(const QString &vendor);
}
