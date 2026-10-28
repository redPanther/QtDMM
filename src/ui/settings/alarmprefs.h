// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSet>
#include <functional>

#include "ui/settings/settingspage.h"
#include "core/alarm.h"

class QListWidget;
class QPushButton;

/// Settings page "Alarms": the alarms of each open meter, chosen at the
/// top (with more than one), with add / edit / remove (AlarmDlg edits one).
/// The alarms are the meter's (MeterController::setAlarms()), kept with its
/// entry in "My devices" or, for a window meter without one, in the window's
/// settings (Alarms/list); the page itself keeps only Alarms/blink.
class AlarmPrefs : public SettingsPage
{
  Q_OBJECT
public:
  /// An open meter on the page.
  struct Device
  {
    QString source;              ///< MeterController::source(); empty: the window's meter
    QString name;
    QList<Alarm> alarms;
    QMap<int, QString> units;    ///< base unit by value id (the window's: setValueUnit())
    QStringList valueNames;      ///< as setValueNames() (the window's: setValueNames())
  };

  explicit AlarmPrefs(QWidget *parent = nullptr);
  /// The open meters and their alarms as they are, read on every reload of
  /// the page; the window's meter first. Without, the window's alarms come
  /// from Alarms/list.
  void        setDevices(const std::function<QList<Device>()> &devices) { m_source = devices; }
  /// Where Apply and OK put a list edited: to the meter @p source
  /// (Device::source). Without, the window's go to Alarms/list.
  void        setApply(const std::function<void(const QString &source, const QList<Alarm> &alarms)> &apply)
  {
    m_apply = apply;
  }
  /// The window's meter's alarms on the page.
  QList<Alarm> alarms() const { return m_devices.value(0).alarms; }
  /// The meter @p source as it is now (its units and value names for
  /// AlarmDlg); empty when it is not open.
  Device      device(const QString &source) const;
  /// The name of a new alarm, the @p number th: "Alarm 3".
  static QString alarmName(int number) { return tr("Alarm %1").arg(number); }
  /// Shows the alarms of the meter @p source (empty: the window's).
  void        setCurrentDevice(const QString &source);
  /// The display and the analog meter blink while they show an alarm's colour (Alarms/blink).
  bool blink() const;
  /// Base unit of the current reading, shown next to the thresholds; empty
  /// before the first reading (the labels go without a unit).
  void setUnit(const QString &unit) { setValueUnit(0, unit); }
  /// The same for value @p id of the window's meter (Reading::id).
  void setValueUnit(int id, const QString &unit);
  /// The names of the window's meter's values ("1: CO2 (ppm)" ...), one
  /// each; with more than one an alarm chooses the value it watches.
  void setValueNames(const QStringList &names);

public Q_SLOTS:
  void defaultsSLOT() override;
  void factoryDefaultsSLOT() override;
  void applySLOT() override;

private:
  Device &current() { return m_devices[m_current]; }
  void refresh();
  void add();
  void edit();
  void remove();
  void toggle(int row, bool on);

  std::function<QList<Device>()> m_source;
  std::function<void(const QString &, const QList<Alarm> &)> m_apply;
  QList<Device> m_devices;          ///< never empty, the window's first
  int m_current = 0;
  QSet<QString> m_changed;          ///< sources edited
  QMap<int, QString> m_units;       ///< the window's meter's, by value id
  QStringList m_valueNames;         ///< the window's meter's
  class QComboBox *m_device;        ///< which meter; hidden with one
  QLabel *m_deviceLabel;
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
