// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QIODevice>

#include <cstring>

/// A read-only stream whose bytes arrive by signal (a HID report, a Bluetooth
/// notification, a timer, a socket) and wait in m_rx until FrameReader reads
/// them. The subclass appends to m_rx in the device's own thread and emits
/// readyRead().
class BufferedDevice : public QIODevice
{
public:
  using QIODevice::QIODevice;

  /// Must stay const: QIODevice's version is const and virtual, so a
  /// non-const one would hide instead of override it and FrameReader
  /// (holding a QIODevice*) would get the base version.
  qint64 bytesAvailable() const override { return m_rx.size() + QIODevice::bytesAvailable(); }

protected:
  qint64 readData(char *data, qint64 maxSize) override
  {
    const qint64 len = qMin(maxSize, qint64(m_rx.size()));
    if (len <= 0)
      return 0;
    std::memcpy(data, m_rx.constData(), size_t(len));
    m_rx.remove(0, len);
    return len;
  }

  QByteArray m_rx;   ///< received, not read yet
};
