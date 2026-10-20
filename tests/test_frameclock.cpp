// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// FrameClock: runs while a view wants it, off with rate 0, slower when its
// ticks take too long and back up when they leave room.

#include <QApplication>
#include <QElapsedTimer>
#include <QThread>

#include "ui/frameclock.h"
#include "testutil.h"

int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  FrameClock *clock = FrameClock::instance();

  // --- 1. wanted: running; nobody, or rate 0: not ---
  {
    check(!clock->isRunning() && clock->rate() == 30 && clock->interval() == 33, "at first: off, 30 Hz");
    auto *a = new QObject;
    QObject b;
    clock->want(a, true);
    clock->want(&b, true);
    check(clock->isRunning() && clock->wanted(a), "wanted: running");
    clock->want(&b, false);
    check(clock->isRunning(), "one still wants it");
    delete a;
    check(!clock->isRunning(), "the last one destroyed: off");
    clock->want(&b, true);
    clock->setRate(0);
    check(!clock->isRunning(), "rate 0: off, also when wanted");
    clock->setRate(30);
    check(clock->isRunning(), "rate back: on");
    clock->want(&b, false);
  }

  // --- 2. adaptive: ticks that take 40 % of the time and more halve the
  //        rate, down to a quarter; under 10 % it comes back up ---
  {
    int cost = 20;   // ms per tick, of 33
    const auto conn = QObject::connect(clock, &FrameClock::tick, [&]
    {
      QElapsedTimer t;
      t.start();
      while (t.elapsed() < cost)
        QThread::usleep(200);
    });
    for (int i = 0; i < 15; ++i)
      clock->tickNow();
    check(clock->interval() == 66, QString("slow ticks: half the rate, interval %1 ms").arg(clock->interval()));
    cost = 100;
    for (int i = 0; i < 30; ++i)
      clock->tickNow();
    check(clock->interval() == 132, QString("never slower than a quarter: %1 ms").arg(clock->interval()));
    cost = 0;
    for (int i = 0; i < 30; ++i)
      clock->tickNow();
    check(clock->interval() == 33, QString("cheap ticks: back up to the rate, %1 ms").arg(clock->interval()));
    QObject::disconnect(conn);
  }

  if (failed == 0)
    qInfo() << "All FrameClock tests passed.";
  else
    qWarning() << failed << "FrameClock test(s) failed.";
  return failed == 0 ? 0 : 1;
}
