// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/settings/settingspage.h"
#include "core/alarm.h"

class QListWidget;
class QPushButton;

/// Settings page "Alarms": the list of alarms with add / edit / remove
/// (AlarmDlg edits one). Stored as JSON under Alarms/list.
class AlarmPrefs : public SettingsPage
{
  Q_OBJECT
public:
  explicit AlarmPrefs(QWidget *parent = nullptr);

  QList<Alarm> alarms() const { return m_alarms; }
  /// The display and the analog meter blink while they show an alarm's colour (Alarms/blink).
  bool blink() const;
  /// Base unit of the current reading, shown next to the thresholds; empty
  /// before the first reading (the labels go without a unit).
  void setUnit(const QString &unit) { setValueUnit(0, unit); }
  /// The same for value @p id of the meter (Reading::id).
  void setValueUnit(int id, const QString &unit);
  /// The names of the meter's values ("1: CO2 (ppm)" ...), one each; with
  /// more than one an alarm chooses the value it watches.
  void setValueNames(const QStringList &names);

public Q_SLOTS:
  void defaultsSLOT() override;
  void factoryDefaultsSLOT() override;
  void applySLOT() override;

private:
  void refresh();
  void add();
  void edit();
  void remove();
  void toggle(int row, bool on);

  QList<Alarm> m_alarms;
  QMap<int, QString> m_units;   ///< base unit by value id
  QStringList m_valueNames;
  QListWidget *m_list;
  QLabel *m_empty;   ///< the hint in the empty list
  QPushButton *m_edit, *m_remove;
  class QCheckBox *m_blink;
};

/// Editor for one alarm: name, condition, duration, hysteresis, actions.
class AlarmDlg : public QDialog
{
  Q_OBJECT
public:
  /// @p units by value id; @p valueNames as AlarmPrefs::setValueNames().
  AlarmDlg(const Alarm &alarm, const QMap<int, QString> &units, const QStringList &valueNames = {},
           QWidget *parent = nullptr);
  Alarm alarm() const;

private:
  void updateFields();
  /// The sound chosen, its repetitions; "Choose file..." asks for a WAV file.
  void updateSound();

  class AlarmSound *m_preview;   ///< plays the sound chosen
  int m_soundIndex = 0;          ///< the sound before "Choose file...", back on Cancel

  QMap<int, QString> m_units;
  QString m_unit;   ///< the unit of the value chosen
  int m_valueId = 0;
  QString m_valueSource;   ///< the meter of the value (ValueRef::source), kept as it was
  class QComboBox *m_value = nullptr;   ///< which value; none with one
  class QLineEdit *m_name, *m_a, *m_b, *m_seconds, *m_hysteresis, *m_message, *m_command;
  class QComboBox *m_condition, *m_recorder, *m_sound, *m_repeat;
  class QSpinBox *m_repeatCount;
  class QCheckBox *m_enabled, *m_banner, *m_tint, *m_clearSound, *m_popup, *m_raise, *m_markGraph, *m_markTable, *m_disconnect;
  class QPushButton *m_color;
  class QLabel *m_aLabel, *m_bLabel, *m_secondsLabel, *m_hystLabel;
  QColor m_colorValue;
};
