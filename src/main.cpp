//======================================================================
// File:		main.cpp
// Author:	Matthias Toussaint
// Created:	Tue Apr 10 17:36:39 CEST 2001
//----------------------------------------------------------------------
// This file is part of QtDMM.
//
// QtDMM is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 3
// as published by the Free Software Foundation.
//
// QtDMM is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
//----------------------------------------------------------------------
// Copyright (c) 2001 Matthias Toussaint
//======================================================================
#include <QMessageBox>
#include <QtGui>
#include <iostream>

#include "ui/mainwindow.h"
#include "ui/mnemoniccheck.h"
#include "ui/dialogs/adddevicedlg.h"
#include "core/settings.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdio.h>

// QtDMM is a GUI-subsystem executable on Windows, so it has no console of its
// own. When started from cmd/PowerShell, reattach to the parent's console so
// --help, --version and the --debug frame dump remain visible there.
static void attachParentConsole()
{
  if (::AttachConsole(ATTACH_PARENT_PROCESS))
  {
    FILE *f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    // Qt would otherwise route qWarning() & co. to the debugger
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
  }
}
#endif

// Everything goes to stderr, prefixed by severity. Written with fprintf on
// purpose: calling qDebug() & co. from inside the handler re-enters it.
// Debug messages only appear for logging categories switched on by --debug
// (see MainWindow::setConsoleLogging), which keeps release builds quiet.
void qtdmmMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
  const char *prefix = "";
  switch (type)
  {
    case QtDebugMsg:
      prefix = context.category ? context.category : "Debug";
      break;
    case QtInfoMsg:     prefix = "Info"; break;
    case QtWarningMsg:  prefix = "Warning"; break;
    case QtCriticalMsg: prefix = "Critical"; break;
    case QtFatalMsg:    prefix = "Fatal"; break;
  }
  fprintf(stderr, "%s: %s\n", prefix, msg.toLocal8Bit().constData());
  fflush(stderr);
  if (type == QtFatalMsg)
    abort();
}

void initTranslation(QApplication *app,QTranslator *QtTranslation, QTranslator *AppTranslation)
{
  // the texts are English: there is nothing to load for English or the C
  // locale, and no reason to warn about it
  const QLocale locale = QLocale::system();
  const bool english = locale.language() == QLocale::English || locale.language() == QLocale::C;

  if (QtTranslation->load(QString("qt_%1").arg(locale.name()), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
    app->installTranslator(QtTranslation);
  else if (!english)
    qWarning() << "Could not load Qt translation for" << locale.name();

  // compiled in (default), or installed as files by a distribution package
  // (QTDMM_EMBED_TRANSLATIONS=OFF): <prefix>/share/qtdmm/translations
  const QStringList dirs = {":/Translations",
                            QCoreApplication::applicationDirPath() + "/../share/qtdmm/translations"};
  bool loaded = false;
  for (const QString &dir : dirs)
  {
    if (AppTranslation->load(locale, "qtdmm", "_", dir))
    {
      loaded = true;
      break;
    }
  }
  if (loaded)
    app->installTranslator(AppTranslation);
  else if (!english)
    qWarning() << "Could not load App translation for" << locale.name();
}

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
  attachParentConsole();
#endif
  qInstallMessageHandler(qtdmmMessageOutput);
  QApplication app(argc, argv);
  QTranslator QtTranslation;
  QTranslator AppTranslation;
  QCommandLineParser parser;

  app.setApplicationName(APP_NAME);
  app.setApplicationVersion(APP_VERSION);
  app.setOrganizationName(APP_ORGANIZATION);
  // the application id: on Wayland the compositor finds the desktop file,
  // and with it the icon, by this name (not by the program name)
  app.setDesktopFileName("io.github.qtdmm.qtdmm");

  initTranslation(&app,&QtTranslation,&AppTranslation);

  parser.addOption({"debug", QObject::tr("protocol debugging information")});
  parser.addOption({"config-dir",QObject::tr("sets directory where config files are located"), "config-dir"});
  parser.addOption({"config-id",QObject::tr("sets <config-id>"), "config-id"});
  // for the tests: list doubled Alt letters and keys, then quit
  QCommandLineOption checkMnemonics("check-mnemonics");
  checkMnemonics.setFlags(QCommandLineOption::HiddenFromHelp);
  parser.addOption(checkMnemonics);
  parser.addHelpOption();
  parser.addVersionOption();
  parser.process(app);

  if (parser.isSet(checkMnemonics))
  {
    // a config of its own: a missing one would greet with a dialog, and
    // only Settings knows the file's name on each platform
    Settings cfg(parser.value("config-id"), parser.value("config-dir"));
    if (!cfg.fileExists())
    {
      cfg.setInt("QtDMM/version", 0);
      cfg.setInt("QtDMM/revision", 84);
      cfg.save();
    }
  }

  MainWindow mainWin(parser);

  mainWin.show();
  mainWin.move(100, 100);

  if (parser.isSet(checkMnemonics))
  {
    // the assistant opens on demand: one here, so its pages are checked too
    auto *addDevice = new AddDeviceDlg(nullptr, &mainWin);
    addDevice->chooseConnection(AddDeviceDlg::Cable);
    QTimer::singleShot(0, &mainWin, [&mainWin]
    {
      const QStringList conflicts = MnemonicCheck::run(&mainWin);
      for (const QString &c : conflicts)
        std::cout << qPrintable(c) << std::endl;
      std::cout << conflicts.size() << " conflict(s), language " << qPrintable(QLocale::system().name()) << std::endl;
      QCoreApplication::exit(conflicts.isEmpty() ? 0 : 1);
    });
  }

  return app.exec();
}
