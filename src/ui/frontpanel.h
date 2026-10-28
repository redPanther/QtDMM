// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include <QPointer>
#include <QWidget>

#include "core/valueref.h"

class ControlBar;
class DeviceSession;
class FoldButton;
class LcdWidget;
class MeterController;
class ValueFeed;

/// One meter's front: its LCD, the keys below it (ControlBar, for meters
/// QtDMM can press keys of) and the fold button in the LCD's corner.
///
/// The panel shows the meter of value() (ValueFeed): its readings, min/max
/// and staleness on the LCD, its key states on the keys; a click on a key
/// goes to that meter. Each meter open in the process has one (MainWindow),
/// the window's meter the one InstanceWidget feeds further (the limit test,
/// the alarm colour, the value choice).
class FrontPanel : public QWidget
{
  Q_OBJECT
public:
  explicit FrontPanel(DeviceSession *session, QWidget *parent = nullptr);

  LcdWidget  *display() const { return m_display; }
  /// The value on the LCD's big line, of the meter that has it; another
  /// meter (or none) clears the LCD.
  void        setValue(const ValueRef &value);
  ValueRef    value() const;
  /// The LCD's small line (LcdWidget::setSecondaries()); std::nullopt: the
  /// first values after the big one.
  void        setSecondaries(const std::optional<QList<int>> &ids);
  QList<int>  secondaries() const;
  /// Whether the small line was chosen (else it is not saved).
  bool        secondariesChosen() const { return m_secondariesChosen; }
  /// The meter shown, nullptr while it is not open.
  MeterController *controller() const;
  /// The keys folded away (the fold button, "Hide controls").
  void        setFolded(bool folded);
  bool        isFolded() const;
  /// The meter has keys QtDMM can press (ControlBar::supported()).
  bool        keysSupported() const { return m_keysSupported; }
  /// The keys are shown: the meter has some and they are not folded.
  bool        keysShown() const;
  /// Pixels the keys take below the LCD, 0 while they are not shown.
  int         keysHeight() const;
  /// Reads the meter's keys anew (after its settings were applied).
  void        updateKeys();

Q_SIGNALS:
  /// Folded or unfolded by the user (the fold button).
  void        foldToggled(bool folded);
  /// The keys appeared or went: the panel's height for a width changed.
  void        keysChanged();
  /// A key did not reach the meter (not connected yet).
  void        keyFailed();

protected:
  /// Keeps the fold button in the LCD's top right corner.
  bool        eventFilter(QObject *watched, QEvent *event) override;

private:
  /// The meter now shown: its keys, the LCD afresh, the connection.
  void        attach(MeterController *ctl);
  /// The keys enabled while the meter's connection is wanted.
  void        updateConnected();

  ValueFeed  *m_feed;
  LcdWidget  *m_display;
  ControlBar *m_controls;
  FoldButton *m_fold;
  bool        m_keysSupported = false;
  bool        m_secondariesChosen = false;
  QPointer<MeterController> m_ctl;
  QMetaObject::Connection m_link;   ///< the meter's connection state, for the keys
};
