// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/views/graphcommon.h"
#include "ui/views/plotaxis.h"
class QCPItemText;
class QCPTextElement;
class QCustomPlot;
class QCPAxis;

/// Common QCustomPlot host with title, empty/HOLD overlays, and OpenGL control.
class QcpGraphCommon : public GraphCommon
{
  Q_OBJECT
public:
  explicit QcpGraphCommon(QWidget *parent = nullptr, const QString &noData = QString());
  QCustomPlot *customPlot() const { return m_plot; }
  static void setOpenGlEnabledDefault(bool enabled);
  static bool openGlEnabledDefault();
  void setOpenGlEnabled(bool enabled);
  bool openGlEnabled() const;
  void setNoDataText(const QString &text);
  void setTitle(const QString &title) override;
  void setTheme(const PlotTheme &theme) override;
  void setHold(bool hold) override;
  void clear() override;
  void setAxisRange(QCPAxis *axis, const PlotAxis::Range &range);
protected:
  QCustomPlot *m_plot = nullptr;
  QCPTextElement *m_titleElement = nullptr;
  QCPItemText *m_noData = nullptr;
  QCPItemText *m_holdItem = nullptr;
  void setNoDataVisible(bool visible);
private:
  static bool s_openGlDefault;
};
