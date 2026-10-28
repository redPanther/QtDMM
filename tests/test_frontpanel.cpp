// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// FrontPanel, one meter's LCD with its keys: it shows the meter of its
// value, "no value" while that is not open, the readings when it comes,
// the keys only for a meter that has them, folded on request.
#include <QApplication>
#include <QDebug>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/devicelibrary.h"
#include "core/reading.h"
#include "service/devicesession.h"
#include "service/metercontroller.h"
#include "ui/frontpanel.h"
#include "ui/views/lcdwidget.h"
#include "testutil.h"

int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir dir;
  DeviceLibrary lib(dir.path());
  const QString sim = lib.add("Bench sim", {{"DMM/model", "QtDMM Simulated meter"}, {"Port settings/device", "calc V/DC 1"},
                                            {"DMM/virtual-waveform", 0}, {"DMM/virtual-min", "1.5"}, {"DMM/virtual-max", "1.5"}});
  DeviceSession session;
  session.setInstanceId("test");
  session.setConfigDir(dir.path());

  FrontPanel panel(&session);
  panel.show();
  QSignalSpy keys(&panel, &FrontPanel::keysChanged);

  // 1. the value of a meter not open: no meter, no value, no keys
  panel.setValue(ValueRef(0, "device:" + sim));
  check(panel.controller() == nullptr, "not open: no meter");
  check(panel.display()->lineValue(0).isEmpty() || panel.display()->lineValue(0).startsWith("-"),
        "not open: no value, got " + panel.display()->lineValue(0));
  check(!panel.keysSupported() && panel.keysHeight() == 0, "not open: no keys");

  // 2. opened: its readings on the LCD
  MeterController *ctl = session.open(sim);
  check(ctl && panel.controller() == ctl, "opened: the panel shows it");
  check(waitFor([&] { return panel.display()->lineValue(0).contains("1.5"); }, 4000),
        "its reading on the LCD, got " + panel.display()->lineValue(0));

  // 3. the simulated meter has no keys; folding changes nothing then
  check(!panel.keysSupported() && !panel.keysShown(), "simulated meter: no keys");
  panel.setFolded(true);
  check(panel.isFolded() && keys.isEmpty(), "folded without keys: no change of height");
  panel.setFolded(false);

  // 4. closed: no value again, the value kept
  session.close(sim);
  check(panel.controller() == nullptr && panel.value().source == "device:" + sim, "closed: no meter, value kept");
  check(!panel.display()->lineValue(0).contains("1.5"), "closed: the last reading gone");

  // 5. an empty source: no meter (the window has none of its own)
  session.open(sim);
  panel.setValue(ValueRef(0));
  check(panel.controller() == nullptr, "empty source: no meter");

  if (failed)
  {
    qWarning() << failed << "FrontPanel check(s) failed";
    return 1;
  }
  qInfo() << "All FrontPanel tests passed.";
  return 0;
}
