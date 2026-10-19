// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/valuemenu.h"

#include <QActionGroup>
#include <QMenu>

#include "ui/instancewidget.h"
#include "ui/views/graphwidget.h"
#include "ui/views/lcdwidget.h"

void ValueMenu::fill(QMenu *menu, InstanceWidget *wid, int id, const std::function<void(View)> &show)
{
  menu->setToolTipsVisible(true);
  auto checkable = [](QMenu *m, const QString &text, bool on)
  {
    QAction *a = m->addAction(text);
    a->setCheckable(true);
    a->setChecked(on);
    return a;
  };

  // the LCD: big, or on the small line
  QMenu *lcd = menu->addMenu(tr("&LCD"));
  const bool primary = wid->lcdValue() == id;
  QObject::connect(checkable(lcd, tr("&Main value"), primary), &QAction::triggered, menu, [wid, id, show]
  {
    // a value from the small line swaps places with the big one
    const int before = wid->lcdValue();
    QList<int> small = wid->lcdSecondaries();
    const qsizetype at = small.indexOf(id);
    if (at >= 0)
      small[at] = before;
    wid->setLcdValue(id);
    wid->setLcdSecondaries(small);
    show(View::Lcd);
  });
  QAction *secondary = checkable(lcd, tr("&Secondary value"), wid->lcdSecondaries().contains(id));
  secondary->setEnabled(!primary);
  if (primary)
    secondary->setToolTip(tr("It is the main value"));
  QObject::connect(secondary, &QAction::triggered, menu, [wid, id, show](bool on)
  {
    QList<int> small = wid->lcdSecondaries();
    small.removeAll(id);
    if (on)
    {
      small << id;
      while (small.size() > LcdWidget::kSmallLineValues)
        small.removeFirst();   // the older one goes
    }
    wid->setLcdSecondaries(small);
    show(View::Lcd);
  });

  QObject::connect(checkable(menu, tr("&Analog meter"), wid->meterValue() == id), &QAction::triggered, menu,
                   [wid, id, show]
  {
    wid->setMeterValue(id);
    show(View::Meter);
  });
  // the graph: off, or on an axis. A unit the axis has already joins it,
  // another one takes its place there (GraphWidget::setValueAxis())
  GraphWidget *graph = wid->graph();
  QMenu *curves = menu->addMenu(tr("&Graph"));
  auto *sides = new QActionGroup(curves);
  const bool shown = id == 0 ? graph->mainShown() : graph->shownValues().contains(id);
  const GraphWidget::ValueAxis side = graph->valueSide(id);
  const std::pair<GraphWidget::ValueAxis, QString> choices[] = { { GraphWidget::AxisAuto, tr("&Off") },
                                                                 { GraphWidget::AxisLeft, tr("&Left axis") },
                                                                 { GraphWidget::AxisRight, tr("&Right axis") } };
  for (const auto &[axis, text] : choices)
  {
    const bool off = axis == GraphWidget::AxisAuto;
    QAction *a = checkable(curves, text, off ? !shown : side == axis);
    sides->addAction(a);
    if (id == 0 && axis == GraphWidget::AxisRight)
    {
      a->setEnabled(false);
      a->setToolTip(tr("The main value is on the left axis"));
    }
    QObject::connect(a, &QAction::triggered, menu, [graph, id, axis = axis, off, show]
    {
      if (off)
      {
        graph->setValueShown(id, false);
        return;
      }
      graph->setValueAxis(id, axis);
      graph->setValueShown(id, true);
      show(View::Graph);
    });
  }
  QObject::connect(checkable(menu, tr("&Poincaré plot"), wid->poincareValue() == id), &QAction::triggered, menu,
                   [wid, id, show]
  {
    wid->setPoincareValue(id);
    show(View::Poincare);
  });
}
