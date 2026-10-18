// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "device/transports/hidreader.h"

Q_LOGGING_CATEGORY(lcHid, "qtdmm.hid", QtWarningMsg)

HidReader::HidReader(hid_device *handle)
  : m_handle(handle)
{
}

void HidReader::run()
{
  unsigned char buf[64];
  while (!m_stop.load())
  {
    // the timeout keeps stop() effective even when the cable sends nothing
    // (CP2110/CH9329 send no idle reports)
    const int res = hid_read_timeout(m_handle, buf, sizeof(buf), 100);
    if (res < 0)
    {
      Q_EMIT readError(QString::fromWCharArray(hid_error(m_handle)));
      break;
    }
    if (res > 0)
      Q_EMIT report(QByteArray(reinterpret_cast<const char *>(buf), res));
  }
  hid_close(m_handle);
  m_handle = nullptr;
  Q_EMIT finished();
}

void HidReaderThread::stop()
{
  if (!m_thread)
    return;
  m_reader->stop();
  m_thread->quit();   // in case the loop already ended
  if (m_thread->isRunning() && !m_thread->wait(2000))
  {
    // hid_read_timeout() did not return - should not happen; better a
    // leaked thread than a crash in it
    qWarning() << "HID: reader thread did not stop";
    m_thread->setParent(nullptr);
  }
  else
  {
    // the thread is done, so its object can be deleted from here (a
    // deleteLater() would never run: no event loop is left in that thread)
    delete m_reader;
    delete m_thread;
  }
  m_thread = nullptr;
  m_reader = nullptr;
}
