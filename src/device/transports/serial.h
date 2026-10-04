#pragma once

#include <QtCore>
#include <QSerialPort>

#include "device/dmmdecoder.h"


/// A meter on a real (or USB-to-serial) serial port.
///
/// Thin QSerialPort wrapper that knows the meter's line settings from its
/// DMMInfo and applies them in init(), including the DTR/RTS levels many
/// optically isolated cables draw their power from.
class SerialDevice : public QSerialPort {
    Q_OBJECT
public:
    explicit SerialDevice(const DmmDecoder::DMMInfo info, QString device, QObject *p = Q_NULLPTR) : QSerialPort(p), m_dmmInfo(info) {
        setPortName(device);
    };

    /// Appends the system's serial port names to @p portlist.
    static bool availablePorts(QStringList &portlist);
    /// The name of @p port that stays when the cable is plugged in again:
    /// on Linux the link in @p byIdDir (/dev/serial/by-id) that points to
    /// it - ttyUSB0 may be ttyUSB1 tomorrow. Else, or when there is none,
    /// @p port as it is.
    static QString stablePortName(const QString &port, const QString &byIdDir = QStringLiteral("/dev/serial/by-id"));
    /// Sets baud rate, data/stop bits, parity and the DTR/RTS lines from
    /// the DMMInfo. Must be called on an open port.
    bool init();

protected:
    DmmDecoder::DMMInfo m_dmmInfo;
};
