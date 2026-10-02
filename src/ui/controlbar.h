// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QWidget>

#include "device/dmmdecoder.h"

class QToolButton;
struct Reading;

/// The meter's keys under the display, for meters QtDMM can remote-control
/// (the UNI-T iDMM protocol: UT60BT, UT61B+/D+/E+, UT161). Groups from left
/// to right as on the meter's front - function, range, display, device -
/// that wrap to a second row when the window is narrow.
///
/// HOLD, AUTO, REL, MIN/MAX and PEAK show the meter's state as the readings
/// report it, not the last click; RANGE, SELECT, Hz/% and LIGHT are plain
/// keys. The keys are enabled while the meter is connected.
///
/// For now the bar only signals keyPressed(): sending the key codes to the
/// meter comes with the remote control itself (a later branch).
class ControlBar : public QWidget
{
  Q_OBJECT
public:
  explicit ControlBar(QWidget *parent = nullptr);

  /// A meter with keys QtDMM can press.
  static bool supported(const DmmDecoder::DMMInfo &info);

  void setConnected(bool connected);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override;

public Q_SLOTS:
  /// The key states the reading reports (HOLD, AUTO).
  void showReading(const Reading &reading);

Q_SIGNALS:
  /// "select1", "select2", "range", "auto", "hold", "rel", "minmax",
  /// "peak", "lamp" - the names of ut61eplus.py's command table.
  void keyPressed(const QString &key);

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  QList<QWidget *> m_groups;
  QList<QToolButton *> m_keys;
  QToolButton *m_hold = nullptr;
  QToolButton *m_auto = nullptr;
  /// Places the groups in rows for @p width; returns the height used.
  int layoutGroups(int width, bool apply) const;
};

/// The small fold button in the top right corner of the display that hides
/// and shows the ControlBar: ▾ open, ▸ folded; muted until hovered.
class FoldButton : public QWidget
{
  Q_OBJECT
public:
  explicit FoldButton(QWidget *parent);
  void setFolded(bool folded);
  bool isFolded() const { return m_folded; }

Q_SIGNALS:
  void toggled(bool folded);

protected:
  void paintEvent(QPaintEvent *) override;
  void enterEvent(QEnterEvent *) override;
  void leaveEvent(QEvent *) override;
  void mousePressEvent(QMouseEvent *) override;

private:
  bool m_folded = false;
  bool m_hover = false;
};
