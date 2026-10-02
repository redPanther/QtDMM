// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Instances dialog without a running second QtDMM: the list is built from the
// config files in a temporary config directory, delete mode removes the
// selected instance's file, a calculated instance is created with the keys
// DMM and MainWin rely on, and a new instance copies all but the meter.

#include <QtWidgets>
#include <QTemporaryDir>

#include "ui/dialogs/instancesdlg.h"
#include "core/settings.h"

static int failed = 0;

static void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAIL:" << what;
    ++failed;
  }
}

static QStringList instanceButtons(QDialog &dlg)
{
  // QListWidget::clear() deletes the row widgets deferred
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QStringList names;
  for (QPushButton *b : dlg.findChildren<QPushButton *>())
    if (b->property("instanceId").isValid())
      names << b->property("instanceId").toString();
  return names;
}

int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  app.setApplicationName("qtdmm_test");

  QTemporaryDir dir;
  check(dir.isValid(), "temporary config dir");

  // --- 1. two instance configs on disk -> both listed ---
  Settings settings("default", dir.path());
  settings.setString("DMM/model", "UT61E");
  settings.save();
  QString probeFile;
  {
    Settings probe("probe", dir.path());
    probe.setString("DMM/model", "UT803");
    probe.save();
    probeFile = probe.fileName();
  }
  check(QFile::exists(probeFile), "probe config written: " + probeFile);

  InstancesDlg dlg(&settings, "default", dir.path());
  dlg.setInstancesOnline({"default"});
  QStringList listed = instanceButtons(dlg);
  check(listed.contains("default") && listed.contains("probe"),
        "both instances listed: " + listed.join(','));

  // --- 2. delete mode: check "probe", press "-" again -> file gone ---
  auto *del = dlg.findChild<QToolButton *>("ui_instance_del");
  check(del != nullptr, "delete button found");
  if (del)
  {
    check(del->isCheckable() && !del->isChecked(), "delete button is a toggle");
    del->click();          // enter delete mode; buttons become checkable
    QPushButton *probeBtn = nullptr;
    for (QPushButton *b : dlg.findChildren<QPushButton *>())
      if (b->property("instanceId").toString() == "probe")
        probeBtn = b;
    check(probeBtn && probeBtn->isCheckable(), "probe button checkable in delete mode");
    if (probeBtn)
      probeBtn->setChecked(true);
    del->click();          // leave delete mode: delete what is checked
    check(!QFile::exists(probeFile), "probe config removed");
    check(!instanceButtons(dlg).contains("probe"), "probe no longer listed");
  }

  // --- 3. a calculated instance gets the keys the new process needs ---
  const QString calcFile = InstancesDlg::createCalculatedInstance("p", dir.path(), "W", "u * i", false);
  check(QFile::exists(calcFile), "calc config created: " + calcFile);
  {
    Settings calc("p", dir.path());
    check(calc.getString("DMM/model") == "QtDMM Calculated value", "model key");
    check(calc.getBool("DMM/configured"), "configured flag set (auto-connect at first start)");
    check(calc.getString("Port settings/device") == "calc W u * i", "device string");
    check(calc.getString("DMM/calc-expression") == "u * i", "formula key");
  }

  // --- 4. a new instance copies everything but the meter ---
  settings.setString("Port settings/device", "Serial /dev/ttyUSB0");
  settings.setString("Port settings/ble-key", "00112233445566778899aabbccddeeff");
  settings.setString("Port settings/custom_device0", "RFC2217 bench:4000");
  settings.setInt("Port settings/baud", 19200);
  settings.setInt("Graph/sample-time", 5);
  settings.setColor("Graph/background", QColor("#123456"));
  settings.setInt("Position/x", 100);
  settings.setInt("Position/width", 800);
  settings.setBool("Scpi/enabled", true);
  settings.setInt("Scpi/port", 5025);
  settings.save();
  const QString copyFile = settings.copyConfig("copy");
  check(QFile::exists(copyFile), "copied config written: " + copyFile);
  {
    Settings copy("copy", dir.path());
    check(copy.fileExists(), "copy exists (no welcome dialog)");
    check(copy.getInt("Graph/sample-time") == 5, "graph setting copied");
    check(copy.getColor("Graph/background") == QColor("#123456"), "colour copied");
    check(copy.getInt("Position/width") == 800, "window size copied");
    check(copy.getInt("Scpi/port") == 5025, "SCPI port copied");
    check(copy.getString("Port settings/custom_device0") == "RFC2217 bench:4000", "custom port list copied");
    check(copy.getString("DMM/model").isEmpty(), "model not copied");
    check(copy.getString("Port settings/device").isEmpty(), "device not copied");
    check(copy.getString("Port settings/ble-key").isEmpty(), "BLE key not copied");
    check(copy.getInt("Port settings/baud", -1) == -1, "baud rate not copied");
    check(copy.getInt("Position/x", -1) == -1, "window position not copied");
    check(!copy.getBool("Scpi/enabled"), "SCPI server not enabled in the copy");
  }

  if (failed == 0)
    qInfo() << "All instances dialog tests passed.";
  else
    qWarning() << failed << "instances dialog test(s) failed.";
  return failed == 0 ? 0 : 1;
}
