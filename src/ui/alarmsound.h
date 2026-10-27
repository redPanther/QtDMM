// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QMap>
#include <QObject>
#include <QString>
#include <functional>

#include "core/alarm.h"

class QSoundEffect;
class QTimer;

/// The alarms' sounds (Alarm::sound): once, N times, or as long as the alarm
/// is raised, a second from start to start. Every second it asks whether the
/// alarm still sounds (raised and not acknowledged) and stops if not. Each
/// alarm plays its own. Without Qt Multimedia every sound is the system's
/// beep, repeated the same way.
class AlarmSound : public QObject
{
  Q_OBJECT
public:
  /// Whether the alarm at @p index still sounds.
  using Still = std::function<bool(int index)>;

  explicit AlarmSound(Still still, QObject *parent = nullptr);

  /// The alarm at @p index has raised: its sound starts.
  void start(int index, const Alarm &alarm);
  /// All sounds stop (the alarms were edited, the indexes may have moved).
  void stopAll();
  /// Whether a sound of the alarm at @p index is still to come.
  bool sounding(int index) const { return m_playing.contains(index); }
  /// @p sound once (Alarm::sound), e.g. to try it in the editor.
  void play(const QString &sound);
  /// Instead of playing: the tests count what would have sounded.
  void setPlayer(const std::function<void(const QString &)> &player) { m_player = player; }

  /// The built-in sounds: name for Alarm::sound, and what the editor shows.
  static QList<QPair<QString, QString>> builtIn();
  /// The sound when an alarm clears (Alarm::clearSound).
  static QString clearedSound() { return QStringLiteral("alarm_ok"); }

private:
  struct Playing
  {
    QString sound;
    int left;        ///< plays still to come, -1 while raised
    QTimer *timer;   ///< a second from start to start
  };
  void next(int index);
  void stop(int index);

  Still m_still;
  QMap<int, Playing> m_playing;   ///< by alarm index
  std::function<void(const QString &)> m_player;
#ifdef QTDMM_WITH_MULTIMEDIA
  QHash<QString, QSoundEffect *> m_effects;   ///< one per sound, loaded at its first use
#endif
};
