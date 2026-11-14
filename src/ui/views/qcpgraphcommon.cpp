// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/views/qcpgraphcommon.h"
#include "3rdparty/qcustomplot/qcustomplot.h"
#include <QCoreApplication>
#include <QVBoxLayout>

bool QcpGraphCommon::s_openGlDefault = true;
QcpGraphCommon::QcpGraphCommon(QWidget *parent, const QString &noData) : GraphCommon(parent)
{
  setMinimumSize(220, 180);
  auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
  m_plot = new QCustomPlot(this); layout->addWidget(m_plot);
#ifdef QCUSTOMPLOT_USE_OPENGL
  setOpenGlEnabled(s_openGlDefault);
#endif
  m_plot->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_plot, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
    emit customContextMenuRequested(mapFromGlobal(m_plot->mapToGlobal(pos)));
  });
  m_plot->plotLayout()->insertRow(0);
  m_titleElement = new QCPTextElement(m_plot, QString(), font()); m_plot->plotLayout()->addElement(0, 0, m_titleElement);
  m_noData = new QCPItemText(m_plot); m_noData->position->setType(QCPItemPosition::ptAxisRectRatio);
  m_noData->position->setCoords(0.5, 0.5); m_noData->setPositionAlignment(Qt::AlignCenter);
  m_noData->setText(noData.isEmpty() ? QCoreApplication::translate("QcpGraphCommon", "No data yet") : noData);
  m_holdItem = new QCPItemText(m_plot); m_holdItem->position->setType(QCPItemPosition::ptAxisRectRatio);
  m_holdItem->position->setCoords(0.98, 0.02); m_holdItem->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
  m_holdItem->setText(QStringLiteral("HOLD")); m_holdItem->setVisible(false);
}
void QcpGraphCommon::setOpenGlEnabledDefault(bool enabled) { s_openGlDefault = enabled; }
bool QcpGraphCommon::openGlEnabledDefault() { return s_openGlDefault; }
void QcpGraphCommon::setOpenGlEnabled(bool enabled)
{
#ifdef QCUSTOMPLOT_USE_OPENGL
  if (m_plot) m_plot->setOpenGl(enabled, 4);
#else
  Q_UNUSED(enabled);
#endif
}
bool QcpGraphCommon::openGlEnabled() const
{
#ifdef QCUSTOMPLOT_USE_OPENGL
  return m_plot && m_plot->openGl();
#else
  return false;
#endif
}
void QcpGraphCommon::setNoDataText(const QString &text) { m_noData->setText(text); }
void QcpGraphCommon::setNoDataVisible(bool visible) { m_noData->setVisible(visible); }
void QcpGraphCommon::setTitle(const QString &title)
{ GraphCommon::setTitle(title); m_titleElement->setText(title); }
void QcpGraphCommon::setTheme(const PlotTheme &theme)
{
  GraphCommon::setTheme(theme); theme.applyTo(m_plot);
  m_titleElement->setTextColor(theme.labels); m_noData->setColor(theme.labels); m_holdItem->setColor(theme.labels);
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}
void QcpGraphCommon::setHold(bool hold)
{ GraphCommon::setHold(hold); m_holdItem->setVisible(hold); }
void QcpGraphCommon::clear() { setNoDataVisible(true); }
void QcpGraphCommon::setAxisRange(QCPAxis *axis, const PlotAxis::Range &range)
{
  if (!axis) return;
  axis->setRange(range.minimum / range.factor, range.maximum / range.factor);
  axis->setLabel(range.title);
}
