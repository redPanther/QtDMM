// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later

#include "service/valuefeed.h"

#include "service/devicesession.h"
#include "service/metercontroller.h"

ValueFeed::ValueFeed(DeviceSession *session, QObject *parent)
  : QObject(parent)
  , m_session(session)
{
  connect(session, &DeviceSession::opened, this, &ValueFeed::attach);
  connect(session, &DeviceSession::closed, this, &ValueFeed::attach);
}

void ValueFeed::setValue(const ValueRef &value)
{
  m_value = value;
  attach();
}

void ValueFeed::attach()
{
  MeterController *ctl = m_session->controller(m_value);
  // the same meter: nothing to do (a closed one is gone from m_ctl already)
  if (ctl && ctl == m_ctl)
    return;
  for (const QMetaObject::Connection &c : std::as_const(m_links))
    disconnect(c);
  m_links.clear();
  m_ctl = ctl;
  if (ctl)
  {
    m_links << connect(ctl, &MeterController::reading, this, &ValueFeed::reading)
            << connect(ctl, &MeterController::staleChanged, this, &ValueFeed::staleChanged)
            << connect(ctl, &MeterController::minimumChanged, this, &ValueFeed::minimumChanged)
            << connect(ctl, &MeterController::maximumChanged, this, &ValueFeed::maximumChanged)
            << connect(ctl, &MeterController::minMaxReset, this, &ValueFeed::minMaxReset)
            << connect(ctl, &MeterController::valueMinMaxChanged, this, &ValueFeed::valueMinMaxChanged);
  }
  Q_EMIT controllerChanged(ctl);
  if (!ctl)
  {
    // the meter is not open: no value, not the last one it had
    Reading none;
    none.id = m_value.id;
    none.source = m_value.source;
    none.quality = Quality::NoValue;
    Q_EMIT staleChanged(false);
    Q_EMIT minMaxReset();
    Q_EMIT reading(none);
  }
}
