//======================================================================
// File:		readerthread.cpp
// Author:	Matthias Toussaint
// Created:	Sat Apr 14 12:44:00 CEST 2001
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

#include <QtGui>
#include <QtWidgets>
#include <QtSerialPort>

#include <unistd.h>
#include <stdio.h>
#include <iostream>

#include "readerthread.h"



ReaderThread::ReaderThread(QObject *receiver) :
  QObject(receiver),
  m_receiver(receiver),
  m_readValue(false),
  m_format(ReadEvent::Invalid),
  m_length(0),
  m_sendRequest(true),
  m_id(0),
  m_numValues(1),
  m_decoder(Q_NULLPTR)
{
  m_port = Q_NULLPTR;
}


void ReaderThread::setHandle(QIODevice *handle)
{
  m_port = handle;

  m_readValue = false;

  if (!m_port)
  {
    m_status = ReaderThread::NotConnected;
    m_readValue = false;
  }
  else
  {
    connect(m_port, SIGNAL(readyRead()), this, SLOT(socketNotifierSLOT()));
    connect(m_port, SIGNAL(aboutToClose()), this, SLOT(socketClose()));
  }
}


void ReaderThread::socketClose()
{
  m_id = 0;
}

void ReaderThread::start()
{
  QTimer *timer = new QTimer(this);
  connect(timer, SIGNAL(timeout()), this, SLOT(timer()));
  timer->start(1000);
}
void ReaderThread::timer()
{
  if (m_readValue)
  {
    sendReadRequest();
    m_readValue = false;
  }
}

void ReaderThread::startRead()
{
  //std::cerr << "start read" << std::endl;
  m_readValue = true;
  m_sendRequest = true;
  m_last_check_ok_idx = -1;
}


void ReaderThread::socketNotifierSLOT()
{
  int retval = 0;
  int r;
  char byte;

  m_status = ReaderThread::Ok;
  int64_t bytesToRead = (m_decoder == Q_NULLPTR) ? 0 : m_decoder->getPacketLength();

  while ((r = m_port->read( &byte, 1)) > 0)
  {
    retval++;
    m_fifo[m_length] = byte;

    if (m_decoder != Q_NULLPTR && m_decoder->checkFormat(m_fifo,m_length))
    {
      m_length = (m_length - bytesToRead + 1 + FIFO_LENGTH) % FIFO_LENGTH;

      for (int i = 0; i < bytesToRead; ++i)
      {
        m_buffer[i] = m_fifo[m_length];
        m_length = (m_length + 1) % FIFO_LENGTH;
      }

      m_sendRequest = true;
      m_length = 0;

      Q_EMIT readEvent( QByteArray(m_buffer,bytesToRead), m_id);

      m_id = (m_id + 1) % m_numValues;

      if (m_port->bytesAvailable() < bytesToRead) {
        break;
      }
    }
    else
      m_length = (m_length + 1) % FIFO_LENGTH;
  }

  if (0 == retval) {
    if (-1 == r) {
      m_status = ReaderThread::Error;
    }
    else if (0 == r) {
      m_status = ReaderThread::Timeout;
    }
  }
}

void ReaderThread::sendReadRequest()
{
  switch (m_format)
  {
    case ReadEvent::Metex14:
      if (m_sendRequest)
      {
        /* TODO: Errorhandling */
        if (m_port->write("D\n", 2) != 2)
          m_status = Error;
         //std::cerr << "WROTE: " << ret << std::endl;
        m_sendRequest = false;
      }
      break;
    default:
      break;
  }
}
