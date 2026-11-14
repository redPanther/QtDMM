// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/views/plottheme.h"
#include <QWidget>

/// QWidget-level shared plot state. Concrete plot widgets own sample handling.
class GraphCommon : public QWidget
{
  Q_OBJECT
public:
  explicit GraphCommon(QWidget *parent = nullptr);
  virtual void setTheme(const PlotTheme &theme);
  PlotTheme theme() const { return m_theme; }
  virtual void setHold(bool hold);
  bool hold() const { return m_hold; }
  virtual void clear() = 0;
  virtual void setTitle(const QString &title);
  QString title() const { return m_title; }
protected:
  void setHoldState(bool hold) { m_hold = hold; }
  PlotTheme m_theme;
  QString m_title;
private:
  bool m_hold = false;
};
