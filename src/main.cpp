//======================================================================
// File:		main.cpp
// Author: Matthias Toussaint
// Created: Tue Apr 10 17:36:39 CEST 2001
//----------------------------------------------------------------------
// This file is part of QtDMM.
//
// QtDMM is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// QtDMM is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with QtDMM. If not, see <http://www.gnu.org/licenses/>.
//----------------------------------------------------------------------
// Copyright (c) 2001 Matthias Toussaint
//======================================================================
#include <QLockFile>
#include <QMessageBox>
#include <QtGui>
#include <iostream>

#include "ui/mainwindow.h"
#include "ui/mnemoniccheck.h"
#include "ui/dialogs/adddevicedlg.h"
#include "core/applog.h"
#include "core/settings.h"
#include "ui/views/qcpgraphcommon.h"
#include "service/metercontroller.h"

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
  // and into the log the user can attach to a report
  const AppLog::Level level = type == QtDebugMsg ? AppLog::Debug : type == QtInfoMsg ? AppLog::Info
                              : type == QtWarningMsg ? AppLog::Warning : AppLog::Error;
  const QLatin1String category(context.category ? context.category : "default");
  AppLog::write(level, category, msg);
}

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
  attachParentConsole();
#endif
  QApplication app(argc, argv);
  qInstallMessageHandler(qtdmmMessageOutput);
  // ... (remaining main body)
}
