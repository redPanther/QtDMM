// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mdiarranger.h"

#include <QApplication>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMouseEvent>
#include <QTimer>
#include <QtMath>

namespace
{
constexpr int kGap = 6;              // between windows and at the edges
constexpr double kAspect = 1.75;     // width/height an instrument face looks right at
constexpr int kTableMinWidth = 260;
}

MdiArranger::MdiArranger(QMdiArea *area, QObject *parent)
  : QObject(parent)
  , m_area(area)
{
  // mouse events of the windows' children (Ctrl+drag) and the viewport's
  // resize go through one application-wide filter; it only looks at them
  // while they concern our area
  qApp->installEventFilter(this);
}

void MdiArranger::addWindow(QMdiSubWindow *window, Role role)
{
  m_order << window;
  m_roles << role;
  setTitleBarHidden(window, m_titleBarsHidden);
  connect(window, &QObject::destroyed, this, [this, window]
  {
    const int i = m_order.indexOf(window);
    if (i >= 0)
    {
      m_order.removeAt(i);
      m_roles.removeAt(i);
    }
  });
  arrange();
}

void MdiArranger::setMode(Mode mode)
{
  m_mode = mode;
  setTitleBarsHidden(mode != Free);
  arrange();
  Q_EMIT changed();
}

void MdiArranger::setTitleBarsHidden(bool hidden)
{
  m_titleBarsHidden = hidden;
  for (QMdiSubWindow *w : m_order)
    setTitleBarHidden(w, hidden);
  arrange();
  Q_EMIT changed();
}

void MdiArranger::setTitleBarHidden(QMdiSubWindow *window, bool hidden)
{
  if (titleBarHidden(window) == hidden && window->property("titleBarKnown").toBool())
    return;
  const QRect g = window->geometry();
  const bool visible = window->isVisible();
  // a frameless sub-window has neither title bar nor border
  window->setWindowFlags(hidden ? Qt::FramelessWindowHint : Qt::SubWindow);
  window->setProperty("titleBarHidden", hidden);
  window->setProperty("titleBarKnown", true);
  window->setGeometry(g);
  if (visible)
  {
    window->show();
    if (!hidden)
    {
      // a window that was activated while frameless draws an empty title
      // bar until it is activated again
      QMdiSubWindow *active = m_area->activeSubWindow();
      m_area->setActiveSubWindow(window);
      if (active && active != window)
        m_area->setActiveSubWindow(active);
    }
  }
  arrange();
}

bool MdiArranger::titleBarHidden(const QMdiSubWindow *window)
{
  return window->property("titleBarHidden").toBool();
}

void MdiArranger::arrange()
{
  if (m_mode == Free || m_pending)
    return;
  m_pending = true;
  QTimer::singleShot(0, this, [this]
  {
    m_pending = false;
    doArrange();
  });
}

// The instruments in a strip on top - as many rows as make them biggest,
// at most 45 % of the height when there is anything else - and below the
// graph, with a table in a column on the right.
QList<QRect> MdiArranger::layout(const QRect &area, const QList<Role> &roles, int headerHeight,
                                 int tableMinWidth)
{
  QList<QRect> out(roles.size());
  const QRect all = area.adjusted(kGap, kGap, -kGap, -kGap);
  QList<int> inst, graphs, tables;
  for (int i = 0; i < roles.size(); ++i)
    (roles[i] == Instrument ? inst : roles[i] == Graph ? graphs : tables) << i;

  // lays ids out side by side (or on top of each other) in r
  auto row = [&](const QList<int> &ids, const QRect &r, Qt::Orientation o)
  {
    const int n = ids.size();
    if (n == 0)
      return;
    const int extent = o == Qt::Horizontal ? r.width() : r.height();
    const int each = (extent - kGap * (n - 1)) / n;
    for (int k = 0; k < n; ++k)
    {
      const int start = k * (each + kGap);
      const int len = k == n - 1 ? extent - start : each;
      out[ids[k]] = o == Qt::Horizontal ? QRect(r.left() + start, r.top(), len, r.height())
                                        : QRect(r.left(), r.top() + start, r.width(), len);
    }
  };
  auto rest = [&](const QRect &r)
  {
    if (graphs.isEmpty())
    {
      row(tables, r, Qt::Vertical);
      return;
    }
    if (tables.isEmpty())
    {
      row(graphs, r, Qt::Vertical);
      return;
    }
    // the table's footer (buttons, statistics) must not be squeezed, but the
    // graph keeps at least half
    const int tw = qMin(qMax(tableMinWidth, int(r.width() * 0.3)), r.width() / 2);
    row(graphs, QRect(r.left(), r.top(), r.width() - tw - kGap, r.height()), Qt::Vertical);
    row(tables, QRect(r.right() - tw + 1, r.top(), tw, r.height()), Qt::Vertical);
  };

  const int n = inst.size();
  if (n == 0)
  {
    rest(all);
    return out;
  }
  const bool onlyInst = graphs.isEmpty() && tables.isEmpty();
  const double maxShare = onlyInst ? 1.0 : 0.45;
  int bestLines = 1;
  double bestSize = 0, bestHeight = 0;
  for (int lines = 1; lines <= n; ++lines)
  {
    const int per = (n + lines - 1) / lines;
    const double cw = double(all.width() - kGap * (per - 1)) / per;
    const double ch = qMin(cw / kAspect + headerHeight, (all.height() * maxShare - kGap * (lines - 1)) / lines);
    const double size = qMin(cw, (ch - headerHeight) * kAspect);
    if (size > bestSize * 1.05)
    {
      bestSize = size;
      bestLines = lines;
      bestHeight = onlyInst ? all.height() : ch * lines + kGap * (lines - 1);
    }
  }
  const int per = (n + bestLines - 1) / bestLines;
  const int stripHeight = int(bestHeight);
  const int lineHeight = (stripHeight - kGap * (bestLines - 1)) / bestLines;
  for (int l = 0; l < bestLines; ++l)
    row(inst.mid(l * per, per), QRect(all.left(), all.top() + l * (lineHeight + kGap), all.width(), lineHeight),
        Qt::Horizontal);
  if (!onlyInst)
    rest(all.adjusted(0, stripHeight + kGap, 0, 0));
  return out;
}

