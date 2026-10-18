// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// MeterController behind its SCPI server, over TCP on localhost: what a
// command asks of the UI (connect, record) runs after the command, and the
// checks count what is still on its way - "INP ON;INP ON" connects once,
// "INIT;INIT" starts once and leaves -213 (review R10-05).

#include <QtCore>
#include <QtNetwork>

#include "service/metercontroller.h"
#include "testutil.h"

// lets the queued work run
static void settle(int ms = 200)
{
  waitFor([] { return false; }, ms);
}

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  MeterController ctl;
  ScpiConfig config;
  config.enabled = true;
  config.port = 0;   // the system picks one: a fixed port may be in use
  ctl.applyScpi(config);
  quint16 port = 0;
  for (const ServiceState &s : ctl.services())
    if (s.id == QLatin1String("scpi"))
      port = s.port;
  check(port != 0, "the SCPI server listens");

  QList<bool> connects;
  QList<bool> recordings;
  QObject::connect(&ctl, &MeterController::connectRequested, [&](bool on) { connects << on; });
  QObject::connect(&ctl, &MeterController::recordingRequested, [&](bool start) { recordings << start; });

  QTcpSocket c;
  c.connectToHost(QHostAddress::LocalHost, port);
  check(waitFor([&] { return c.state() == QAbstractSocket::ConnectedState; }), "connected");
  auto query = [&](const QByteArray &line)
  {
    c.write(line + "\n");
    c.flush();
    if (!waitFor([&] { return c.canReadLine(); }))
      return QString();
    return QString::fromLatin1(c.readLine()).trimmed();
  };
  auto send = [&](const QByteArray &line)
  {
    c.write(line + "\n");
    c.flush();
    settle();
  };

  // nothing runs inside the command, once after it
  send("INP ON;INP ON");
  check(connects == QList<bool>{ true }, QString("INP ON;INP ON: %1 connect request(s)").arg(connects.size()));
  connects.clear();
  // no UI opened the meter: it is not wanted, OFF has nothing to do
  send("INP OFF");
  check(connects.isEmpty(), "INP OFF while not wanted: nothing");

  send("INIT;INIT");
  check(recordings == QList<bool>{ true }, QString("INIT;INIT: %1 start(s)").arg(recordings.size()));
  check(query("SYST:ERR?").startsWith("-213"), "the second INIT leaves -213");
  recordings.clear();
  // once the start has run, the next INIT is checked against the recorder again
  send("INIT");
  check(recordings == QList<bool>{ true }, "INIT after the start ran: checked anew");

  if (failed)
  {
    qWarning() << failed << "MeterController check(s) failed";
    return 1;
  }
  qInfo() << "All MeterController tests passed.";
  return 0;
}
