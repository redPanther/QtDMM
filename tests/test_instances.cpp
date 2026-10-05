// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The instances without a running second QtDMM, in a temporary config
// directory: names, renaming a stopped one with the formulas that use it
// (other instances and My devices), and a new instance that copies all but
// the meter.

#include <QtCore>
#include <QTemporaryDir>

#include "core/calcexpr.h"
#include "core/devicelibrary.h"
#include "core/instances.h"
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

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  app.setApplicationName("qtdmm_test");

  QTemporaryDir dir;
  check(dir.isValid(), "temporary config dir");

  // --- 1. names: they are variables in formulas ---
  check(Instances::isValidName("u") && Instances::isValidName("psu_1") && Instances::isValidName("_x"), "valid names");
  check(!Instances::isValidName("1u") && !Instances::isValidName("uni-t") && !Instances::isValidName("a b")
          && !Instances::isValidName("") && !Instances::isValidName("default"), "invalid names");

  // --- 2. a variable renamed in a formula: whole names only ---
  check(CalcExpr::renameVariable("u * i", "u", "volt") == "volt * i", "simple");
  check(CalcExpr::renameVariable("u*u+uu-u_1", "u", "v") == "v*v+uu-u_1", "whole names only");
  check(CalcExpr::renameVariable("2u * 1e-3 + u", "u", "v") == "2u * 1e-3 + v", "not in numbers");
  check(CalcExpr::renameVariable("abs (abs) + max(abs, 2)", "abs", "a") == "abs (a) + max(a, 2)",
        "not a function of that name");
  check(CalcExpr::renameVariable("u *", "u", "v") == "v *", "also a formula that does not parse");

  // --- 3. renaming a stopped instance ---
  Settings settings("default", dir.path());
  settings.setString("DMM/model", "UT61E");
  settings.save();
  QString uFile;
  {
    Settings u("u", dir.path());
    u.setString("DMM/model", "UT803");
    u.save();
    uFile = u.fileName();
    Settings p("p", dir.path());
    p.setString("DMM/calc-expression", "u * i");
    p.setString("DMM/calc-unit", "W");
    p.setString("Port settings/device", "calc W u * i");
    p.save();
    Settings other("other", dir.path());
    other.setString("DMM/calc-expression", "i * 2");
    other.setString("Port settings/device", "calc A i * 2");
    other.save();
  }
  DeviceLibrary library(dir.path());
  const QString power = library.add("Power", { { "DMM/model", "QtDMM Calculated value" },
                                               { "DMM/calc-expression", "sqrt(u^2)" },
                                               { "Port settings/device", "calc V sqrt(u^2)" } });

  QString error;
  QStringList changed = Instances::rename(settings, &library, "u", "1u", &error);
  check(changed.isEmpty() && !error.isEmpty() && QFile::exists(uFile), "invalid new name refused: " + error);
  error.clear();
  changed = Instances::rename(settings, &library, "u", "p", &error);
  check(!error.isEmpty() && QFile::exists(uFile), "taken name refused: " + error);
  error.clear();
  changed = Instances::rename(settings, &library, "u", "P", &error);
  check(!error.isEmpty() && QFile::exists(uFile), "a taken name in another case refused: " + error);
  error.clear();
  changed = Instances::rename(settings, &library, "u", "volt", &error);
  check(error.isEmpty(), "renamed: " + error);
  check(!QFile::exists(uFile), "old file gone");
  check(settings.getConfigInstances().contains("volt") && !settings.getConfigInstances().contains("u"),
        "listed under the new name: " + settings.getConfigInstances().join(','));
  check(changed == QStringList({ "p" }), "changed formulas: " + changed.join(','));
  {
    Settings p("p", dir.path());
    check(p.getString("DMM/calc-expression") == "volt * i", "formula: " + p.getString("DMM/calc-expression"));
    check(p.getString("Port settings/device") == "calc W volt * i", "port: " + p.getString("Port settings/device"));
    Settings volt("volt", dir.path());
    check(volt.getString("DMM/model") == "UT803", "settings moved with it");
    Settings other("other", dir.path());
    check(other.getString("Port settings/device") == "calc A i * 2", "others unchanged");
  }
  check(library.find(power)->keys.value("DMM/calc-expression") == "sqrt(volt^2)", "My devices: formula");
  check(library.find(power)->keys.value("Port settings/device") == "calc V sqrt(volt^2)", "My devices: port");
  // only the case of its own name changes: allowed
  changed = Instances::rename(settings, &library, "volt", "Volt", &error);
  check(error.isEmpty() && settings.getConfigInstances().contains("Volt"), "case of its own name: " + error);
  error.clear();
  check(!settings.renameConfig("default", "x"), "default is not renamed");
  // the unit stays even when it is called like the instance
  check(Instances::renamedInFormula({ { "Port settings/device", "calc W W * 2" } }, "W", "w")
          .value("Port settings/device") == "calc W w * 2", "the unit stays");

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
    qInfo() << "All instances tests passed.";
  else
    qWarning() << failed << "instances test(s) failed.";
  return failed == 0 ? 0 : 1;
}
