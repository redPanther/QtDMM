// Tests for MdiArranger: the automatic layout ("Displays on top") and the
// bookkeeping of order and title bars on a real QMdiArea (offscreen).
#include <QApplication>
#include <QDebug>
#include <QLabel>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QTest>

#include "mdiarranger.h"

static int failed = 0;

static void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAILED:" << what;
    failed++;
  }
}

using R = MdiArranger::Role;

// no two rects overlap, all lie inside the area
static void checkTiling(const QRect &area, const QList<QRect> &rects, const QString &name)
{
  for (int i = 0; i < rects.size(); ++i)
  {
    check(area.contains(rects[i]), QString("%1: rect %2 inside the area").arg(name).arg(i));
    check(rects[i].width() > 50 && rects[i].height() > 50, QString("%1: rect %2 not degenerate").arg(name).arg(i));
    for (int j = i + 1; j < rects.size(); ++j)
      check(!rects[i].intersects(rects[j]), QString("%1: rects %2 and %3 do not overlap").arg(name).arg(i).arg(j));
  }
}

static void testLayout()
{
  const QRect area(0, 0, 1200, 700);

  // the full set: instruments on top, graph below, table on the right
  {
    const QList<QRect> r = MdiArranger::layout(area, { R::Instrument, R::Instrument, R::Graph, R::Table }, 22);
    checkTiling(area, r, "full");
    check(r[0].top() == r[1].top() && r[0].height() == r[1].height(), "full: instruments in one row");
    check(r[0].right() < r[1].left(), "full: first instrument on the left");
    check(r[0].bottom() < r[2].top() && r[1].bottom() < r[3].top(), "full: instruments above graph and table");
    check(r[0].height() <= area.height() * 0.45 + 1, "full: instrument strip at most 45 % high");
    check(r[3].left() > r[2].right() && r[3].width() >= 260, "full: table in a column right of the graph");
    check(r[2].width() > r[3].width(), "full: the graph is the wider one");
  }
  // only a graph: all of it
  {
    const QList<QRect> r = MdiArranger::layout(area, { R::Graph }, 22);
    check(r[0] == area.adjusted(6, 6, -6, -6), "graph alone fills the area");
  }
  // only instruments: the whole height
  {
    const QList<QRect> r = MdiArranger::layout(area, { R::Instrument, R::Instrument }, 22);
    checkTiling(area, r, "instruments");
    check(r[0].height() > area.height() * 0.9, "instruments alone take the whole height");
  }
  // a narrow, tall area: two instruments go into two rows
  {
    const QRect tall(0, 0, 500, 1200);
    const QList<QRect> r = MdiArranger::layout(tall, { R::Instrument, R::Instrument }, 22);
    checkTiling(tall, r, "tall");
    check(r[0].bottom() < r[1].top(), "tall: instruments on top of each other");
  }
  // instrument and table, no graph: the table takes the rest
  {
    const QList<QRect> r = MdiArranger::layout(area, { R::Instrument, R::Table }, 22);
    checkTiling(area, r, "no graph");
    check(r[1].width() == area.width() - 12, "no graph: table full width");
  }
}

static void testWindows()
{
  QMdiArea area;
  area.resize(1000, 700);
  MdiArranger arranger(&area);
  QList<QMdiSubWindow *> wins;
  for (const char *name : { "a", "b", "g" })
  {
    QMdiSubWindow *w = area.addSubWindow(new QLabel(name));
    w->setObjectName(name);
    wins << w;
  }
  arranger.addWindow(wins[0], R::Instrument);
  arranger.addWindow(wins[1], R::Instrument);
  arranger.addWindow(wins[2], R::Graph);
  area.show();
  QTest::qWait(50);

  check(arranger.titleBarsHidden(), "title bars hidden in the automatic mode");
  check(MdiArranger::titleBarHidden(wins[0]), "window a has no title bar");
  check(wins[0]->geometry().right() < wins[1]->geometry().left(), "a left of b");
  check(wins[0]->geometry().bottom() < wins[2]->geometry().top(), "instruments above the graph");

  arranger.setOrder({ wins[1], wins[0] });
  QTest::qWait(50);
  check(arranger.order() == QList<QMdiSubWindow *>({ wins[1], wins[0], wins[2] }), "order b, a, then the rest");
  check(wins[1]->geometry().right() < wins[0]->geometry().left(), "after the swap b is left of a");

  wins[1]->hide();
  QTest::qWait(50);
  check(wins[0]->geometry().width() > 800, "with b hidden a takes the whole strip");

  arranger.setMode(MdiArranger::Free);
  check(!arranger.titleBarsHidden() && !MdiArranger::titleBarHidden(wins[0]), "Free brings the title bars back");
  const QRect before = wins[0]->geometry();
  area.resize(600, 400);
  QTest::qWait(50);
  check(wins[0]->geometry() == before, "Free: a resize does not move the windows");

  arranger.setTitleBarHidden(wins[2], true);
  check(MdiArranger::titleBarHidden(wins[2]) && !MdiArranger::titleBarHidden(wins[0]), "one title bar alone");

  // back to the automatic mode, hide and show a window, then title bars on:
  // every visible window has its title bar again
  arranger.setMode(MdiArranger::DisplaysOnTop);
  wins[1]->show();
  wins[0]->hide();
  QTest::qWait(50);
  wins[0]->show();
  QTest::qWait(50);
  arranger.setTitleBarsHidden(false);
  QTest::qWait(50);
  for (QMdiSubWindow *w : wins)
    check(!(w->windowFlags() & Qt::FramelessWindowHint) && !MdiArranger::titleBarHidden(w),
          QString("title bar back on %1").arg(w->objectName()));
}

int main(int argc, char **argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  testLayout();
  testWindows();
  if (failed)
    qWarning() << failed << "check(s) failed";
  else
    qInfo() << "all checks passed";
  return failed ? 1 : 0;
}
