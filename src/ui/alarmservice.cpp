// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/alarmservice.h"

#include <QApplication>
#include <QDateTime>
#include <QMessageBox>
#include <QWidget>

#include "service/devicesession.h"
#include "service/metercontroller.h"
#include "ui/alarmsound.h"

AlarmService::AlarmService(DeviceSession *session, QWidget *window, QObject *parent)
  : QObject(parent), m_session(session), m_window(window)
{
  for (MeterController *ctl : session->controllers())
    attach(ctl);
  connect(session, &DeviceSession::opened, this, &AlarmService::attach);
  // the names in front of the lines come and go with the second meter
  connect(session, &DeviceSession::closed, this, &AlarmService::updateBanner);
}

void AlarmService::setPlayer(const std::function<void(const QString &)> &player)
{
  m_player = player;
  for (const Sounds &s : std::as_const(m_sounds))
  {
    s.alarm->setPlayer(player);
    s.limit->setPlayer(player);
  }
}

bool AlarmService::sounding(const MeterController *ctl, int index) const
{
  const auto it = m_sounds.constFind(ctl);
  return it != m_sounds.cend() && it->alarm->sounding(index);
}

void AlarmService::attach(MeterController *ctl)
{
  if (m_sounds.contains(ctl))
    return;
  Sounds &s = m_sounds[ctl];
  // a sound goes on while its alarm is raised and not acknowledged, the
  // limit test's while the verdict stays
  s.alarm = new AlarmSound([ctl](int index) { return ctl->alarms()->state(index) == AlarmManager::Raised; }, this);
  s.limit = new AlarmSound([ctl](int verdict) { return ctl->verdict() == verdict; }, this);
  if (m_player)
  {
    s.alarm->setPlayer(m_player);
    s.limit->setPlayer(m_player);
  }
  connect(ctl, &MeterController::alarmRaised, this, [this, ctl](int index, const Alarm &alarm, const QString &shown,
                                                                const QString &text)
  {
    alarmRaised(ctl, index, alarm, shown, text);
  });
  connect(ctl, &MeterController::alarmCleared, this, [this, ctl](int, const Alarm &alarm)
  {
    if (alarm.clearSound)
      m_sounds[ctl].alarm->play(AlarmSound::clearedSound());
  });
  // edited alarms: their places in the list may have moved, the sounds stop
  connect(ctl, &MeterController::alarmsChanged, this, [this, ctl] { m_sounds[ctl].alarm->stopAll(); });
  connect(ctl, &MeterController::limitJudged, this, [this, ctl](int, LimitTest::Verdict verdict, double)
  {
    limitJudged(ctl, verdict);
  });
  connect(ctl, &MeterController::limitTestChanged, this, [this, ctl]
  {
    Sounds &t = m_sounds[ctl];
    t.limit->stopAll();
    t.last = LimitTest::NoVerdict;
  });
  connect(ctl, &MeterController::alarmBannerChanged, this, [this, ctl](const QString &lines, const QColor &color)
  {
    Sounds &t = m_sounds[ctl];
    t.banner = lines;
    t.color = color;
    updateBanner();
  });
  // closed: its sounds stop with it
  connect(ctl, &QObject::destroyed, this, [this, ctl]
  {
    const Sounds gone = m_sounds.take(ctl);
    delete gone.alarm;
    delete gone.limit;
    updateBanner();
  });
  updateBanner();
}

// The controller has reported the alarm and run its program; what is left
// needs the desktop: the sound, raise the window, pop up a box.
void AlarmService::alarmRaised(MeterController *ctl, int index, const Alarm &alarm, const QString &shown,
                               const QString &text)
{
  m_sounds[ctl].alarm->start(index, alarm);
  if (alarm.raiseWindow && m_window)
  {
    m_window->raise();
    m_window->activateWindow();
    QApplication::alert(m_window);
  }
  if (alarm.popup)
  {
    const QString title = m_session->controllers().size() > 1 && !ctl->deviceName().isEmpty()
                            ? tr("QtDMM alarm: %1 (%2)").arg(alarm.name, ctl->deviceName())
                            : tr("QtDMM alarm: %1").arg(alarm.name);
    auto *box = new QMessageBox(QMessageBox::Warning, title,
                                QString("%1\n%2   %3").arg(text, shown, QDateTime::currentDateTime().toString("HH:mm:ss")),
                                QMessageBox::Ok, m_window);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(false);
    box->show();
  }
}

// A new verdict: the old one's sound stops, the new one's starts (repeated
// while the verdict stays, if wanted).
void AlarmService::limitJudged(MeterController *ctl, LimitTest::Verdict verdict)
{
  Sounds &s = m_sounds[ctl];
  if (verdict == s.last)
    return;
  s.last = verdict;
  s.limit->stopAll();
  const LimitTest &test = ctl->limitTest();
  Alarm sound;
  sound.sound = verdict == LimitTest::Pass ? test.passSound : verdict == LimitTest::High ? test.highSound
                : verdict == LimitTest::Low ? test.lowSound : QString();
  const bool repeat = verdict == LimitTest::High ? test.highRepeat : verdict == LimitTest::Low ? test.lowRepeat
                      : verdict == LimitTest::Pass && test.passRepeat;
  sound.repeat = repeat ? Alarm::WhileRaised : Alarm::Once;
  s.limit->start(verdict, sound);
}

QString AlarmService::bannerText() const
{
  const QList<MeterController *> all = m_session->controllers();
  QStringList lines;
  for (const MeterController *ctl : all)
  {
    const auto it = m_sounds.constFind(ctl);
    if (it == m_sounds.cend() || it->banner.isEmpty())
      continue;
    for (const QString &line : it->banner.split('\n'))
      lines << (all.size() > 1 && !ctl->deviceName().isEmpty() ? tr("%1: %2").arg(ctl->deviceName(), line) : line);
  }
  return lines.join('\n');
}

void AlarmService::updateBanner()
{
  QColor color;
  for (const MeterController *ctl : m_session->controllers())
    if (const auto it = m_sounds.constFind(ctl); it != m_sounds.cend() && !it->banner.isEmpty())
    {
      color = it->color;
      break;
    }
  Q_EMIT bannerChanged(bannerText(), color);
}

void AlarmService::acknowledgeAll()
{
  for (MeterController *ctl : m_session->controllers())
    ctl->acknowledgeAlarms();
}
