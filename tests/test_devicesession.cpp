// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// DeviceSession: meters of "My devices" opened in the process without a
// window, next to the window's meter, on one SCPI server; their recordings
// follow the window's. Simulated meters only, no hardware.

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>

#include "core/devicelibrary.h"
#include "core/reading.h"
#include "core/readingadapter.h"
#include "core/sampletypes.h"
#include "recording/recordingstore.h"
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

  // 2. refused: twice, unknown, the window's
  check(!session.open(sim, &error) && error.startsWith("-221,"), "open twice: " + error);
  check(!session.open("nosuch", &error) && error.startsWith("-224,"), "unknown id: " + error);
  check(!session.close("w1", &error) && error.startsWith("-221,"), "the window's meter is not closed here: " + error);
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
  // a second of the same name is POWER_2, and stays so when the first closes
  const QString twin = lib.add("power", {{"DMM/model", "QtDMM Simulated meter"}, {"Port settings/device", "calc V/DC 7"},
                                         {"DMM/virtual-waveform", 0}, {"DMM/virtual-min", "7"}, {"DMM/virtual-max", "7"}});
  check(session.open(twin) && ask("INST:CAT?").endsWith("\"POWER\",\"POWER_2\""), "the same name twice: " + ask("INST:CAT?"));
  // the meter chosen closed: none chosen, not silently another one
  ask("INST POWER");
  ask(QString("QTDMM:DEV:CLOS \"%1\"").arg(calc).toLatin1());
  check(!session.controller("device:" + calc) && ask("INST:CAT?") == "\"WINDOW_METER\",\"BENCH_SIM\",\"POWER_2\"",
        "QTDMM:DEV:CLOS: " + ask("INST:CAT?"));
  check(ask("INST?") == "\"\"" && ask("MEAS?") == "9.91E+37", "the meter chosen closed: none: " + ask("INST?"));
  ask("*CLS");
  ask("INIT");
  check(ask("SYST:ERR?").startsWith("-241,"), "the meter chosen closed: INIT -241");
  session.close(twin);

  // 4. recording: the window's starts and stops the meter here, on its time
  // base; INIT and ABOR on the meter here start and stop the window's (as
  // its Record button: the window's start there)
  RecordingStore *lead = window.recorder();
  RecordingStore *mine = ctl->recorder();
  QObject::connect(&window, &MeterController::recordingRequested, lead, [lead](bool start)
  {
    if (start)
      lead->start();
    else
      lead->stop();
  });
  auto settle = [] { for (int i = 0; i < 3; ++i) QCoreApplication::processEvents(); };
  lead->start();
  check(mine->isRunning() && mine->startClock() == lead->startClock() && mine->startDateTime() == lead->startDateTime(),
        "the window's start starts the meter here, at the same moment");
  lead->stop();
  check(mine->state() == RecordingStore::View, "the window's stop stops it");
  ask("INST BENCH_SIM");
  ask("*CLS");
  ask("INIT");
  settle();
  check(lead->isRunning() && mine->isRunning() && ask("SYST:ERR?").startsWith("0,"), "INIT here starts the window's");
  check(waitFor([&] { return mine->count() >= 2; }, 4000), QString("readings recorded: %1").arg(mine->count()));
  check(mine->unit() == "V", "the unit of the meter here: " + mine->unit());
  ask("ABOR");
  settle();
  check(!lead->isRunning() && mine->state() == RecordingStore::View && mine->dirty(), "ABOR here stops the window's");
  // saved into the folder for recordings, by name only, nothing overwritten
  ctl->setSaveDir(dir.path() + "/rec");
  ask("QTDMM:REC:SAVE \"bench\"");
  check(ask("SYST:ERR?").startsWith("0,") && QFile::exists(dir.path() + "/rec/bench.csv") && !mine->dirty(),
        "QTDMM:REC:SAVE");
  ask("QTDMM:REC:SAVE \"bench.csv\",RAW");
  check(ask("SYST:ERR?").startsWith("-257,"), "QTDMM:REC:SAVE: no file overwritten");
  ask("QTDMM:REC:SAVE \"../bench\"");
  check(ask("SYST:ERR?").startsWith("-257,"), "QTDMM:REC:SAVE: a file name only");
  ask("QTDMM:REC:SAVE \"raw.csv\", raw");
  check(ask("SYST:ERR?").startsWith("0,") && QFile::exists(dir.path() + "/rec/raw.csv"), "QTDMM:REC:SAVE ..., RAW");
  // one not saved is overwritten, with a message
  QString replaced;
  {
    QObject::connect(&session, &DeviceSession::error, &session, [&replaced](const QString &text) { replaced = text; });
    lead->start();
    check(replaced.isEmpty(), "saved: no message at the next start: " + replaced);
    lead->stop();
    lead->start();
    check(mine->isRunning() && replaced.contains("Bench sim"), "not saved: replaced, said so: " + replaced);
    lead->stop();
  }
  // the window Live: the meter here too, so that a start reaching back
  // (the pre-trigger) has its readings
  lead->live();
  check(mine->state() == RecordingStore::Live, "the window Live: the meter here too");
  check(waitFor([&] { return mine->count() >= 2; }, 4000), "Live: readings");
  lead->startAt(Sample::now() - 2000, QDateTime::currentDateTime().addMSecs(-2000));
  check(mine->isRunning() && mine->count() >= 2 && mine->series().last().t >= 1500,
        QString("started 2 s back: its Live readings, the last at %1 ms").arg(mine->count() ? mine->series().last().t : -1));
  lead->stop();
  // opened while the window records: it joins on the window's time base
  lead->start();
  ask(QString("QTDMM:DEV:OPEN \"%1\"").arg(calc).toLatin1());
  if (MeterController *power = session.controller("device:" + calc))
  {
    check(power->recorder()->isRunning() && power->recorder()->startClock() == lead->startClock(), "opened while recording: joins");
    session.close(calc);
  }
  lead->stop();

  // 5. the window takes the meter open in the background: it is closed
  QString message;
  QObject::connect(&session, &DeviceSession::error, [&message](const QString &text) { message = text; });
  session.release(sim);
  check(!session.controller("device:" + sim) && message.contains("Bench sim"), "release: closed, said so: " + message);
  check(session.controllers().size() == 1, "only the window's meter left");
  // the window alone: its time begins with its first reading, as before
  {
    lead->start();
    waitFor([] { return false; }, 50);
    ReadingAdapter adapter;
    Reading r = ReadingAdapter::reading(adapter.adaptValue(1, "1.000", "V", "DC", "AUTO", false, true, false, 0,
                                                           QDateTime::currentMSecsSinceEpoch()));
    lead->setReading(r);
    check(lead->count() == 1 && lead->series().first().t == 0 && lead->startClock() == r.t,
          QString("the window alone: from its first reading, %1 ms").arg(lead->count() ? lead->series().first().t : -1));
    lead->stop();
  }

  // 6. no window (a later program without one): INIT on one of the meters
  // starts all of the process at one moment, ABOR stops them
  {
    const QString two = lib.add("Bench two", {{"DMM/model", "QtDMM Simulated meter"}, {"Port settings/device", "calc V/DC 2"},
                                              {"DMM/virtual-waveform", 0}, {"DMM/virtual-min", "2"}, {"DMM/virtual-max", "2"}});
    DeviceSession alone;
    alone.setInstanceId("alone");
    alone.setConfigDir(dir.path());
    MeterController *a = alone.open(sim);
    MeterController *b = alone.open(two);
    check(a && b && alone.controllers().size() == 2, "no window: two meters");
    if (a && b)
    {
      auto tell = [&](const char *line) { return QString::fromLatin1(alone.host()->scpi()->process(line)).trimmed(); };
      tell("INST BENCH_TWO");
      tell("INIT");
      settle();
      check(a->recorder()->isRunning() && b->recorder()->isRunning()
              && a->recorder()->startClock() == b->recorder()->startClock(),
            "no window: INIT starts both at one moment");
      tell("ABOR");
      settle();
      check(a->recorder()->state() == RecordingStore::View && b->recorder()->state() == RecordingStore::View,
            "no window: ABOR stops both");
    }
  }

  return failed == 0 ? 0 : 1;
}
