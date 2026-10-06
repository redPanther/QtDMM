// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ControlBar, the meter's keys under the display: which keys a meter has,
// what a click sends, holding MIN/MAX leaves it, and the states the
// readings report.
#include <QApplication>
#include <QDebug>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

#include "core/reading.h"
#include "ui/controlbar.h"

static int failed = 0;

static void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAILED:" << what;
    failed++;
  }
}

static QToolButton *key(ControlBar &bar, const QString &name)
{
  for (QToolButton *b : bar.findChildren<QToolButton *>())
    if (b->property("key").toString() == name)
      return b;
  return nullptr;
}

int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);

  ControlBar bar;
  bar.show();
  bar.setConnected(true);

  // --- 1. the keys the meter has: no PEAK on the UT60BT, on the UT61x+ ---
  bar.setProtocol(FrameFormat::UniTiDMM);
  check(key(bar, "hold") && !key(bar, "hold")->isHidden(), "UT60BT: HOLD shown");
  check(key(bar, "lamp") && !key(bar, "lamp")->isHidden(), "UT60BT: LIGHT shown");
  check(key(bar, "peak") && key(bar, "peak")->isHidden(), "UT60BT: no PEAK");
  bar.setProtocol(FrameFormat::UniTUT61Plus);
  check(!key(bar, "peak")->isHidden(), "UT61x+: PEAK shown");
  bar.setProtocol(FrameFormat::UniTiDMM);

  // --- 2. a click sends the key; MIN/MAX held while on leaves it ---
  QSignalSpy sent(&bar, &ControlBar::keyPressed);
  QTest::mouseClick(key(bar, "hold"), Qt::LeftButton);
  QTest::mouseClick(key(bar, "minmax"), Qt::LeftButton);
  check(sent.size() == 2 && sent[0][0].toString() == "hold" && sent[1][0].toString() == "minmax",
        "click: hold, minmax");
  QToolButton *minmax = key(bar, "minmax");
  check(minmax->isChecked(), "minmax: shown on after the click");
  QTest::mousePress(minmax, Qt::LeftButton);
  QTest::qWait(ControlBar::kLongPressMs + 100);
  QTest::mouseRelease(minmax, Qt::LeftButton);
  check(sent.size() == 3 && sent[2][0].toString() == "minmax_off", "minmax held: minmax_off, got " + sent.last()[0].toString());
  check(!minmax->isChecked(), "minmax held: off");
  // held while off: just MIN/MAX
  QTest::mousePress(minmax, Qt::LeftButton);
  QTest::qWait(ControlBar::kLongPressMs + 100);
  QTest::mouseRelease(minmax, Qt::LeftButton);
  check(sent.size() == 4 && sent[3][0].toString() == "minmax", "minmax held while off: minmax");

  // --- 3. the states come from the readings, not from the clicks ---
  Reading r;
  r.hold = true;
  r.range = "MANU";
  r.flags = SampleFlag::Relative | SampleFlag::Min;
  bar.showReading(r);
  check(key(bar, "hold")->isChecked() && !key(bar, "auto")->isChecked() && key(bar, "rel")->isChecked()
          && key(bar, "minmax")->isChecked() && !key(bar, "peak")->isChecked(),
        "states: HOLD, MANU, REL, MIN");
  r = Reading();
  r.range = "AUTO";
  bar.showReading(r);
  check(!key(bar, "hold")->isChecked() && key(bar, "auto")->isChecked() && !key(bar, "rel")->isChecked()
          && !key(bar, "minmax")->isChecked(),
        "states: all off, AUTO");
  // a second value says nothing about the keys
  Reading second;
  second.id = 1;
  second.hold = true;
  bar.showReading(second);
  check(!key(bar, "hold")->isChecked(), "states: a secondary value is ignored");

  // --- 4. not connected: no keys ---
  bar.setConnected(false);
  check(!key(bar, "hold")->isEnabled(), "disconnected: keys off");

  if (failed)
  {
    qWarning() << failed << "ControlBar check(s) failed";
    return 1;
  }
  qInfo() << "All ControlBar tests passed.";
  return 0;
}
