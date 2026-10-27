// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AlarmSound: once, N times, while raised; stops when the alarm no longer
// sounds (cleared, acknowledged) and when the alarms are edited. The sounds
// themselves are counted, not played. The waits end half a second between
// two ticks, so a busy machine may be late by that much. Also: every built-in sound is in the
// resources.

#include <QtCore>
#include <QTest>

#include "ui/alarmsound.h"
#include "testutil.h"

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);

  QSet<int> raised;
  QStringList played;
  AlarmSound sound([&raised](int index) { return raised.contains(index); });
  sound.setPlayer([&played](const QString &s) { played << s; });

  Alarm once; once.sound = "alarm_warn";
  Alarm times; times.sound = "alarm_high"; times.repeat = Alarm::Times; times.repeatCount = 3;
  Alarm whileRaised; whileRaised.sound = "beep"; whileRaised.repeat = Alarm::WhileRaised;
  Alarm silent; silent.sound.clear(); silent.repeat = Alarm::WhileRaised;

  // --- 1. once, and no sound at all ---
  raised = { 0, 3 };
  sound.start(0, once);
  sound.start(3, silent);
  check(played == QStringList{ "alarm_warn" } && !sound.sounding(0), "once: one sound, nothing to come");
  check(!sound.sounding(3), "no sound: nothing plays");

  // --- 2. three times and while raised, side by side, a second apart ---
  played.clear();
  raised = { 1, 2 };
  sound.start(1, times);
  sound.start(2, whileRaised);
  check(played.size() == 2, "both start at once");
  QTest::qWait(2500);
  check(played.count("alarm_high") == 3 && !sound.sounding(1), "three times: " + played.join(' '));
  check(played.count("beep") == 3 && sound.sounding(2), "while raised: goes on: " + played.join(' '));
  raised.remove(2);   // cleared or acknowledged
  QTest::qWait(1500);
  check(played.count("beep") == 3 && !sound.sounding(2), "while raised: stops when the alarm no longer sounds");

  // --- 3. three times, but the alarm clears after the first ---
  played.clear();
  raised = { 1 };
  sound.start(1, times);
  raised.clear();
  QTest::qWait(1500);
  check(played.size() == 1 && !sound.sounding(1), "times: never longer than the alarm is raised");

  // --- 4. edited alarms: all stop; a new raise starts over ---
  played.clear();
  raised = { 2 };
  sound.start(2, whileRaised);
  sound.stopAll();
  QTest::qWait(1500);
  check(played.size() == 1 && !sound.sounding(2), "stopAll");
  sound.start(2, whileRaised);
  sound.start(2, whileRaised);
  QTest::qWait(1500);
  check(played.size() == 4, "raised again: one timer, not two: " + QString::number(played.size()));
  sound.stopAll();

  // --- 5. the built-in sounds are in the resources ---
  for (const auto &[name, shown] : AlarmSound::builtIn())
    check(QFile::exists(QString(":/sounds/%1.wav").arg(name)), "built in: " + name);
  check(QFile::exists(QString(":/sounds/%1.wav").arg(AlarmSound::clearedSound())), "the sound for clearing");

  return failed == 0 ? 0 : 1;
}
