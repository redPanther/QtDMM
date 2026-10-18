// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

#include "core/sampletypes.h"
#include "service/scpiendpoint.h"

class QTcpServer;
class QTcpSocket;

/// A small SCPI instrument on a raw TCP socket (port 5025 by default), the
/// way bench meters and lxi-tools speak it: one program message per line,
/// commands separated by ';', queries answered in the same order. It turns
/// the meter QtDMM reads into a network instrument, so anything that talks
/// SCPI - lxi-tools, sigrok's scpi-dmm, a Python script - can read a
/// consumer multimeter.
///
/// The server only reports; it never writes to the meter. It knows the
/// current reading (setReading()), whether the meter is connected and
/// whether the recorder runs, and asks for the recorder / connection
/// through signals. process() is the whole command interpreter and is
/// public so the test can drive it without sockets.
class ScpiServer : public QObject
{
  Q_OBJECT
public:
  /// What the meter last delivered, in SI base units.
  struct Reading
  {
    double value = 0;
    QString unit;      ///< "V", "A", "Ohm" ... without prefix
    PortKey port;      ///< what is measured: voltage.dc, current.ac, voltage.diode, ...
    QString range;     ///< "AUTO", "MANU" or the meter's range text
    bool overload = false;
    bool hold = false;
    bool valid = false;
    qint64 msecs = 0;
  };

  explicit ScpiServer(QObject *parent = nullptr);
  ~ScpiServer() override;

  /// Listens on @p address:@p port; when the port is taken the next
  /// @p tries ports are tried (several QtDMM instances on one machine).
  /// Returns false when none could be bound; errorString() says why.
  bool start(const QHostAddress &address, quint16 port, int tries = 10);
  void stop();
  bool isListening() const;
  quint16 port() const;
  QHostAddress address() const;
  int clientCount() const { return m_clients.size(); }
  QString errorString() const { return m_error; }

  /// The meter shown in *IDN? ("UNI-T UT61E"); the manufacturer is QtDMM.
  void setModel(const QString &model) { m_model = model; }
  /// Reading of value @p id (0 = main, 1 = second) as it arrives.
  void setReading(int id, const Reading &reading);
  /// The meter sends readings: false while connecting, after a timeout and
  /// while a lost connection is reopened (STAT:QUES? bit 2, INPut?).
  void setConnected(bool on) { m_connected = on; }
  void setRecording(bool on) { m_recording = on; }
  bool connected() const { return m_connected; }
  bool recording() const { return m_recording; }

  /// Min/max memory of the main value (CALCulate:AVERage), in SI base units.
  struct Stats
  {
    int count = 0;      ///< readings since the last reset; 0: min, max and mean are NaN
    double min = 0;
    double max = 0;
    double mean = 0;
  };
  /// What the graph is doing (QTDMM:RECord?).
  struct RecordingState
  {
    enum Mode { Idle, Live, Record, View };
    Mode mode = Idle;
    double seconds = 0;   ///< from the first to the last reading
    int count = 0;        ///< readings held
    QString unit;         ///< base unit, "V"
  };
  /// One reading of the recording (QTDMM:RECord:DATA?).
  struct Point
  {
    double seconds = 0;    ///< since the start of the recording
    double value = 0;      ///< SI base units; NaN = a gap
    bool overload = false;
  };
  /// One alarm of the meter (QTDMM:ALARm:LIST?).
  struct AlarmState
  {
    enum Level { Off, Pending, Raised, Acknowledged };
    QString name;
    Level level = Off;
  };
  /// A running QtDMM instance with its last reading (QTDMM:INSTances?).
  struct Instance
  {
    QString id;
    double value = 0;
    QString unit;
    bool valid = false;   ///< a current reading, neither held nor overload
  };
  /// Where the QTDMM: queries get what the server does not hold itself. A
  /// source not set answers as if there were nothing.
  struct Sources
  {
    std::function<Stats()> stats;
    std::function<RecordingState()> recording;
    /// The last @p count readings of the recording, oldest first.
    std::function<QList<Point>(int count)> recordingData;
    std::function<QList<AlarmState>()> alarms;
    /// The meter's keys QtDMM can press ("hold", "range", ...), empty for a
    /// meter without.
    std::function<QStringList()> keys;
    /// Presses @p key; false when it could not (the meter not connected).
    std::function<bool(const QString &key)> pressKey;
    std::function<QList<Instance>()> instances;
  };
  void setSources(Sources sources) { m_sources = std::move(sources); }

