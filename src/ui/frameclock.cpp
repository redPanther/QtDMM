// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/frameclock.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QWidget>

namespace
{
constexpr int kSlowest = 4;      ///< down to a quarter of the rate
constexpr int kMeasured = 15;    ///< ticks between two looks at the cost
}

FrameClock *FrameClock::instance()
{
  static FrameClock *clock = new FrameClock(qApp);
  return clock;
}

FrameClock::FrameClock(QObject *parent) : QObject(parent)
{
  m_timer.setTimerType(Qt::PreciseTimer);
  m_timer.setInterval(m_interval);
  connect(&m_timer, &QTimer::timeout, this, [this]
  {
    // all windows minimized: nothing to see, nothing to draw
    bool shown = false;
    for (const QWidget *w : QApplication::topLevelWidgets())
      if (w->isVisible() && !w->isMinimized())
      {
        shown = true;
        break;
      }
    if (shown)
      tickNow();
  });
}

void FrameClock::want(QObject *who, bool on)
{
  if (on == m_wanted.contains(who))
    return;
  if (on)
    m_wanted.insert(who, connect(who, &QObject::destroyed, this, [this, who] { want(who, false); }));
  else
    disconnect(m_wanted.take(who));
  update();
}

void FrameClock::setRate(int hz)
{
  m_rate = qBound(0, hz, 120);
  m_interval = m_rate > 0 ? 1000 / m_rate : 0;
  m_timer.setInterval(m_interval);
  m_cost = m_counted = 0;
  update();
}

void FrameClock::update()
{
  const bool run = m_rate > 0 && !m_wanted.isEmpty();
  if (run && !m_timer.isActive())
    m_timer.start();
  else if (!run)
    m_timer.stop();
}

void FrameClock::tickNow()
{
  QElapsedTimer took;
  took.start();
  Q_EMIT tick();
  adapt(took.elapsed());
}

void FrameClock::adapt(qint64 ms)
{
  if (m_rate <= 0)
    return;
  m_cost += ms;
  if (++m_counted < kMeasured)
    return;
  const qint64 spent = m_cost, room = qint64(m_counted) * m_interval;
  m_cost = m_counted = 0;
  const int fastest = 1000 / m_rate;
  // more than 40 % of the time drawing: half the rate; under 10 % at the
  // slower one: back up
  if (spent * 10 > room * 4 && m_interval < fastest * kSlowest)
    m_interval *= 2;
  else if (spent * 10 < room && m_interval > fastest)
    m_interval = qMax(fastest, m_interval / 2);
  else
    return;
  m_timer.setInterval(m_interval);
}
