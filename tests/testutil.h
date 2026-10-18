// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the test programs share: the failure count, check() and waitFor().
// main() returns failed == 0 ? 0 : 1.

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QString>

#include <functional>

inline int failed = 0;

/// Counts a failure when @p cond is false and says @p what.
inline void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAILED:" << what;
    failed++;
  }
}

/// Runs the event loop until @p cond holds; false after @p ms without.
inline bool waitFor(const std::function<bool()> &cond, int ms = 2000)
{
  QElapsedTimer t;
  t.start();
  while (!cond())
  {
    if (t.elapsed() > ms)
      return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  return true;
}
