// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/alarmsound.h"

#include <QApplication>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#ifdef QTDMM_WITH_MULTIMEDIA
#include <QSoundEffect>
#endif

AlarmSound::AlarmSound(Still still, QObject *parent) :
  QObject(parent),
  m_still(std::move(still))
{
}

QList<QPair<QString, QString>> AlarmSound::builtIn()
{
  return { { QStringLiteral("alarm_warn"), tr("Warning") },
           { QStringLiteral("alarm_high"), tr("High") },
           { QStringLiteral("alarm_low"), tr("Low") },
           { QStringLiteral("alarm_crit"), tr("Critical") },
           { QStringLiteral("alarm_ok"), tr("OK") } };
}

void AlarmSound::start(int index, const Alarm &alarm)
{
  stop(index);
  if (alarm.sound.isEmpty())
    return;
  play(alarm.sound);
  const int left = alarm.repeat == Alarm::WhileRaised ? -1 : alarm.repeat == Alarm::Times ? alarm.repeatCount - 1 : 0;
  if (left == 0)
    return;
  auto *timer = new QTimer(this);
  timer->setInterval(1000);
  connect(timer, &QTimer::timeout, this, [this, index] { next(index); });
  timer->start();
  m_playing.insert(index, { alarm.sound, left, timer });
}

void AlarmSound::stopAll()
{
  for (const Playing &p : std::as_const(m_playing))
    p.timer->deleteLater();
  m_playing.clear();
}

void AlarmSound::stop(int index)
{
  if (m_playing.contains(index))
    m_playing.take(index).timer->deleteLater();
}

// one more, if the alarm still sounds and plays are left
void AlarmSound::next(int index)
{
  auto it = m_playing.find(index);
  if (it == m_playing.end())
    return;
  if (!m_still(index))
  {
    stop(index);
    return;
  }
  play(it->sound);
  if (it->left < 0)
    return;   // while raised
  --it->left;
  if (it->left == 0)
    stop(index);
}

void AlarmSound::play(const QString &sound)
{
  if (m_player)
  {
    m_player(sound);
    return;
  }
#ifdef QTDMM_WITH_MULTIMEDIA
  if (sound != QLatin1String("beep"))
  {
    QSoundEffect *&effect = m_effects[sound];
    if (!effect)
    {
      effect = new QSoundEffect(this);
      effect->setSource(QFileInfo(sound).isAbsolute() ? QUrl::fromLocalFile(sound)
                                                      : QUrl(QStringLiteral("qrc:/sounds/%1.wav").arg(sound)));
    }
    // a file that is gone or no WAV: at least the beep
    if (effect->status() != QSoundEffect::Error)
    {
      effect->play();
      return;
    }
  }
#endif
  QApplication::beep();
}
