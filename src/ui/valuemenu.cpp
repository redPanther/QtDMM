// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/valuemenu.h"

#include <QActionGroup>
#include <QMenu>

#include "ui/instancewidget.h"
#include "ui/views/graphwidget.h"

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

  // the LCD: three places, big and the two on the small line. A choice
  // takes the place; the value there goes where this one was, or off the
  // display when it had none
  QMenu *lcd = menu->addMenu(tr("&LCD"));
  auto *places = new QActionGroup(lcd);
  const QList<int> small = wid->lcdSecondaries();
  const int taken[] = { wid->lcdValue(), int(small.value(0, -1)), int(small.value(1, -1)) };
  const QString texts[] = { tr("&Main value"), tr("Secondary value &left"), tr("Secondary value &right") };
  for (int place = 0; place < 3; ++place)
  {
    QAction *a = checkable(lcd, texts[place], taken[place] == id);
    places->addAction(a);
    // a meter with two values has one place on the small line
    a->setEnabled(place == 0 || place < wid->numValues());
    QObject::connect(a, &QAction::triggered, menu, [wid, id, place, show]
    {
      const QList<int> small = wid->lcdSecondaries();
      QList<int> now { wid->lcdValue(), int(small.value(0, -1)), int(small.value(1, -1)) };
      const qsizetype from = now.indexOf(id);
      if (from >= 0)
        now[from] = now[place];
      now[place] = id;
      QList<int> line;
      for (int i = 1; i < 3; ++i)
        if (now[i] >= 0)
          line << now[i];
      wid->setLcdValue(now[0]);
      wid->setLcdSecondaries(line);
      show(View::Lcd);
    });
  }
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
