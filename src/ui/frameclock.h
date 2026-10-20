// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>

/// The one drawing clock of the process: the views that move with the time
/// (the graph's Live window) draw on its tick, not only when a reading
/// comes. The data path - store, alarms, SCPI, recording - stays with each
/// reading.
///
/// It runs only while a view wants it (want()) and a window is shown, at
/// 30 Hz by default (Graph/frame-rate, 0 = off). When the ticks cost too
/// much of the time between them, it halves its rate (down to a quarter)
/// and comes back up when there is room again.
class FrameClock : public QObject
{
  Q_OBJECT
public:
  static FrameClock *instance();

  /// Whether @p who wants ticks; forgotten when it is destroyed.
  void   want(QObject *who, bool on);
  bool   wanted(const QObject *who) const { return m_wanted.contains(who); }
  bool   isRunning() const { return m_timer.isActive(); }

  /// Ticks a second at full speed; 0 switches the clock off.
  void   setRate(int hz);
  int    rate() const { return m_rate; }
  /// The interval now, ms: 1000 / rate(), longer while it is slowed down.
  int    interval() const { return m_interval; }

  /// One tick at once, as the timer would (for the tests).
  void   tickNow();

Q_SIGNALS:
  void   tick();

private:
  explicit FrameClock(QObject *parent = nullptr);
  void   update();
  /// After a tick that took @p ms: slower when the ticks take too much,
  /// faster again when they leave room.
  void   adapt(qint64 ms);

  QTimer m_timer;
  QHash<const QObject *, QMetaObject::Connection> m_wanted;   ///< and their destroyed()
  int    m_rate = 30;
  int    m_interval = 33;
  qint64 m_cost = 0;    ///< ms the last ticks took together
  int    m_counted = 0; ///< how many
};
