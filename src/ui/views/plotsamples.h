// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QVector>

/// Bounded FIFO sample storage. hold() returns an independent frozen copy.
template <typename T>
class RingBuffer
{
public:
  explicit RingBuffer(int capacity) : m_capacity(capacity > 0 ? capacity : 1) { m_items.reserve(m_capacity); }
  int size() const { return m_items.size(); }
  bool isEmpty() const { return m_items.isEmpty(); }
  void clear() { m_items.clear(); m_head = 0; }
  void append(const T &value)
  {
    if (m_items.size() < m_capacity) m_items.append(value);
    else { m_items[m_head] = value; m_head = (m_head + 1) % m_capacity; }
  }
  QVector<T> values() const
  {
    QVector<T> result;
    result.reserve(m_items.size());
    for (int i = 0; i < m_items.size(); ++i) result.append(m_items[(m_head + i) % m_items.size()]);
    return result;
  }
  QVector<T> hold() const { return values(); }
private:
  int m_capacity;
  int m_head = 0;
  QVector<T> m_items;
};
