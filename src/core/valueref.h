// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHashFunctions>
#include <QJsonValue>
#include <QString>

#include "core/reading.h"

/// Which value a view, an alarm or the graph follows: the value @c id of the
/// meter @c source (Reading::source). An empty source is the window's own
/// meter, which is all there is until several meters share a process.
///
/// Stored as text: the own meter's value as the bare number it always was
/// ("1"), so old settings load and older versions still read new ones;
/// another meter's as "1@device:ab12cd34".
struct ValueRef
{
  QString source;   ///< Reading::source; empty = the window's own meter
  int     id = 0;   ///< 0 = main value, 1..3 = secondary values

  ValueRef() = default;
  explicit ValueRef(int valueId, const QString &from = QString()) : source(from), id(valueId) {}

  /// @p r is this value: the same id, from this meter (any meter when the
  /// source is empty).
  bool matches(const Reading &r) const { return r.id == id && (source.isEmpty() || r.source == source); }

  QString toString() const { return source.isEmpty() ? QString::number(id) : QString("%1@%2").arg(id).arg(source); }

  /// Reads toString(); anything else is the own meter's main value.
  static ValueRef fromString(const QString &text)
  {
    const qsizetype at = text.indexOf('@');
    bool ok = false;
    const int id = text.left(at).trimmed().toInt(&ok);
    if (!ok || id < 0 || id >= kMaxValues)
      return {};
    return ValueRef(id, at < 0 ? QString() : text.mid(at + 1).trimmed());
  }

  /// For JSON (alarms): a number for the own meter, as before, else the text.
  QJsonValue toJson() const { return source.isEmpty() ? QJsonValue(id) : QJsonValue(toString()); }
  static ValueRef fromJson(const QJsonValue &v) { return v.isString() ? fromString(v.toString()) : fromString(QString::number(v.toInt(0))); }

  friend bool operator==(const ValueRef &a, const ValueRef &b) { return a.id == b.id && a.source == b.source; }
  friend bool operator!=(const ValueRef &a, const ValueRef &b) { return !(a == b); }
};

inline size_t qHash(const ValueRef &v, size_t seed = 0)
{
  return qHashMulti(seed, v.source, v.id);
}
