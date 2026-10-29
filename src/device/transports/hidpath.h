// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

/// @file
/// Finding a USB-HID cable again when its path changed.

/// A HID path is not stable: /dev/hidraw3 is /dev/hidraw5 after the cable
/// was plugged in again or the computer restarted, and on Windows and macOS
/// the path changes with the USB port or on every plug. The port entry
/// keeps the cable's USB ids ("0x1a86:0xe008 /dev/hidraw3"); when the path
/// is gone or now belongs to another device, the one cable with these ids
/// is taken instead. On Linux a udev link (/dev/qtdmm/..., see
/// assets/linux/70-qtdmm.rules) is a stable path of its own.
namespace HidPath
{
  /// One HID device as hidapi lists it.
  struct Candidate
  {
    unsigned short vendorId = 0;
    unsigned short productId = 0;
    QString path;
  };

  /// What a port entry says: "HID 0xvvvv:0xpppp path", "0xvvvv:0xpppp path"
  /// or a bare path (no ids: nothing to look for).
  struct Entry
  {
    bool hasIds = false;
    unsigned short vendorId = 0;
    unsigned short productId = 0;
    QString path;
  };
  Entry parse(const QString &entry);

  /// The path to open. @p canonical maps a path to what it points to (a
  /// udev link to its /dev/hidrawN; identity elsewhere). Pure, for tests.
  struct Choice
  {
    QString path;      ///< to open; the entry's own path when nothing better is known
    QString moved;     ///< the entry's path is gone, this is where the cable is now
    bool ambiguous = false;   ///< the path is gone and several cables have these ids
  };
  Choice choose(const Entry &entry, const QList<Candidate> &present, QString (*canonical)(const QString &));

  /// choose() against the devices hidapi lists now.
  Choice resolve(const QString &entry);

  /// The udev link in @p dir that points to @p path (Linux; empty elsewhere).
  QString linkFor(const QString &path, const QString &dir = QStringLiteral("/dev/qtdmm"));
}
