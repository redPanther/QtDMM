// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// DeviceSession: meters of "My devices" opened in the process without a
// window, next to the window's meter, on one SCPI server. Simulated meters
// only, no hardware.

#include <QCoreApplication>
#include <QDateTime>
#include <QTemporaryDir>

#include "core/devicelibrary.h"
#include "core/reading.h"
#include "service/devicesession.h"
#include "service/metercontroller.h"
#include "service/scpiserver.h"
#include "service/servicehost.h"
#include "service/sharedstatemanager.h"
#include "testutil.h"

int main(int argc, char **argv)
{
  // shared memory of its own (test_calc_device does the same)
  qputenv("QTDMM_IPC_KEY", "qtdmm_test_session_" + QByteArray::number(QCoreApplication::applicationPid()));
  QCoreApplication app(argc, argv);
  QTemporaryDir dir;
  DeviceLibrary lib(dir.path());
  const QString sim = lib.add("Bench sim", {{"DMM/model", "QtDMM Simulated meter"}, {"Port settings/device", "calc V/DC 1"},
                                            {"DMM/virtual-waveform", 0}, {"DMM/virtual-min", "1.5"}, {"DMM/virtual-max", "1.5"}});
  const QString calc = lib.add("Power", {{"DMM/model", "QtDMM Calculated value"}, {"Port settings/device", "calc W bench_sim * 2"},
                                         {"DMM/calc-unit", "W"}, {"DMM/calc-expression", "bench_sim * 2"}});

  DeviceSession session;
  session.setInstanceId("test");
  session.setConfigDir(dir.path());
  // the other instances, after the meters of the process
  SharedStateManager state("test"), other("bench_sim"), third("psu");
  check(state.registerInstance() && other.registerInstance() && third.registerInstance(), "instances register");
  session.setStateManager(&state);
  MeterController window(nullptr, session.host());
  window.setInstanceId("test");
  window.setDevice("w1", "Window meter");
  session.adopt(&window);
  auto ask = [&](const char *line) { return QString::fromLatin1(session.host()->scpi()->process(line)).trimmed(); };

  // 1. open from My devices: connected, readings with its source
  QString error;
  MeterController *ctl = session.open(sim, &error);
  check(ctl && error.isEmpty(), "open the simulated meter: " + error);
  if (!ctl)
    return 1;
  Reading last;
  bool got = false;
  QObject::connect(ctl, &MeterController::reading, [&last, &got](const Reading &r) { last = r; got = true; });
  check(waitFor([&] { return got; }, 4000), "a reading of the simulated meter");
  check(last.source == "device:" + sim && qAbs(last.value - 1.5) < 1e-9,
        QString("reading %1 from %2").arg(last.value).arg(last.source));
  check(session.controllers().size() == 2 && session.controllers().first() == &window, "the window's meter first");
  check(session.controller("device:" + sim) == ctl, "found by source");
  check(ask("INST:CAT?") == "\"WINDOW_METER\",\"BENCH_SIM\"", "both meters on one server: " + ask("INST:CAT?"));

  // 2. refused: twice, unknown, the window's; INIT on a meter without a window
  check(!session.open(sim, &error) && error.startsWith("-221,"), "open twice: " + error);
  check(!session.open("nosuch", &error) && error.startsWith("-224,"), "unknown id: " + error);
  check(!session.close("w1", &error) && error.startsWith("-221,"), "the window's meter is not closed here: " + error);
  ask("INST BENCH_SIM");
  ask("*CLS");
  ask("INIT");
  QCoreApplication::processEvents();
  check(ask("SYST:ERR?").startsWith("-213,"), "INIT on a meter without a window: refused");

  // 3. SCPI: catalog, open, close
  const QString cat = ask("QTDMM:DEV:CAT?");
  check(cat.contains(QString("\"%1\",\"Bench sim\",1").arg(sim)) && cat.contains(QString("\"%1\",\"Power\",0").arg(calc)),
        "QTDMM:DEV:CAT?: " + cat);
  ask(QString("QTDMM:DEV:OPEN \"%1\"").arg(calc).toLatin1());
  check(ask("SYST:ERR?").startsWith("0,") && session.controller("device:" + calc), "QTDMM:DEV:OPEN");
  // the calculated value reads the simulated meter of this process by its name
  if (MeterController *power = session.controller("device:" + calc))
  {
    double value = 0;
    QObject::connect(power, &MeterController::reading, [&value](const Reading &r) { value = r.value; });
    check(waitFor([&] { return qAbs(value - 3.0) < 1e-9; }, 4000), QString("bench_sim * 2 = 3: %1").arg(value));
  }
  // a meter of the process wins over an instance of the same name; the
  // others are there as before
  {
    SharedStateManager::Reading r;
    r.value = 9;
    r.unit = "V";
    r.msecs = QDateTime::currentMSecsSinceEpoch();
    r.valid = true;
    other.publishReading(r);
    third.publishReading(r);
    state.checkForChanges();
    const QMap<QString, CalcDevice::Input> in = session.calcInputs()();
    check(in.contains("bench_sim") && qAbs(in.value("bench_sim").value - 1.5) < 1e-9 && in.contains("psu")
            && qAbs(in.value("psu").value - 9) < 1e-9,
          QString("process before instances: bench_sim %1, psu %2").arg(in.value("bench_sim").value).arg(in.value("psu").value));
  }
  check(DeviceSession::variableName("UT61E Bench") == "ut61e_bench" && DeviceSession::variableName("34465A") == "_34465a",
        "variable names");
  check(ask("INST:CAT?") == "\"WINDOW_METER\",\"BENCH_SIM\",\"POWER\"", "three meters: " + ask("INST:CAT?"));
  ask("QTDMM:DEV:OPEN \"nosuch\"");
  check(ask("SYST:ERR?").startsWith("-224,"), "QTDMM:DEV:OPEN of an unknown id");
  ask(QString("QTDMM:DEV:CLOS \"%1\"").arg(calc).toLatin1());
  check(!session.controller("device:" + calc) && ask("INST:CAT?") == "\"WINDOW_METER\",\"BENCH_SIM\"",
        "QTDMM:DEV:CLOS: " + ask("INST:CAT?"));

  // 4. the window takes the meter open in the background: it is closed
  QString message;
  QObject::connect(&session, &DeviceSession::error, [&message](const QString &text) { message = text; });
  session.release(sim);
  check(!session.controller("device:" + sim) && message.contains("Bench sim"), "release: closed, said so: " + message);
  check(session.controllers().size() == 1, "only the window's meter left");

  return failed == 0 ? 0 : 1;
}
