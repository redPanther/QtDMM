// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QPixmap>
#include <QSize>

#include "device/devicetype.h"

/// The picture of a kind of instrument: a template from assets/devices
/// (":/devices/<DeviceType::id()>.svg", "unknown.svg" when there is none),
/// its two placeholder colours (#FF00FF primary, #00FFFF secondary) replaced,
/// drawn into a box with its aspect ratio kept. Qt SVG knows no CSS variables,
/// hence the placeholders. The colours are neutral for now; per-vendor
/// colours later are only another Colors.
namespace DeviceSymbol
{
struct Colors
{
  QColor primary;
  QColor secondary;
};
/// Neutral grey, for every vendor until vendor colours come.
Colors defaultColors();

/// The template of @p type with @p colors, fitted into @p box (device
/// independent pixels, for @p devicePixelRatio), centred; transparent around
/// it. A null pixmap when not even the fallback could be read.
QPixmap pixmap(DeviceType::Type type, QSize box, qreal devicePixelRatio = 1.0,
               const Colors &colors = defaultColors());
/// The same for no known type (manual settings): the fallback "unknown".
QPixmap unknown(QSize box, qreal devicePixelRatio = 1.0, const Colors &colors = defaultColors());
}
