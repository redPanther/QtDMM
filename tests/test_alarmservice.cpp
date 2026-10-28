// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rules of every open meter: AlarmService (each meter's sounds, one
// banner with the meters' names, Acknowledge for all, a meter closed takes
// its sounds along), the front panel in its meter's alarm colour, and the
// window's rules from before 26.4 moving into its entry in My devices.
#include <QApplication>
#include <QDebug>
#include <QTemporaryDir>

#include "core/devicelibrary.h"
#include "core/settings.h"
#include "service/devicesession.h"
#include "service/metercontroller.h"
#include "ui/alarmservice.h"
#include "ui/frontpanel.h"
#include "ui/instancewidget.h"
#include "ui/views/lcdwidget.h"
#include "testutil.h"

int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir dir;
  DeviceLibrary lib(dir.path());
  const QVariantMap simKeys { {"DMM/model", "QtDMM Simulated meter"}, {"Port settings/device", "calc A/DC 2"},
                              {"DMM/virtual-waveform", 0}, {"DMM/virtual-min", "0.25"}, {"DMM/virtual-max", "0.25"} };
  const QString b = lib.add("Sim B", simKeys);

  // 1. two meters, sounds of the one beside, one banner
  {
    DeviceSession session;
    session.setConfigDir(dir.path());
    MeterController window(nullptr, session.host());
    window.setDevice("w1", "Sim A");
    session.adopt(&window);
    QWidget top;
    AlarmService service(&session, &top);
    QStringList played;
    service.setPlayer([&played](const QString &sound) { played << sound; });
    QString banner;
    QObject::connect(&service, &AlarmService::bannerChanged, [&banner](const QString &lines, const QColor &) { banner = lines; });

    MeterController *ctl = session.open(b);
    check(ctl != nullptr, "Sim B open");
    if (!ctl)
      return 1;
    FrontPanel panel(&session);
    panel.setValue(ValueRef(0, ctl->source()));

    Alarm high;
    high.name = "High current";
    high.condition = Alarm::Above;
    high.a = 0.1;
    high.sound = "alarm_warn";
    high.repeat = Alarm::WhileRaised;
    high.color = QColor(0xd8, 0x22, 0x22);
    ctl->setAlarms({ high });
    check(waitFor([&] { return ctl->alarms()->anyRaised(); }, 4000), "Sim B's alarm raised");
    check(played.contains("alarm_warn") && service.sounding(ctl, 0), "its sound plays and goes on");
    check(banner.startsWith("Sim B: High current"), "the banner names the meter: " + banner);
    check(panel.display()->alertColor() == high.color, "its front panel in the alarm's colour");

    service.acknowledgeAll();
    check(ctl->alarms()->state(0) == AlarmManager::Acknowledged && banner.isEmpty(), "Acknowledge: all meters");
    check(waitFor([&] { return !service.sounding(ctl, 0); }, 2500), "the sound stops when acknowledged");

    // the limit test: PASS sounds once
    played.clear();
    LimitTest test;
    test.enabled = true;
    test.form = LimitTest::Range;
    test.lower = 0.2;
    test.upper = 0.3;
    test.passSound = "alarm_ok";
    ctl->setLimitTest(test);
    check(waitFor([&] { return played.contains("alarm_ok"); }, 4000), "PASS on Sim B sounds");

    // closed: its sounds and lines go with it
    ctl->setAlarms({});
    high.name = "Again";
    ctl->setAlarms({ high });
    check(waitFor([&] { return ctl->alarms()->anyRaised(); }, 4000), "raised again");
    session.close(b);
    check(banner.isEmpty() && !service.sounding(ctl, 0), "closed: no line, no sound");
    check(!panel.display()->alertColor().isValid(), "the panel without its meter: no colour");
  }

  // 2. the window's rules from before 26.4 move into its entry, once; the
  //    alarm list of the window then is the entry's
  {
    const QString a = lib.add("Sim A", simKeys);
    Alarm low;
    low.name = "Low";
    {
      Settings old("mig", dir.path());
      old.setValues(simKeys);
      old.setString("DMM/my-device", a);
      old.setString("Alarms/list", Alarm::listToJson({ low }));
      old.save();
      old.sync();
    }
    DeviceSession session;
    session.setConfigDir(dir.path());
    InstanceWidget wid("mig", dir.path(), &session);
    check(wid.controller()->alarms()->alarms().size() == 1, "the window's meter has the old alarm");
    check(Alarm::listFromJson(lib.find(a)->alarms).size() == 1, "it moved into the entry");
    check(wid.settings()->getString("Alarms/list").isEmpty(), "and left the window's settings");
    low.name = "Low 2";
    wid.controller()->setAlarms({ low });
    check(Alarm::listFromJson(lib.find(a)->alarms).value(0).name == "Low 2", "a change goes to the entry");
  }

  if (failed)
  {
    qWarning() << failed << "alarm service check(s) failed";
    return 1;
  }
  qInfo() << "All alarm service tests passed.";
  return 0;
}
