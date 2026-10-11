// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the network transports (VXI-11, HiSLIP) need from an SCPI interpreter.
// The transports know nothing about measurements; they only move bytes and call these functions.

#include <QByteArray>
#include <QList>

#include <functional>

struct ScpiEndpoint
{
  // Executes ONE program message line (no line terminator). Returns the response including its
  // terminating '\n', or an empty array if the line produced none (ScpiServer::process() does this).
  std::function<QByteArray(const QByteArray &)> process;

  // Status byte for VXI-11 device_readstb and HiSLIP AsyncStatusQuery. Optional.
  std::function<quint8()> statusByte;

  // IEEE 488.2 device clear. Optional.
  std::function<void()> deviceClear;

  // A single write may carry several lines; every query answers with a message of its own.
  QList<QByteArray> run(const QByteArray &message) const
  {
    QList<QByteArray> responses;
    if (!process)
      return responses;
    for (const QByteArray &raw : message.split('\n'))
    {
      QByteArray line = raw;
      while (line.endsWith('\r'))
        line.chop(1);
      if (line.trimmed().isEmpty())
        continue;
      const QByteArray response = process(line);
      if (!response.isEmpty())
        responses << response;
    }
    return responses;
  }

  quint8 stb() const { return statusByte ? statusByte() : quint8(0); }
};
