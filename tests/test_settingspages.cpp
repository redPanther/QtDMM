// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings pages after the redesign (26.2): the keys go through a page
// unchanged, fields that depend on a choice come with it, and the
// recording length - asked when Record is pressed, no longer on a page -
// survives OK and a page reset.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QRadioButton>
#include <QSpinBox>
#include <QTemporaryDir>

#include "core/settings.h"
#include "ui/dialogs/recordlengthdlg.h"
#include "ui/settings/generalprefs.h"
#include "ui/settings/graphprefs.h"
#include "ui/settings/guiprefs.h"
#include "ui/settings/recorderprefs.h"

static int failed = 0;

static void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAILED:" << what;
    failed++;
  }
}

template <typename T> static T *child(QWidget &page, const char *name)
{
  T *w = page.findChild<T *>(name);
  check(w, QString("no %1").arg(name));
  return w;
}

// shown on the page (with the row around it)
static bool shown(QWidget *w)
{
  return w && w->isVisible();
}

int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir dir;

  // --- 1. the keys go through the pages unchanged: loaded, applied, saved
  //        - values off the defaults, so a page that dropped one shows ---
  {
    Settings cfg("pages", dir.path());
    const QList<QPair<QString, QVariant>> keys = {
      { "Alert/unsaved-file", false }, { "Save/window-pos", false }, { "Icons/text-label", true },
      { "Port settings/sigrok_exe", QString("/opt/sigrok-cli") },
      { "Windows/design", QString("silver") }, { "Display/display-bar", false }, { "Display/display-min-max", true },
      { "Meter/scale-mode", 2 }, { "Meter/style", 0 }, { "Meter/ballistics", false }, { "Meter/red-zone", 75 },
      { "Graph/variant", QString("custom") }, { "Graph/line-mode", 2 }, { "Graph/point-mode", 3 },
      { "Graph/line-width", 4 }, { "Graph/crosshair-cursor", false },
      { "Scale/automatic", false }, { "Scale/automatic-include-zero", false },
      { "Scale/minimum", QString("-2") }, { "Scale/maximum", QString("7k") },
      { "Window/size", 45 }, { "Window/size-unit", 1 },
      { "Graph/show-integration", true }, { "Graph/int-line-mode", 1 }, { "Graph/int-point-mode", 4 },
      { "Graph/int-line-width", 3 }, { "Graph/int-scale", QString("0.5") }, { "Graph/int-offset", QString("1") },
      { "Graph/int-threshold", QString("2m") },
      { "Sample/rate", 5 }, { "Sample/rate-unit", 2 }, { "Sample/time", 90 }, { "Sample/time-unit", 1 },
      { "Start/mode", int(GraphWidget::Falling) }, { "Start/hour", 13 }, { "Start/minute", 14 }, { "Start/second", 15 },
      { "Start/raising-threshold", QString("1.5") }, { "Start/falling-threshold", QString("-3") },
      { "Start/pre-trigger", true }, { "Start/pre-trigger-time", 7 }, { "Start/pre-trigger-unit", 1 } };
    for (const auto &[key, value] : keys)
    {
      if (value.typeId() == QMetaType::Bool)
        cfg.setBool(key, value.toBool());
      else if (value.typeId() == QMetaType::Int)
        cfg.setInt(key, value.toInt());
      else
        cfg.setString(key, value.toString());
    }
    cfg.save();

    GeneralPrefs general;
    GuiPrefs gui;
    GraphPrefs graph;
    RecorderPrefs recorder;
    for (SettingsPage *p : std::initializer_list<SettingsPage *>{ &general, &gui, &graph, &recorder })
    {
      p->setCfg(&cfg);
      p->defaultsSLOT();
    }
    // the keys away, then back from the pages
    Settings blank("pages_out", dir.path());
    for (SettingsPage *p : std::initializer_list<SettingsPage *>{ &general, &gui, &graph, &recorder })
    {
      p->setCfg(&blank);
      p->applySLOT();
    }
    blank.save();
    for (const auto &[key, value] : keys)
    {
      const QString got = value.typeId() == QMetaType::Bool ? QString::number(blank.getBool(key, !value.toBool()))
                          : value.typeId() == QMetaType::Int ? QString::number(blank.getInt(key, -99))
                                                             : blank.getString(key, "-");
      const QString want = value.typeId() == QMetaType::Bool ? QString::number(value.toBool()) : value.toString();
      check(got == want, QString("keys: %1 came back as %2, expected %3").arg(key, got, want));
    }

    // what the dialog reads
    check(graph.windowSeconds() == 45 * 60 && !graph.automaticScale() && graph.scaleMax() == 7000,
          "graph: window or Y axis");
    check(recorder.sampleLength() == 90 * 60 * 10 && recorder.sampleMode() == GraphWidget::Falling
            && recorder.startTime() == QTime(13, 14, 15) && recorder.preTrigger() == 7 * 60 * 1000,
          "recorder: length, start or pre-trigger");

    // --- 2. what comes with a choice ---
    graph.show();
    recorder.show();
    QApplication::processEvents();
    check(shown(child<QWidget>(graph, "ui_scaleMin")) && shown(child<QWidget>(graph, "ui_intScale")),
          "graph: fixed axis and integration show their fields");
    check(shown(child<QWidget>(graph, "ui_dataColor")), "graph: Custom shows the own colours");
    child<QComboBox>(graph, "ui_variant")->setCurrentIndex(0);
    child<QRadioButton>(graph, "autoScaleBut")->setChecked(true);
    child<QCheckBox>(graph, "ui_showInt")->setChecked(false);
    check(!shown(child<QWidget>(graph, "ui_dataColor")), "graph: the own colours go with Custom");
    check(!shown(child<QWidget>(graph, "ui_scaleMin")), "graph: the limits go with the fixed axis");
    check(!shown(child<QWidget>(graph, "ui_intScale")), "graph: the integration fields go with it");
    check(shown(child<QWidget>(graph, "ui_cursorColor")), "graph: the cursor colour stays");

    check(shown(child<QWidget>(recorder, "ui_fallingThreshold")) && !shown(child<QWidget>(recorder, "ui_raisingThreshold"))
            && shown(child<QWidget>(recorder, "ui_preTrigger")) && !shown(child<QWidget>(recorder, "ui_startTime")),
          "recorder: the falling threshold and the pre-trigger with the threshold start");
    child<QRadioButton>(recorder, "manualBut")->setChecked(true);
    check(!shown(child<QWidget>(recorder, "ui_fallingThreshold")), "recorder: by hand shows no threshold");
    check(!shown(child<QWidget>(recorder, "ui_preTrigger")), "recorder: by hand shows no pre-trigger");
    child<QRadioButton>(recorder, "predefinedBut")->setChecked(true);
    check(shown(child<QWidget>(recorder, "ui_startTime")), "recorder: the clock time comes with its start");

    // --- 3. the recording length: not on the page, kept by it ---
    recorder.setLength(3, 2);
    recorder.factoryDefaultsSLOT();
    check(recorder.lengthValue() == 3 && recorder.lengthUnit() == 2 && recorder.sampleLength() == 3 * 3600 * 10,
          "length: a page reset keeps it");
    recorder.applySLOT();
    blank.save();
    check(blank.getInt("Sample/time") == 3 && blank.getInt("Sample/time-unit") == 2, "length: OK writes it back");
    recorder.setLength(0, 0);
    check(recorder.sampleLength() == 0, "length: 0 records until stopped");
  }

  // --- 4. the window Record asks with ---
  {
    RecordLengthDlg dlg(25, 1);
    check(dlg.value() == 25 && dlg.unit() == 1, "length dialog: the last length ready");
    dlg.findChild<QSpinBox *>("ui_length")->setValue(0);
    check(dlg.value() == 0 && dlg.findChild<QSpinBox *>("ui_length")->text() == QStringLiteral("∞"),
          "length dialog: 0 is ∞");
  }

  if (failed)
    qWarning() << failed << "settings page test(s) failed.";
  else
    qInfo() << "settings pages: all passed";
  return failed ? 1 : 0;
}
