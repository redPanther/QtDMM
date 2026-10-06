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
/// to right as on the meter's front - function, range, display, LIGHT and
/// HOLD on the right - that wrap to a second row when the window is narrow.
///
/// HOLD, AUTO, REL, MIN/MAX and PEAK show the meter's state as the readings
/// report it, not the last click; RANGE, SELECT, Hz/% and LIGHT are plain
/// keys. MIN/MAX and PEAK are left as on the meter: by holding the key
/// (keyPressed("minmax_off")). The keys are enabled while the meter is
/// connected; setProtocol() hides those the meter does not have (PEAK on
/// the UT60BT). MainWindow sends them (MeterController::pressKey()).
class ControlBar : public QWidget
{
  Q_OBJECT
public:
  explicit ControlBar(QWidget *parent = nullptr);

  /// A meter with keys QtDMM can press.
  static bool supported(const DmmDecoder::DMMInfo &info);

  void setConnected(bool connected);
  /// Shows the keys the decoder of @p format can press
  /// (DmmDecoder::keyRequest()).
  void setProtocol(FrameFormat::DataFormat format);
  /// Holding MIN/MAX or PEAK this long leaves it (ms).
  static constexpr int kLongPressMs = 700;

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override;

public Q_SLOTS:
  /// The key states the reading reports (HOLD, AUTO).
  void showReading(const Reading &reading);

Q_SIGNALS:
  /// "select1", "select2", "range", "auto", "hold", "rel", "minmax",
  /// "minmax_off", "peak", "peak_off", "lamp" (DmmDecoder::keyRequest()).
  void keyPressed(const QString &key);

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  QList<QWidget *> m_groups;
  QList<QToolButton *> m_keys;
  QToolButton *m_hold = nullptr;
  QToolButton *m_auto = nullptr;
  QToolButton *m_rel = nullptr;
  QToolButton *m_minmax = nullptr;
  QToolButton *m_peak = nullptr;
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
