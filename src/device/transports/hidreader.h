// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore>
#include <QLoggingCategory>
#include <atomic>

// Distro packages (Debian, FreeBSD ports) install hidapi.h below hidapi/; the
// hidapi CMake target (Windows via FetchContent) and Homebrew put it top-level.
#if __has_include(<hidapi/hidapi.h>)
  #include <hidapi/hidapi.h>
#else
  #include <hidapi.h>
#endif

/// Low-level trace of the HID transports, enabled by --debug (category
/// "qtdmm.hid"). Shared by HIDSerialDevice and HidHoltekDevice.
Q_DECLARE_LOGGING_CATEGORY(lcHid)

/// The blocking hidapi read loop, living in its own thread. It owns the
/// handle from the moment run() starts until the loop ends, so hid_close()
/// happens exactly once, in the thread that reads. Reports go to the owning
/// device (HIDSerialDevice, HidHoltekDevice) as queued signals; nothing is
/// shared. The reader knows nothing about the protocol: it hands over every
/// input report (up to 64 bytes) unchanged.
class HidReader : public QObject
{
  Q_OBJECT
public:
  HidReader(hid_device *handle);
  /// Ends the loop at its next timeout (100 ms); thread-safe.
  void stop() { m_stop.store(true); }

public Q_SLOTS:
  void run();

Q_SIGNALS:
  /// One input report as read from the cable.
  void report(const QByteArray &raw);
  /// hid_read failed (cable unplugged); the loop has ended.
  void readError(const QString &what);
  /// The loop has ended and the handle is closed.
  void finished();

private:
  hid_device *m_handle;
  std::atomic<bool> m_stop{false};
};

/// The thread of a HidReader, for HIDSerialDevice and HidHoltekDevice. The
/// device stays in the caller's thread, so its readData()/bytesAvailable()/
/// deleteLater() are plain single-threaded code and the reports arrive as
/// queued signals.
class HidReaderThread
{
public:
  HidReaderThread() = default;
  ~HidReaderThread() { stop(); }
  Q_DISABLE_COPY(HidReaderThread)

  /// Runs the read loop for @p handle (owned by the reader from now on) and
  /// queues its reports and its read error to @p device.
  template <class Device>
  void start(hid_device *handle, Device *device, void (Device::*onReport)(const QByteArray &),
             void (Device::*onReadError)(const QString &))
  {
    m_thread = new QThread(device);
    m_reader = new HidReader(handle);
    m_reader->moveToThread(m_thread);
    QObject::connect(m_thread, &QThread::started, m_reader, &HidReader::run);
    QObject::connect(m_reader, &HidReader::report, device, onReport);
    QObject::connect(m_reader, &HidReader::readError, device, onReadError);
    // direct: the thread object lives in the device's thread, and stop()
    // blocks that thread's event loop while it waits for the quit
    QObject::connect(m_reader, &HidReader::finished, m_thread, &QThread::quit, Qt::DirectConnection);
    m_thread->start();
  }
  /// Stops the reader and waits for it; the reader closes the handle.
  /// Nothing to do when not started.
  void stop();

private:
  QThread *m_thread = nullptr;
  HidReader *m_reader = nullptr;
};