  /// Largest QTDMM:RECord:DATA? answer, in readings.
  static constexpr int kMaxDataPoints = 10000;

  /// Supplies the screen dump for HCOPy:SDUMp:DATA?: the main window
  /// encoded in @p format ("PNG", "BMP", "JPG"); empty when not possible.
  using ScreenshotSource = std::function<QByteArray(const QByteArray &format)>;
  void setScreenshotSource(ScreenshotSource source) { m_screenshot = std::move(source); }
  /// Wraps @p data as an IEEE 488.2 definite-length block ("#6123456...").
  static QByteArray block(const QByteArray &data);

  /// Runs one program message (without the terminator) and returns the
  /// response: the query answers joined by ';', with a trailing newline,
  /// or nothing when no command was a query. Errors go to the error
  /// queue (SYST:ERR?).
  QByteArray process(const QByteArray &message);
  /// The interpreter for the other transports (HiSLIP): process() and the
  /// status byte; valid as long as this object lives.
  ScpiEndpoint endpoint();
  /// IEEE 488.2 status byte: bit 2 (4) while the error queue holds errors.
  quint8 statusByte() const { return m_errors.isEmpty() ? 0 : 4; }

  /// Number of readings considered current: older ones answer as stale.
  static constexpr qint64 kStaleMs = 5000;

  /// Puts an error into the queue (SYST:ERR?); also for what the receiver
  /// of a command refuses (INIT ignored).
  void pushError(int code, const QString &text);

Q_SIGNALS:
  void clientsChanged(int count);
  /// INITiate / ABORt: the client wants the recorder started or stopped.
  void startRecording();
  void stopRecording();
  /// INPut ON|OFF: the client wants the meter connected or disconnected.
  void connectRequested(bool on);
  /// CALCulate:AVERage:CLEar: start the min/max memory afresh (F4).
  void resetMinMaxRequested();
  /// QTDMM:ALARm:ACKnowledge
  void acknowledgeAlarmsRequested();
  /// QTDMM:MARK "text": a mark in the graph and the readings table.
  void markRequested(const QString &text);

private:
  struct Command
  {
    QStringList path;     ///< mnemonics, upper case, numeric suffix stripped
    QList<int> suffix;    ///< numeric suffix per mnemonic (1 when absent)
    bool query = false;
    QString args;
  };

  void onNewConnection();
  void onReadyRead(QTcpSocket *socket);
  /// deleteLater(), or after the command when one is running (see m_running)
  void release(QTcpSocket *socket);
  QByteArray handle(const Command &cmd, bool &isQuery);
  QString handleText(const Command &cmd, bool &isQuery);
  QString handleCommon(const Command &cmd, bool &isQuery);
  QString handleReading(const Command &cmd, bool &isQuery);
  QString handleStatus(const Command &cmd, bool &isQuery);
  QString handleSystem(const Command &cmd, bool &isQuery);
  /// A refused command: @p code queued, nothing to answer (isQuery false).
  QString refuse(bool &isQuery, int code, const QString &text);
  /// False and -100 queued when @p cmd is not a query, resp. is one.
  bool queryOnly(const Command &cmd, bool &isQuery);
  bool commandOnly(const Command &cmd, bool &isQuery);
  static bool matches(const QString &mnemonic, const char *longForm);
  static QString number(double v);
  QString function(const Reading &r) const;
  QString handleQtdmm(const Command &cmd, bool &isQuery);
  QByteArray recordingData(const Command &cmd, bool &isQuery);
  static QString unquote(const QString &arg);
  const Reading *reading(int channel);

  QTcpServer *m_server;
  QList<QTcpSocket *> m_clients;
  /// A command may spin an event loop (a dialog); a client that goes
  /// meanwhile waits in m_released, deleting its socket there would pull it
  /// from under its own readyRead.
  int m_running = 0;
  QList<QTcpSocket *> m_released;
  QString m_error;
  QString m_model;
  Reading m_readings[2];
  bool m_connected = false;
  bool m_recording = false;
  QList<QPair<int, QString>> m_errors;
  ScreenshotSource m_screenshot;
  Sources m_sources;
  QByteArray m_screenshotFormat = "PNG";
  QStringList m_lastPath;   ///< for the "compound header" rule of ';'
};
