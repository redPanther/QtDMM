// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/reading.h"

/// The min/max memory of the main value, in SI base units, so it carries
/// across range changes (mV <-> V, kΩ <-> MΩ). It starts afresh only when
/// the port changes - another function (V DC -> Ω), another coupling
/// (DC -> AC -> AC+DC), another quantity - or the unit at the same port
/// (°C -> °F): the same rule as the recording's stop (RecordingStore).
/// An overload or a value without a known quantity says nothing about the
/// function. kern_spezifikation §7, §13 point 1.
class MinMaxMemory
{
public:
  struct Result
  {
    bool reset = false;    ///< the memory started afresh with this reading
    bool newMin = false;
    bool newMax = false;
  };

  /// A main value that is not held. An overload (NaN) is no new extreme.
  Result feed(const Reading &r)
  {
    Result res;
    if (!r.overload && r.port.quantity != Quantity::Unknown)
    {
      if (m_port.isValid() && (r.port != m_port || r.baseUnit != m_baseUnit))
      {
        clear();
        res.reset = true;
      }
      m_port = r.port;
      m_baseUnit = r.baseUnit;
    }
    if (r.value > m_max)
    {
      m_max = r.value;
      res.newMax = true;
    }
    if (r.value < m_min)
    {
      m_min = r.value;
      res.newMin = true;
    }
    return res;
  }

  /// Empty again (the Reset key); the next reading sets the port anew.
  void clear()
  {
    m_min = kEmptyMin;
    m_max = kEmptyMax;
    m_port = PortKey();
    m_baseUnit.clear();
  }

  /// In SI base units; +-1e20 when empty.
  double minimum() const { return m_min; }
  double maximum() const { return m_max; }

private:
  static constexpr double kEmptyMin = 1.0E20;
  static constexpr double kEmptyMax = -1.0E20;
  double  m_min = kEmptyMin;
  double  m_max = kEmptyMax;
  PortKey m_port;          ///< invalid until a reading with a known quantity
  QString m_baseUnit;
};
