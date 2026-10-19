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

  QObject::connect(checkable(menu, tr("&Analog meter"), wid->meterValue() == id), &QAction::triggered, menu,
                   [wid, id, show]
  {
    wid->setMeterValue(id);
    show(View::Meter);
  });
  QObject::connect(checkable(menu, tr("&Poincaré plot"), wid->poincareValue() == id), &QAction::triggered, menu,
                   [wid, id, show]
  {
    wid->setPoincareValue(id);
    show(View::Poincare);
  });

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

  // the graph: shown or not, and on which axis
  GraphWidget *graph = wid->graph();
  QMenu *curves = menu->addMenu(tr("&Graph"));
  const bool shown = id == 0 ? graph->mainShown() : graph->shownValues().contains(id);
  QAction *inGraph = checkable(curves, tr("&Show in graph"), shown);
  if (!shown && graph->valueAxis(id) == GraphWidget::AxisAuto && !graph->valueFits(id))
  {
    inGraph->setEnabled(false);
    inGraph->setToolTip(tr("Two units are shown already: choose an axis for it"));
  }
  QObject::connect(inGraph, &QAction::triggered, menu, [graph, id, show](bool on)
  {
    graph->setValueShown(id, on);
    if (on)
      show(View::Graph);
  });
  curves->addSeparator();
  auto *axes = new QActionGroup(curves);
  const std::pair<GraphWidget::ValueAxis, QString> choices[] = { { GraphWidget::AxisAuto, tr("&Automatic axis") },
                                                                 { GraphWidget::AxisLeft, tr("&Left axis") },
                                                                 { GraphWidget::AxisRight, tr("&Right axis") } };
  for (const auto &[axis, text] : choices)
  {
    QAction *a = checkable(curves, text, graph->valueAxis(id) == axis);
    axes->addAction(a);
    if (id == 0 && axis != GraphWidget::AxisLeft)
    {
      a->setEnabled(false);
      a->setToolTip(tr("The main value is on the left axis"));
    }
    QObject::connect(a, &QAction::triggered, menu, [graph, id, axis = axis, show]
    {
      graph->setValueAxis(id, axis);
      // automatic: on only when there is an axis for it
      if (axis != GraphWidget::AxisAuto || graph->valueFits(id))
        graph->setValueShown(id, true);
      show(View::Graph);
    });
  }
}