void MdiArranger::arrangeNow()
{
  const Mode mode = m_mode;
  m_mode = DisplaysOnTop;
  doArrange();
  m_mode = mode;
}

void MdiArranger::doArrange()
{
  if (m_mode == Free)
    return;
  QList<QMdiSubWindow *> visible;
  QList<Role> roles;
  for (int i = 0; i < m_order.size(); ++i)
  {
    QMdiSubWindow *w = m_order[i];
    if (!w->isVisible() || w->isMinimized())
      continue;
    if (w->isMaximized())
      w->showNormal();
    visible << w;
    roles << m_roles[i];
  }
  // a header line inside the window, plus the title bar when it is shown
  const int header = m_titleBarsHidden ? 22 : 48;
  int tableMin = kTableMinWidth;
  for (int i = 0; i < visible.size(); ++i)
    if (roles[i] == Table)
      tableMin = qMax(tableMin, visible[i]->minimumSizeHint().width());
  const QList<QRect> rects = layout(m_area->viewport()->rect(), roles, header, tableMin);
  for (int i = 0; i < visible.size(); ++i)
    visible[i]->setGeometry(rects[i]);
}

// ---------------------------------------------------------------- Ctrl+drag

QMdiSubWindow *MdiArranger::subWindowOf(QObject *object) const
{
  for (QObject *o = object; o; o = o->parent())
    if (auto *w = qobject_cast<QMdiSubWindow *>(o))
      return m_order.contains(w) ? w : nullptr;
  return nullptr;
}

bool MdiArranger::eventFilter(QObject *watched, QEvent *event)
{
  switch (event->type())
  {
    case QEvent::Resize:
      if (watched == m_area->viewport())
        arrange();
      break;
    case QEvent::Show:
    case QEvent::Hide:
      if (auto *w = qobject_cast<QMdiSubWindow *>(watched); w && m_order.contains(w))
        arrange();
      break;
    case QEvent::MouseButtonPress:
    {
      // Free mode: Ctrl+drag moves a window, also one without title bar
      auto *me = static_cast<QMouseEvent *>(event);
      if (m_mode != Free || me->button() != Qt::LeftButton || !(me->modifiers() & Qt::ControlModifier))
        break;
      if (QMdiSubWindow *w = subWindowOf(watched))
      {
        m_drag = w;
        m_dragOffset = me->globalPosition().toPoint() - w->pos();
        w->raise();
        return true;
      }
      break;
    }
    case QEvent::MouseMove:
      if (m_drag)
      {
        m_drag->move(static_cast<QMouseEvent *>(event)->globalPosition().toPoint() - m_dragOffset);
        return true;
      }
      break;
    case QEvent::MouseButtonRelease:
      if (m_drag)
      {
        m_drag = nullptr;
        return true;
      }
      break;
    default:
      break;
  }
  return QObject::eventFilter(watched, event);
}
