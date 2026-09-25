// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QLabel;
class QTimer;

/// A view inside an MDI window with a header line on top: a bold name, a
/// detail such as the port, and a dot that blinks green with the readings
/// and turns grey when they stop. The header gives a window without title
/// bar its name; its context menu is the window's menu.
///
/// In the Silver design the frame paints a brushed-metal gradient behind
/// the view.
class ViewFrame : public QWidget
{
  Q_OBJECT
public:
  /// @param view    the view, reparented into the frame
  /// @param showDot whether the header has the activity dot (instruments)
  /// @param parent  parent widget
  ViewFrame(QWidget *view, bool showDot, QWidget *parent = nullptr);

  QWidget *view() const { return m_view; }
  void setTitle(const QString &title);
  void setDetail(const QString &detail);
  void setHeaderVisible(bool on);
  bool headerVisible() const;

public Q_SLOTS:
  /// A reading arrived: the dot blinks.
  void pulse();

Q_SIGNALS:
  /// Right click on the header line, at @p globalPos.
  void menuRequested(const QPoint &globalPos);

protected:
  void paintEvent(QPaintEvent *) override;

private:
  void updateDot();

  QWidget *m_view;
  QWidget *m_header;
  QLabel  *m_title;
  QLabel  *m_detail;
  QLabel  *m_dot = nullptr;
  QTimer  *m_idle = nullptr;
  bool     m_blink = false;
  bool     m_active = false;
};
