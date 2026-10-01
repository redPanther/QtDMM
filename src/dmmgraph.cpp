//======================================================================
// File:		dmmgraph.cpp
// Author:	Matthias Toussaint
// Created:	Tue Apr 10 17:45:35 CEST 2001
//----------------------------------------------------------------------
// This file is part of QtDMM.
//
// QtDMM is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 3
// as published by the Free Software Foundation.
//
// QtDMM is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
//----------------------------------------------------------------------
// Copyright (c) 2001 Matthias Toussaint
//======================================================================

#include <QtGui>
#include <QtWidgets>
#include <QPen>
#include <QRegularExpression>
#include <cmath>
#include <QSvgGenerator>
#include <QPdfWriter>

#include "dmmgraph.h"

#include <cmath>
#include "siprefix.h"
#include "recordingfile.h"
#include "settings.h"


DMMGraph::DMMGraph(QWidget *parent): DMMGraph(parent, Q_NULLPTR)
{
}
DMMGraph::DMMGraph(QWidget *parent, Settings *settings) :
  QWidget(parent),
  m_size(600),
  m_scaleMin(0),
  m_scaleMax(0),
  m_autoScale(true),
  m_connected(false),
  m_mouseDown(false),
  m_mousePan(false),
  m_cursorMode(NoCursor),
  m_lineWidth(2),
  m_intLineWidth(2),
  m_alertUnsaved(true),
  m_crosshair(true),
  m_pointMode(Circle),
  m_intPointMode(Square),
  m_lineMode(Solid),
  m_intLineMode(NoLine),
  m_integrationScale(1.0),
  m_integrationOffset(0.0),
  m_showIntegration(false),
  m_includeZero(false)
{
  m_cfg = settings;
  m_store = new RecordingStore(this);

  scrollbar = new QScrollBar(Qt::Horizontal, this);
  scrollbar->setGeometry(0, height() - 16, width(), 16);
  scrollbar->setTracking(true);
  scrollbar->setCursor(Qt::ArrowCursor);

  connect(scrollbar, &QScrollBar::valueChanged, this, [this](int) { updateXAxisRange(); updateMarkPositions(); });

  emitInfo();

  m_chart = new QChart();
  m_chart->legend()->hide();
  m_chart->setMargins(QMargins(4, 4, 4, 4));

  m_dataSeries = new QLineSeries();
  m_chart->addSeries(m_dataSeries);

  m_dataPoints = new QScatterSeries();
  m_chart->addSeries(m_dataPoints);

  m_intSeries = new QLineSeries();
  m_chart->addSeries(m_intSeries);

  m_intPoints = new QScatterSeries();
  m_chart->addSeries(m_intPoints);

  m_xAxis = new QValueAxis();
  m_xAxis->setTitleText(tr("[sec]"));
  m_chart->addAxis(m_xAxis, Qt::AlignBottom);
  m_dataSeries->attachAxis(m_xAxis);
  m_dataPoints->attachAxis(m_xAxis);
  m_intSeries->attachAxis(m_xAxis);
  m_intPoints->attachAxis(m_xAxis);

  m_yAxis = new QValueAxis();
  m_chart->addAxis(m_yAxis, Qt::AlignLeft);
  m_defaultLabels = m_yAxis->labelsBrush();
  m_defaultLabelFormat = m_yAxis->labelFormat();
  m_defaultAxisLine = m_yAxis->linePenColor();
  m_yTitle = new QGraphicsSimpleTextItem(m_chart);
  m_yTitle->setFont(m_xAxis->titleFont());
  m_yTitle->setBrush(m_xAxis->titleBrush());
  connect(m_chart, &QChart::plotAreaChanged, this, [this](const QRectF &)
  {
    placeYTitle();
    updateCentreTicks();
    updateXLabels();
  });
  m_centreTicks = new QGraphicsPathItem(m_chart);
  m_centreTicks->setZValue(5);   // above the grid, below the curves' markers
  m_dataSeries->attachAxis(m_yAxis);
  m_dataPoints->attachAxis(m_yAxis);
  m_intSeries->attachAxis(m_yAxis);
  m_intPoints->attachAxis(m_yAxis);

  updateSeriesAppearance();

  // Cursor crosshair + draggable threshold lines, overlaid directly on the
  // chart's graphics scene, above the series (see setZValue below).
  m_crosshairVLine   = new QGraphicsLineItem(m_chart);
  m_crosshairHLine   = new QGraphicsLineItem(m_chart);
  m_triggerLine      = new QGraphicsLineItem(m_chart);
  m_externalLine     = new QGraphicsLineItem(m_chart);
  m_integrationLine  = new QGraphicsLineItem(m_chart);

  for (QGraphicsLineItem *item : {m_crosshairVLine, m_crosshairHLine, m_triggerLine, m_externalLine, m_integrationLine})
  {
    item->setZValue(1000);
    item->setVisible(false);
  }

  m_chartView = new QChartView(m_chart, this);
  m_chartView->setRenderHint(QPainter::Antialiasing);
  // the graph background fills the widget edge to edge, without Qt Charts'
  // inset and rounded corners, so the scrollbar below lines up with it
  m_chart->setBackgroundRoundness(0);
  m_chart->layout()->setContentsMargins(0, 0, 0, 0);
  m_chartView->setFrameShape(QFrame::NoFrame);
  m_chartView->viewport()->setMouseTracking(true);
  m_chartView->viewport()->installEventFilter(this);
  // keyboard zoom/pan once the graph has been clicked
  m_chartView->setFocusPolicy(Qt::ClickFocus);
  m_chartView->installEventFilter(this);

  m_popup = new QMenu(this);
  connect(m_popup, SIGNAL(triggered(QAction *)), this, SLOT(popupSLOT(QAction *)));

  // time buttons top right, as in the UI demo: the visible window at a click
  m_timeBar = new QWidget(m_chartView);
  auto *bar = new QHBoxLayout(m_timeBar);
  bar->setContentsMargins(0, 0, 0, 0);
  bar->setSpacing(2);
  const QList<QPair<QString, int>> steps = { { tr("All"), 0 }, { tr("1 min"), 60 }, { tr("5 min"), 300 },
                                             { tr("30 min"), 1800 } };
  for (const auto &step : steps)
  {
    auto *b = new QToolButton(m_timeBar);
    b->setText(step.first);
    b->setCheckable(true);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setCursor(Qt::ArrowCursor);
    b->setProperty("seconds", step.second);
    b->setToolTip(step.second == 0 ? tr("Show the whole recording, growing with it")
                                   : tr("Show the last %1").arg(step.first));
    connect(b, &QToolButton::clicked, this, [this, b] { timeButtonClicked(b->property("seconds").toInt()); });
    bar->addWidget(b);
    m_timeButtons.append(b);
  }
  updateTimeButtons();

  m_cursorLabel = new QLabel(m_chartView);
  m_cursorLabel->setAttribute(Qt::WA_TransparentForMouseEvents);   // no Leave for the view
  m_cursorLabel->hide();

  connectStore();
}

void DMMGraph::connectStore()
{
  connect(m_store, &RecordingStore::appended, this, &DMMGraph::onAppended);
  connect(m_store, &RecordingStore::cleared, this, &DMMGraph::onCleared);
  connect(m_store, &RecordingStore::marksChanged, this, &DMMGraph::syncMarks);
  connect(m_store, &RecordingStore::progressChanged, this, &DMMGraph::emitInfo);
  connect(m_store, &RecordingStore::runningChanged, this, &DMMGraph::running);
  connect(m_store, &RecordingStore::externalTriggered, this, &DMMGraph::externalTriggered);
  connect(m_store, &RecordingStore::alert, this, [] { QApplication::beep(); });
}

void DMMGraph::setStore(RecordingStore *store)
{
  if (!store || store == m_store)
    return;
  m_store->disconnect(this);
  m_store = store;
  connectStore();
  // the view shows the new store as it is
  onCleared();
  rebuildSeries();
  syncMarks();
  emitInfo();
}

void DMMGraph::timeButtonClicked(int seconds)
{
  m_followAll = (seconds == 0);
  if (m_followAll)
    requestAll(false);
  else if (seconds != m_windowSeconds)
    Q_EMIT windowRequested(seconds);
  updateTimeButtons();   // also when nothing changes: undo the click's own toggle
}

void DMMGraph::requestAll(bool grow)
{
  const double recorded = m_store->count() * sampleTenths() / 10.0;
  // growing by a quarter at a time: the window does not change with every sample
  int target = int(std::ceil(grow ? recorded * 1.25 : recorded));
  target = qMax(target, 10);
  // whole minutes from one minute on: the settings keep seconds only up to
  // 99999, an odd value above would be cut and asked for again with every sample
  if (target > 60)
    target = (target + 59) / 60 * 60;
  if (m_totalSeconds > 0)
    target = qMin(target, m_totalSeconds);
  if (target != m_windowSeconds)
    Q_EMIT windowRequested(target);
}

void DMMGraph::updateTimeButtons()
{
  for (QToolButton *b : m_timeButtons)
  {
    const int seconds = b->property("seconds").toInt();
    // a window longer than the recording is no choice
    b->setVisible(seconds == 0 || m_totalSeconds <= 0 || seconds <= m_totalSeconds);
    b->setChecked(seconds == 0 ? m_followAll : !m_followAll && seconds == m_windowSeconds);
  }
  placeTimeBar();
}

void DMMGraph::placeTimeBar()
{
  if (!m_timeBar)
    return;
  m_timeBar->adjustSize();
  m_timeBar->move(m_chartView->width() - m_timeBar->width() - 6, 3);
  m_timeBar->raise();
}

void DMMGraph::hideCrosshair()
{
  m_crosshairVLine->setVisible(false);
  m_crosshairHLine->setVisible(false);
  if (m_cursorLabel)
    m_cursorLabel->hide();
}

DMMGraph::~DMMGraph()
{
}

void DMMGraph::print(QPrinter *prt, const QString &title, const QString &comment)
{
  if (!title.isEmpty())
    prt->setDocName(title);
  else
    prt->setDocName(tr("QtDMM: %1").arg(QDateTime::currentDateTime().toString()));

  prt->setCreator("QtDMM: (c) 2001 Matthias Toussaint");
  prt->setPrintProgram("QtDMM: (c) 2001 Matthias Toussaint");

  QPainter p;
  p.begin(prt);

  int w = prt->width();
  int h = prt->height();

  p.setFont(QFont("Helvetica", 16));

  QRect tRect = p.boundingRect(0, 0, w, h, Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap, title);

  p.drawText(tRect, Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap, title);

  p.setFont(QFont("Helvetica", 10));

  QFontMetrics fm = p.fontMetrics();
  int maxWidth = qMax(fm.horizontalAdvance(tr("Sampling start:")),
                      fm.horizontalAdvance(tr("Sampling resolution:")));
  int tHeight = fm.height();

  p.drawText(0, tRect.height() + 10, maxWidth, tHeight, Qt::AlignLeft | Qt::AlignVCenter,
             tr("Sampling start:"));
  p.drawText(maxWidth + 10, tRect.height() + 10, w - maxWidth - 10, tHeight, Qt::AlignLeft | Qt::AlignVCenter,
             m_store->startDateTime().toString());

  p.drawText(0, tRect.height() + 10 + tHeight, maxWidth, tHeight, Qt::AlignLeft | Qt::AlignVCenter,
             tr("Sampling resolution:"));
  p.drawText(maxWidth + 10, tRect.height() + 10 + tHeight,
             w - maxWidth - 10, tHeight, Qt::AlignLeft | Qt::AlignVCenter,
             tr("%1 Seconds").arg(sampleTenths()));

  //p.setFont( QFont( "Helvetica", 10 ));

  QRect cRect = p.boundingRect(0, 0, w, h, Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, comment);

  p.drawText(0, tRect.height() + 20 + 2 * tHeight, w, cRect.height(),
             Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, comment);

  h -= tRect.height() + 30 + 2 * tHeight + cRect.height();

  QRectF chartRect(0, tRect.height() + 30 + 2 * tHeight + cRect.height(), w, h);
  m_chartView->render(&p, chartRect);

  p.end();
}

void DMMGraph::resizeEvent(QResizeEvent *)
{
  m_chartView->setGeometry(0, 0, width(), height() - 16);
  scrollbar->setGeometry(0, height() - 16, width(), 16);
  placeTimeBar();

  // Simpler than tracking/restoring hover state across a resize: just hide it,
  // the next mouse move will reposition it correctly.
  hideCrosshair();

  updateThresholdLinePositions();
  // the thinning follows the plot's width
  if (bucketSize() != m_bucket)
    rebuildSeries();
}

// Samples per pixel column (1 = every sample is a point). Counted over the
// window or, while the recording does not fill it yet, over what is there:
// a short recording in a long window is drawn as it is.
int DMMGraph::bucketSize() const
{
  const double plotWidth = m_chart->plotArea().width();
  const int columns = qMax(1, int(plotWidth > 0 ? plotWidth : width()));
  const int shown = qMin(m_size, m_store->count());
  return qMax(1, (shown + columns - 1) / columns);
}

// The index of the first sample in the bucket of sample @p i. The buckets
// count from the start of the recording, not from the ring's oldest sample:
// while a full ring scrolls, a sample stays in its bucket and the drawn
// minima and maxima stay put instead of jittering with every new sample.
int DMMGraph::bucketStart(int i) const
{
  const qint64 seq = m_store->firstSequence() + i;
  return qMax(0, int(seq - seq % m_bucket - m_store->firstSequence()));
}

// The points of the samples first..last: the sample itself, or with more than
// one the minimum and the maximum in time order, so a spike survives the
// thinning. The store keeps every sample; only the drawing is thinned.
int DMMGraph::bucketPoints(int first, int last, bool integral, QList<QPointF> &out) const
{
  const double step = sampleTenths() / 10.0;
  auto value = [&](int i)
  {
    const RecordedPoint &p = m_store->at(i);
    return integral ? m_integrationOffset + p.integral * m_integrationScale : p.value;
  };
  int lo = first, hi = first;
  double loValue = value(first), hiValue = loValue;
  for (int i = first + 1; i <= last; i++)
  {
    const double v = value(i);
    if (v < loValue)
    {
      loValue = v;
      lo = i;
    }
    if (v > hiValue)
    {
      hiValue = v;
      hi = i;
    }
  }
  if (lo == hi)
  {
    out.append(QPointF(lo * step, loValue));
    return 1;
  }
  if (lo > hi)
  {
    qSwap(lo, hi);
    qSwap(loValue, hiValue);
  }
  out.append(QPointF(lo * step, loValue));
  out.append(QPointF(hi * step, hiValue));
  return 2;
}

void DMMGraph::rebuildSeries()
{
  QList<QPointF> points;
  QList<QPointF> intPoints;
  const int count = m_store->count();
  m_bucket = bucketSize();
  points.reserve(count / m_bucket * 2 + 2);
  intPoints.reserve(count / m_bucket * 2 + 2);

  m_tailData = m_tailInt = 0;
  for (int first = 0; first < count;)
  {
    const int last = qMin(bucketStart(first) + m_bucket, count) - 1;
    m_tailData = bucketPoints(first, last, false, points);
    m_tailInt = bucketPoints(first, last, true, intPoints);
    first = last + 1;
  }

  m_dataSeries->replace(points);
  m_dataPoints->replace(points);
  m_intSeries->replace(intPoints);
  m_intPoints->replace(intPoints);
}

// The newest sample went into the series: a new bucket adds its point, one
// that is still filling replaces the points it had.
void DMMGraph::appendToSeries()
{
  const int count = m_store->count();
  const int first = bucketStart(count - 1);
  if (first != count - 1)
  {
    m_dataSeries->removePoints(m_dataSeries->count() - m_tailData, m_tailData);
    m_dataPoints->removePoints(m_dataPoints->count() - m_tailData, m_tailData);
    m_intSeries->removePoints(m_intSeries->count() - m_tailInt, m_tailInt);
    m_intPoints->removePoints(m_intPoints->count() - m_tailInt, m_tailInt);
  }
  QList<QPointF> points, intPoints;
  m_tailData = bucketPoints(first, count - 1, false, points);
  m_tailInt = bucketPoints(first, count - 1, true, intPoints);
  m_dataSeries->append(points);
  m_dataPoints->append(points);
  m_intSeries->append(intPoints);
  m_intPoints->append(intPoints);
}

void DMMGraph::updateXAxisRange()
{
  double step = sampleTenths() / 10.0;
  int sv = qMax(0, scrollbar->value());

  double start = sv * step, end = (sv + qMax(1, m_size) - 1) * step;
  double div = timeStep((end - start) / 6);   // about 5 ticks
  if (divisions() && end > start)
  {
    // 10 divisions of a time step that cover the window, starting on a
    // step: 600 s are 10 x 1 min
    div = timeStep((end - start) / 10);
    double first = std::floor(start / div) * div;
    while (first + 10 * div < end - div * 1e-9)
    {
      div = timeStep(div * 1.01);
      first = std::floor(start / div) * div;
    }
    start = first;
    end = first + 10 * div;
  }
  m_xAxis->setRange(start, end);
  // the ticks on whole steps, counted from the start of the recording
  m_xStep = div;
  m_xAxis->setTickType(QValueAxis::TicksDynamic);
  m_xAxis->setTickAnchor(0);
  m_xAxis->setTickInterval(div);
  updateXLabels();
}

double DMMGraph::timeStep(double v)
{
  if (!(v > 0) || !std::isfinite(v))
    return 1;
  static const double steps[] = { 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800,
                                  3600, 7200, 10800, 21600, 43200, 86400 };
  for (double s : steps)
    if (s >= v * (1 - 1e-12))
      return s;
  return niceStep(v / 86400) * 86400;   // days in 1-2-5
}

// Seconds, minutes or hours, as the step reads best; the title says which.
void DMMGraph::updateXLabels()
{
  if (!m_xAxis || m_xStep <= 0)
    return;
  double unit = 1;
  QString title = tr("[sec]");
  if (m_xStep >= 3600 && std::fmod(m_xStep, 3600) == 0)
  {
    unit = 3600;
    title = tr("[h]");
  }
  else if (m_xStep >= 60 && std::fmod(m_xStep, 60) == 0)
  {
    unit = 60;
    title = tr("[min]");
  }
  if (m_xAxis->titleText() != title)
    m_xAxis->setTitleText(title);

  const QRectF plot = m_chart->plotArea();
  const double min = m_xAxis->min(), max = m_xAxis->max();
  QList<double> values;
  if (max > min && plot.width() > 0)
    for (double v = std::ceil(min / m_xStep - 1e-9) * m_xStep; v <= max + m_xStep * 1e-9 && values.size() < 200; v += m_xStep)
      values << v;
  while (m_xLabels.size() < values.size())
  {
    auto *item = new QGraphicsSimpleTextItem(m_chart);
    item->setZValue(m_yTitle->zValue());
    m_xLabels << item;
  }
  const QFont font = m_xAxis->labelsFont();
  // Qt's labels start below the axis line and its tick marks
  const double top = plot.bottom() + 6;
  for (int i = 0; i < m_xLabels.size(); ++i)
  {
    QGraphicsSimpleTextItem *item = m_xLabels[i];
    item->setVisible(i < values.size());
    if (i >= values.size())
      continue;
    const double v = values[i] / unit;
    item->setText(QString::number(std::abs(v) < 1e-9 ? 0.0 : v, 'g', 6));
    item->setFont(font);
    item->setBrush(m_xLabelColor.isValid() ? QBrush(m_xLabelColor) : m_xAxis->titleBrush());
    const double x = plot.left() + (values[i] - min) / (max - min) * plot.width();
    item->setPos(x - item->boundingRect().width() / 2, top);
  }
}

double DMMGraph::niceStep(double v)
{
  if (!(v > 0) || !std::isfinite(v))
    return 1;
  double decade = std::pow(10.0, std::floor(std::log10(v)));
  for (double m : { 1.0, 2.0, 5.0, 10.0 })
    if (m * decade >= v * (1 - 1e-12))
      return m * decade;
  return 10 * decade;
}

bool DMMGraph::divisions() const
{
  return m_variant == ScopeBlue || phosphor() || m_variant == ChartRecorder;
}

void DMMGraph::setYRange(double min, double max)
{
  if (divisions() && max > min && std::isfinite(min) && std::isfinite(max))
  {
    // 8 divisions of a 1-2-5 step; the range grows to whole steps
    double div = niceStep((max - min) / 8);
    double first = std::floor(min / div + 1e-9) * div;
    while (first + 8 * div < max - div * 1e-9)
    {
      div = niceStep(div * 1.01);
      first = std::floor(min / div + 1e-9) * div;
    }
    m_yAxis->setRange(first, first + 8 * div);
  }
  else
    m_yAxis->setRange(min, max);
  updateCentreTicks();
}

void DMMGraph::updateCentreTicks()
{
  if (!phosphor())
  {
    m_centreTicks->setPath(QPainterPath());
    return;
  }
  const QRectF pa = m_chart->plotArea();
  const double cx = pa.center().x(), cy = pa.center().y();
  QPainterPath path;
  path.moveTo(pa.left(), cy);
  path.lineTo(pa.right(), cy);
  path.moveTo(cx, pa.top());
  path.lineTo(cx, pa.bottom());
  const double dx = pa.width() / 50, dy = pa.height() / 40;   // 10 x 8 divisions, 5 ticks each
  for (int i = 0; i <= 50; ++i)
  {
    path.moveTo(pa.left() + i * dx, cy - 3);
    path.lineTo(pa.left() + i * dx, cy + 3);
  }
  for (int i = 0; i <= 40; ++i)
  {
    path.moveTo(cx - 3, pa.top() + i * dy);
    path.lineTo(cx + 3, pa.top() + i * dy);
  }
  m_centreTicks->setPath(path);
}

void DMMGraph::updateSeriesAppearance()
{
  m_dataSeries->setPen(QPen(dataColor(), m_lineWidth, penStyle(m_lineMode), Qt::RoundCap, Qt::RoundJoin));
  m_dataSeries->setVisible(m_lineMode != NoLine);

  QScatterSeries::MarkerShape shape = QScatterSeries::MarkerShapeCircle;
  int size = 7;

  switch (m_pointMode)
  {
    case NoPoint:                                                                     break;
    case Circle:      case LargeCircle:  shape = QScatterSeries::MarkerShapeCircle;    break;
    case Square:      case LargeSquare:  shape = QScatterSeries::MarkerShapeRectangle; break;
    // Qt Charts has no "X"/cross marker; approximate both Diamond and X with a
    // rotated square, the closest built-in shape available.
    case Diamond:     case LargeDiamond:
    case X:           case LargeX:       shape = QScatterSeries::MarkerShapeRotatedRectangle; break;
  }
  if (m_pointMode == LargeCircle || m_pointMode == LargeSquare ||
      m_pointMode == LargeDiamond || m_pointMode == LargeX)
    size = 11;

  m_dataPoints->setMarkerShape(shape);
  m_dataPoints->setMarkerSize(size);
  m_dataPoints->setColor(dataColor());
  // Qt Charts rims the markers in white: dense points turn into a white band
  m_dataPoints->setBorderColor(dataColor());
  m_dataPoints->setVisible(m_pointMode != NoPoint);

  // phosphor: one colour, the integration curve dashed when it is a solid line
  const Qt::PenStyle intStyle = phosphor() && m_intLineMode == Solid ? Qt::DashLine : penStyle(m_intLineMode);
  m_intSeries->setPen(QPen(intColor(), m_intLineWidth, intStyle, Qt::RoundCap, Qt::RoundJoin));
  m_intSeries->setVisible(m_showIntegration && m_intLineMode != NoLine);

  QScatterSeries::MarkerShape intShape = QScatterSeries::MarkerShapeCircle;
  int intSize = 7;

  switch (m_intPointMode)
  {
    case NoPoint:                                                                        break;
    case Circle:      case LargeCircle:  intShape = QScatterSeries::MarkerShapeCircle;    break;
    case Square:      case LargeSquare:  intShape = QScatterSeries::MarkerShapeRectangle; break;
    case Diamond:     case LargeDiamond:
    case X:           case LargeX:       intShape = QScatterSeries::MarkerShapeRotatedRectangle; break;
  }
  if (m_intPointMode == LargeCircle || m_intPointMode == LargeSquare ||
      m_intPointMode == LargeDiamond || m_intPointMode == LargeX)
    intSize = 11;

  m_intPoints->setMarkerShape(intShape);
  m_intPoints->setMarkerSize(intSize);
  m_intPoints->setColor(intColor());
  m_intPoints->setBorderColor(intColor());
  m_intPoints->setVisible(m_showIntegration && m_intPointMode != NoPoint);
}

void DMMGraph::updateThresholdLinesVisibility()
{
  m_triggerLine->setVisible(mode() == Raising || mode() == Falling);
  m_externalLine->setVisible(m_store->externalOn());
  m_integrationLine->setVisible(m_showIntegration);

  updateThresholdLinePositions();
}

void DMMGraph::updateThresholdLinePositions()
{
  QRectF plot = m_chart->plotArea();

  auto positionLine = [&](QGraphicsLineItem *item, double value)
  {
    if (!item->isVisible())
      return;
    double y = m_chart->mapToPosition(QPointF(m_xAxis->min(), value), m_dataSeries).y();
    item->setLine(plot.left(), y, plot.right(), y);
  };

  positionLine(m_triggerLine, mode() == Raising ? m_store->raisingThreshold() : m_store->fallingThreshold());
  positionLine(m_externalLine, m_store->externalThreshold());
  positionLine(m_integrationLine, m_store->integrationThreshold());
  updateMarkPositions();
}

void DMMGraph::addMark(const QColor &color, const QString &name)
{
  m_store->addMark(color.rgba(), name);
}

void DMMGraph::syncMarks()
{
  qDeleteAll(m_marks);
  m_marks.clear();
  for (const RecordingStore::Mark &m : m_store->marks())
  {
    auto *line = new QGraphicsLineItem(m_chart);
    line->setPen(QPen(QColor::fromRgba(m.color), 2, Qt::DashLine));
    line->setZValue(999);
    line->setToolTip(m.name);
    m_marks << line;
  }
  updateMarkPositions();
}

void DMMGraph::updateMarkPositions()
{
  const QRectF plot = m_chart->plotArea();
  const double step = sampleTenths() / 10.0;
  const QList<RecordingStore::Mark> marks = m_store->marks();
  for (int i = 0; i < m_marks.size() && i < marks.size(); i++)
  {
    QGraphicsLineItem *line = m_marks[i];
    const double x = marks[i].index * step;
    const bool inView = x >= m_xAxis->min() && x <= m_xAxis->max();
    line->setVisible(inView);
    if (!inView)
      continue;
    const double px = m_chart->mapToPosition(QPointF(x, m_yAxis->min()), m_dataSeries).x();
    line->setLine(px, plot.top(), px, plot.bottom());
  }
}

void DMMGraph::setGraphSize(int size, int length)
{
  m_windowSeconds = size;
  m_totalSeconds = length;
  m_size = static_cast<int>((static_cast<double>(size) / sampleTenths() * 10.));
  const int samples = static_cast<int>((static_cast<double>(length) / sampleTenths() * 10. + 1));

  scrollbar->setMinimum(0);
  scrollbar->setMaximum(samples - 1 - m_size);
  scrollbar->setSingleStep((m_size - 1) / 10);
  scrollbar->setPageStep(m_size);

  m_store->setCapacity(samples);

  emitInfo();

  rebuildSeries();
  updateXAxisRange();
  updateThresholdLinePositions();
  updateTimeButtons();
}

void DMMGraph::setSampleTime(int v)
{
  if (v <= 0 || v == m_store->sampleTime())
    return;
  m_store->setSampleTime(v);
  // m_size and the store's capacity count samples of the old sample time
  if (m_windowSeconds > 0)
    setGraphSize(m_windowSeconds, m_totalSeconds);
}

void DMMGraph::startSLOT()
{
  m_store->start();
}

void DMMGraph::stopSLOT()
{
  m_store->stop();
}

void DMMGraph::onAppended(bool shifted)
{
  const int count = m_store->count();
  const RecordedPoint &p = m_store->last();

  // "All": the window grows once the recording fills it
  if (m_followAll && count >= m_size)
    requestAll(true);

  const bool resFlag = m_autoScale && computeMinMax(p.value);

  if (shifted || bucketSize() != m_bucket)
    rebuildSeries();
  else
    appendToSeries();

  if (resFlag)
  {
    setYRange(m_scaleMin, m_scaleMax);
    updateThresholdLinePositions();
  }
}

void DMMGraph::setUnit(const QString &unit)
{
  // Values arrive in SI base units (see DmmResponse), so the axis shows the
  // base unit and the prefix is dropped here.
  const QString base = SiPrefix::split(unit).baseUnit;
  m_store->setUnit(base);

  m_yTitle->setText(base.isEmpty() ? QString() : QString("[%1]").arg(base));
  // room above the plot for the title
  const int h = base.isEmpty() ? 0 : int(m_yTitle->boundingRect().height()
                                             + QFontMetricsF(m_yAxis->labelsFont()).height() / 2);
  // and at least as much as the time buttons need, so they sit above the plot
  const int bar = m_timeBar ? m_timeBar->sizeHint().height() : 0;
  m_chart->setMargins(QMargins(4, 4 + qMax(h, bar), 4, 4));
  placeYTitle();
}

// right-aligned with the axis line, just above the plot area
void DMMGraph::placeYTitle()
{
  const QRectF plot = m_chart->plotArea();
  const QRectF text = m_yTitle->boundingRect();
  // the top tick label is centred on the plot's top edge: stay above it
  const double labelHalf = QFontMetricsF(m_yAxis->labelsFont()).height() / 2;
  m_yTitle->setPos(qMax(2.0, plot.left() - text.width() - 4), plot.top() - labelHalf - text.height() - 1);
}

void DMMGraph::clearSLOT()
{
  m_store->clear();
}

void DMMGraph::onCleared()
{
  if (m_autoScale)
  {
    if (m_includeZero)
      m_scaleMin = m_scaleMax = 0;
    else
    {
      m_scaleMin =  1e40;
      m_scaleMax = -1e40;
    }
  }

  m_dataSeries->clear();
  m_dataPoints->clear();
  m_intSeries->clear();
  m_intPoints->clear();
  m_tailData = m_tailInt = 0;
}

void DMMGraph::emitInfo()
{
  const int seconds = m_store->remainingLength() / 10;
  const int pointer = m_store->count();
  const int length = m_store->capacity();
  const bool running = m_store->isRunning();

  int w = seconds / 60 / 60 / 24 / 7;
  int d = (seconds / 60 / 60 / 24) % 7;
  int h = (seconds / 60 / 60) % (24);
  int m = (seconds / 60) % 60;
  int s = seconds % 60;

  QString txt;

  if (w)
    txt = QString("%1/%2 - %3week%4 %5day&6 %7:%8:%9 - %10").arg(pointer).arg(length).arg(w).arg((w > 1 ? "s" : "")).arg(d).arg((d > 1 ? "s" : "")).arg(h).arg(m).arg(s)
          .arg(running ? tr("Sampling") : tr("Stopped"));
  else if (d)
    txt = QString("%1/%2 - %3day%4 %5:%6:%7 - %8").arg(pointer).arg(length).arg(d).arg((d > 1 ? "s" : "")).arg(h).arg(m).arg(s).arg(running ? tr("Sampling") : tr("Stopped"));
  else if (h)
    txt = QString("%1/%2 - %3:%4:%5 - %6").arg(pointer).arg(length).arg(h).arg(m).arg(s).arg(running ? tr("Sampling") : tr("Stopped"));
  else
    txt = QString("%1/%2 - %3:%4 - %5").arg(pointer).arg(length).arg(m).arg(s).arg(running ? tr("Sampling") : tr("Stopped"));
  Q_EMIT info(txt);
}

// m_chartView is a child widget covering the whole graph area, so mouse/wheel
// events over it are delivered to its viewport, not to DMMGraph's own
// mousePressEvent/etc. overrides. An event filter on the viewport is the
// standard way to intercept them while keeping all interaction state/logic on
// DMMGraph itself (it already owns everything these handlers need).
bool DMMGraph::eventFilter(QObject *watched, QEvent *event)
{
  if (watched == m_chartView && event->type() == QEvent::KeyPress)
    return handleChartKey(static_cast<QKeyEvent *>(event)) || QWidget::eventFilter(watched, event);

  if (watched == m_chartView->viewport())
  {
    switch (event->type())
    {
      case QEvent::MouseButtonPress:
        handleChartMousePress(static_cast<QMouseEvent *>(event));
        return true;
      case QEvent::MouseMove:
        handleChartMouseMove(static_cast<QMouseEvent *>(event));
        return true;
      case QEvent::MouseButtonRelease:
        handleChartMouseRelease(static_cast<QMouseEvent *>(event));
        return true;
      case QEvent::Wheel:
        handleChartWheel(static_cast<QWheelEvent *>(event));
        return true;
      case QEvent::Leave:
        hideCrosshair();
        break;
      default:
        break;
    }
  }

  return QWidget::eventFilter(watched, event);
}

void DMMGraph::handleChartMousePress(QMouseEvent *ev)
{
  QPoint pos(qRound(ev->position().x()), qRound(ev->position().y()));
  QPoint globalPos(qRound(ev->globalPosition().x()), qRound(ev->globalPosition().y()));

  if (ev->button() == Qt::LeftButton)
  {
    m_mouseDown = true;
    m_mousePan = false;
  }
  else if (ev->button() == Qt::MiddleButton)
  {
    m_mouseDown = false;
    m_mousePan = true;
    m_mpos = pos;
  }
  else if (ev->button() == Qt::RightButton)
  {
    m_popup->clear();

    if (m_connected)
    {
      QAction *action = new QAction(tr("Disconnect"), m_popup);
      action->setProperty("ID", IDDisconnect);
      m_popup->addAction(action);
      //m_popup->insertItem( tr("Disconnect"), IDDisconnect );
    }
    else
    {
      QAction *action = new QAction(tr("Connect"), m_popup);
      action->setProperty("ID", IDConnect);
      m_popup->addAction(action);
      //m_popup->insertItem( tr("Connect"), IDConnect );
    }
    m_popup->addSeparator();

    if (m_store->isRunning())
    {
      QAction *action = new QAction(tr("Stop recorder"), m_popup);
      action->setProperty("ID", IDStopRecorder);
      m_popup->addAction(action);
      //m_popup->insertItem( tr("Stop recorder"), IDStopRecorder );
    }
    else
    {
      QAction *action = new QAction(tr("Start recorder"), m_popup);
      action->setProperty("ID", IDStartRecorder);
      m_popup->addAction(action);
      //m_popup->insertItem( tr("Start recorder"), IDStartRecorder );
    }
    QAction *action = new QAction(tr("Clear graph"), m_popup);
    action->setProperty("ID", IDClearGraph);
    m_popup->addAction(action);
    //m_popup->insertItem( tr("Clear graph"), IDClearGraph );
    m_popup->addSeparator();

    action = new QAction(tr("Configure..."), m_popup);
    action->setProperty("ID", IDConfigure);
    m_popup->addAction(action);
    //m_popup->insertItem( tr("Configure..."), IDConfigure );
    action = new QAction(tr("Copy image"), m_popup);
    action->setProperty("ID", IDCopyImage);
    m_popup->addAction(action);
    action = new QAction(tr("Export image..."), m_popup);
    action->setProperty("ID", IDExportImage);
    m_popup->addAction(action);

    // this graph's colours: the default from the settings page, or one of
    // the variants for this graph only
    QMenu *colours = m_popup->addMenu(tr("Graph &colours"));
    auto variant = [this, colours](const QString &text, int v)
    {
      QAction *a = colours->addAction(text);
      a->setProperty("ID", IDColorVariant);
      a->setProperty("variant", v);
      a->setCheckable(true);
      a->setChecked(m_variantOverride == v);
    };
    variant(tr("&Default: %1").arg(variantTitle(m_defaultVariant)), -1);
    colours->addSeparator();
    for (ColorVariant v : { Neutral, ScopeBlue, PhosphorGreen, PhosphorAmber, ChartRecorder, Custom })
      variant(variantTitle(v), v);

    if (!m_store->isRunning())
    {
      m_popup->addSeparator();
      QAction *action = new QAction(tr("Export data..."), m_popup);
      action->setProperty("ID", IDExportData);
      m_popup->addAction(action);
      //m_popup->insertItem( tr("Export data..."), IDExportData );
      action = new QAction(tr("Import data..."), m_popup);
      action->setProperty("ID", IDImportData);
      m_popup->addAction(action);
      //m_popup->insertItem( tr("Import data..."), IDImportData );
    }

    m_popup->popup(globalPos);
  }
}

void DMMGraph::handleChartMouseMove(QMouseEvent *ev)
{
  QPoint pos(qRound(ev->position().x()), qRound(ev->position().y()));

  if (m_mousePan)
  {
    QRectF plot = m_chart->plotArea();
    double range = m_xAxis->max() - m_xAxis->min();
    if (plot.width() <= 0 || range <= 0)
      return;

    double pixelsPerSecond = plot.width() / range;
    double dxSeconds = (m_mpos.x() - pos.x()) / pixelsPerSecond;
    double dxSamples = dxSeconds / (sampleTenths() / 10.0);

    if (fabs(dxSamples) >= 1)
    {
      int sv = qMax(0, scrollbar->value());
      scrollbar->setValue(qBound(0, sv + qRound(dxSamples), scrollbar->maximum()));
      m_mpos = pos;
    }
    return;
  }

  if (m_mouseDown && m_cursorMode != NoCursor)
  {
    QPointF scenePos = m_chartView->mapToScene(pos);
    double value = m_chart->mapToValue(scenePos, m_dataSeries).y();

    switch (m_cursorMode)
    {
      case Trigger:
        if (mode() == Raising) m_store->setRaisingThreshold(value);
        else                   m_store->setFallingThreshold(value);
        break;
      case External:
        m_store->setExternalThreshold(value);
        break;
      case Integration:
        m_store->setIntegrationThreshold(value);
        break;
      case NoCursor:
        break;
    }

    updateThresholdLinePositions();
    Q_EMIT thresholdChanged(m_cursorMode, value);
    return;
  }

  // Pure hover: hit-test the draggable threshold lines (trigger, then
  // external, then integration - first match wins when lines overlap) and,
  // failing that, drive the crosshair.
  if (!m_mouseDown && !m_mousePan)
  {
    QPointF scenePos = m_chartView->mapToScene(pos);
    const int tolerance = 3;

    auto near = [&](QGraphicsLineItem *item)
    {
      return item->isVisible() && fabs(scenePos.y() - item->line().y1()) < tolerance;
    };

    if (near(m_triggerLine))
      m_cursorMode = Trigger;
    else if (near(m_externalLine))
      m_cursorMode = External;
    else if (near(m_integrationLine))
      m_cursorMode = Integration;
    else
      m_cursorMode = NoCursor;

    m_chartView->viewport()->setCursor(m_cursorMode == NoCursor ? Qt::ArrowCursor : Qt::SplitVCursor);

    if (m_cursorMode != NoCursor || !m_crosshair)
    {
      hideCrosshair();
      return;
    }

    QRectF plot = m_chart->plotArea();
    double x = qBound(plot.left(), scenePos.x(), plot.right());

    m_crosshairVLine->setLine(x, plot.top(), x, plot.bottom());
    m_crosshairVLine->setVisible(true);

    double xValue = m_chart->mapToValue(QPointF(x, scenePos.y()), m_dataSeries).x();
    int idx = qRound(xValue / (sampleTenths() / 10.0));

    QString text = m_store->startDateTime().time().addSecs(int(idx * sampleTenths() / 10)).toString();

    if (idx >= 0 && idx < m_store->count())
    {
      double val = m_store->at(idx).value;
      QPointF scenePoint = m_chart->mapToPosition(QPointF(xValue, val), m_dataSeries);
      m_crosshairHLine->setLine(plot.left(), scenePoint.y(), plot.right(), scenePoint.y());
      m_crosshairHLine->setVisible(true);
      QString unit;
      text += "   " + QString("%1 %2").arg(formatEngineeringValue(val, &unit)).arg(unit);
    }
    else
      m_crosshairHLine->setVisible(false);

    // fixed above the plot, left-aligned so only its end moves with the text
    m_cursorLabel->setText(text);
    m_cursorLabel->adjustSize();
    const QPoint topLeft = m_chartView->mapFromScene(plot.topLeft());
    m_cursorLabel->move(topLeft.x() + 4, 3);
    m_cursorLabel->show();
    m_cursorLabel->raise();
  }
}

void DMMGraph::handleChartMouseRelease(QMouseEvent *)
{
  m_mouseDown = false;
  m_mousePan = false;
}

void DMMGraph::handleChartWheel(QWheelEvent *ev)
{
  m_followAll = false;
  if (ev->angleDelta().x() < 0 || ev->angleDelta().y() < 0)
    Q_EMIT zoomOut(1.1);
  else
    Q_EMIT zoomIn(1.1);
}

QString DMMGraph::formatEngineeringValue(double value, QString *unit) const
{
  QString prefix;
  const QString text = SiPrefix::format(value, &prefix);
  if (unit)
    *unit = prefix + m_store->unit();
  return text;
}

bool DMMGraph::exportDataSLOT()
{
  QDir path;
  QFileInfo fileInfo(m_cfg->getString("QtDMM/LastUsesPath"));
  QStringList validSuffixes = { "csv", "xlsx", "ods" };
  QString fnSuffix = validSuffixes.contains(fileInfo.suffix()) ? fileInfo.suffix() : "csv";
  QString fn = fileInfo.baseName().isEmpty() ? "untitled." + fnSuffix : fileInfo.absolutePath() + "/untitled." + fnSuffix;
  const QString csvFilter = tr("CSV (*.csv)"), xlsxFilter = tr("Excel (*.xlsx)"), odsFilter = tr("OpenDocument (*.ods)");
  QString filter = fnSuffix == "xlsx" ? xlsxFilter : fnSuffix == "ods" ? odsFilter : csvFilter;
  fn = QFileDialog::getSaveFileName(this, tr("Export data"), fn, csvFilter + ";;" + xlsxFilter + ";;" + odsFilter, &filter);

  if (fn.isNull())
    return false;
  // the chosen filter decides when no suffix was typed
  if (QFileInfo(fn).suffix().isEmpty())
    fn += filter == xlsxFilter ? ".xlsx" : filter == odsFilter ? ".ods" : ".csv";

  return exportCsvFile(fn);
}

bool DMMGraph::exportCsvFile(const QString &fileName)
{
  if (m_store->count() <= 0)
    return false;

  m_cfg->setString("QtDMM/LastUsesPath", QDir().absoluteFilePath(fileName));

  QString err;
  if (!m_store->write(fileName, &err))
  {
    Q_EMIT error(err);
    return false;
  }
  return true;
}


void DMMGraph::importDataSLOT()
{
  if (m_store->dirty() && m_alertUnsaved)
  {
    QMessageBox question;
    question.setWindowTitle(tr("QtDMM: Unsaved data"));
    question.setText(tr("<font size=+2><b>Unsaved data</b></font><p>"
                        "Importing data will overwrite your measured data"
                        "<p>Do you want to export your unsaved data first?"));
    question.setIcon(QMessageBox::Question);

    // Standard-Buttons
    question.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    question.setDefaultButton(QMessageBox::Yes);
    question.setEscapeButton(QMessageBox::Cancel);

    QAbstractButton *yesButton = question.button(QMessageBox::Yes);
    if (yesButton)
      yesButton->setText(tr("Export data first"));

    QAbstractButton *noButton = question.button(QMessageBox::No);
    if (noButton)
      noButton->setText(tr("Import & overwrite data"));

    switch (question.exec())
    {
      case QMessageBox::Yes:
        exportDataSLOT();
        return;
      case QMessageBox::Cancel:
        return;
    }
  }
  QString fn = QFileDialog::getOpenFileName(this, tr("Import data"), m_cfg->getString("QtDMM/LastUsesPath", tr("CSV (*.csv);;All files (*)")));

  if (!fn.isNull())
    importCsvFile(fn);
}

bool DMMGraph::importCsvFile(const QString &fileName)
{
  m_cfg->setString("QtDMM/LastUsesPath", QDir().absoluteFilePath(fileName));

  QString err;
  const std::optional<Recording> rec = RecordingFile::read(fileName, &err);
  if (!rec)
  {
    Q_EMIT error(err);
    return false;
  }

  setUnit(rec->unit);
  m_store->setSampleTime(rec->sampleTimeTenths);
  const int cnt = int(rec->values.size());
  // setGraphSize() counts in seconds, the sample time is in tenths of one
  const int size = qMax(1, int(std::ceil(m_size * sampleTenths() / 10.0)));
  const int length = qMax(1, int(std::ceil(cnt * sampleTenths() / 10.0)));

  if (cnt > 1)
    Q_EMIT sampleTime(m_store->sampleTime());

  m_scaleMin =  1e40;
  m_scaleMax = -1e40;

  setGraphSize(size, length);
  m_store->load(*rec);

  setScale(true, true, 0, 0);
  // setGraphSize() above built the series before the values were in; the
  // graph shows the import by itself, not only after MainWid applies the
  // new size
  rebuildSeries();
  updateXAxisRange();

  Q_EMIT error(fileName);
  update();
  Q_EMIT graphSize(size, length);
  return true;
}


void DMMGraph::setThresholds(double falling, double raising)
{
  m_store->setThresholds(falling, raising);

  updateThresholdLinesVisibility();
}

void DMMGraph::setMode(DMMGraph::SampleMode mode)
{
  m_store->setStartMode(RecordingStore::StartMode(mode));

  updateThresholdLinesVisibility();
}

void DMMGraph::setScale(bool autoScale, bool includeZero, double min, double max)
{
  m_autoScale = autoScale;
  m_includeZero = includeZero;

  if (!autoScale)
  {
    m_scaleMin = min;
    m_scaleMax = max;


  }
  else
  {
    if (m_includeZero)
      m_scaleMin = m_scaleMax = 0;
    else
    {
      m_scaleMin =  1e40;
      m_scaleMax = -1e40;
    }


    for (int i = 0; i < m_store->count(); i++)
    {
      const double val = m_store->at(i).value;
      computeMinMax(val);
    }


  }

  setYRange(m_scaleMin, m_scaleMax);
  updateThresholdLinePositions();
}

bool DMMGraph::computeMinMax(double val)
{
  bool ret = false;

  if (val > m_scaleMax * 0.95)
  {
    if (val > 0)
      m_scaleMax = val * 1.2;
    else
      m_scaleMax = val / 1.2;

    ret = true;
  }
  if (val < m_scaleMin * 0.95)
  {
    if (val > 0)
      m_scaleMin = val / 1.2;
    else
      m_scaleMin = val * 1.2;

    ret = true;
  }

  return ret;
}

void DMMGraph::setColors(const QColor &bg, const QColor &grid,
                         const QColor &data, const QColor &cursor,
                         const QColor &start, const QColor &external,
                         const QColor &integration, const QColor &intThreshold)
{
  m_bgColor           = bg;
  m_gridColor         = grid;
  m_dataColor         = data;
  m_cursorColor       = cursor;
  m_startColor        = start;
  m_externalColor     = external;
  m_intColor          = integration;
  m_intThresholdColor = intThreshold;

  applyThemeColors();
  updateSeriesAppearance();
}

void DMMGraph::setThemeColors(const QBrush &background, const QColor &grid, const QColor &labels,
                              const QColor &data)
{
  m_themeBackground = background;
  m_themeGrid = grid;
  m_themeLabels = labels;
  m_themeData = data;
  applyThemeColors();
  updateSeriesAppearance();
}

// The curve colour: a colour chosen in the settings stays in every
// variant; the default blue is replaced by the variant's (or the design's).
QColor DMMGraph::dataColor() const
{
  if (m_variant == Custom || m_dataColor != QColor(Qt::blue))
    return m_dataColor;
  switch (m_variant)
  {
    case ScopeBlue:     return QColor("#ffe14d");
    case PhosphorGreen: return QColor::fromHsv(135, 190, 255);
    case PhosphorAmber: return QColor::fromHsv(38, 230, 255);
    case ChartRecorder: return QColor("#1f4e9c");
    default:            return m_themeData.isValid() ? m_themeData : m_dataColor;
  }
}

// The same for the integration curve (default dark blue); phosphor keeps
// one colour and tells the curves apart by brightness and dashes.
QColor DMMGraph::intColor() const
{
  if (m_variant == Custom || m_intColor != QColor(Qt::darkBlue))
    return m_intColor;
  switch (m_variant)
  {
    case ScopeBlue:     return QColor("#4de8ff");
    case PhosphorGreen: return QColor::fromHsv(135, 190, 190);
    case PhosphorAmber: return QColor::fromHsv(38, 230, 190);
    case ChartRecorder: return QColor("#c0282d");
    default:            return m_themeData.isValid() ? m_themeData.lighter(140) : m_intColor;
  }
}

void DMMGraph::setColorVariant(ColorVariant defaultVariant, int override)
{
  m_defaultVariant = defaultVariant;
  m_variantOverride = override;
  m_variant = override >= 0 ? static_cast<ColorVariant>(override) : defaultVariant;
  applyThemeColors();
  updateSeriesAppearance();
}

QString DMMGraph::variantTitle(ColorVariant variant)
{
  switch (variant)
  {
    case ScopeBlue:     return tr("Scope blue");
    case PhosphorGreen: return tr("Phosphor green");
    case PhosphorAmber: return tr("Phosphor amber");
    case ChartRecorder: return tr("Chart recorder");
    case Custom:        return tr("Custom");
    default:            return tr("Neutral");
  }
}

QString DMMGraph::variantName(ColorVariant variant)
{
  switch (variant)
  {
    case ScopeBlue:     return "scope";
    case PhosphorGreen: return "phosphor-green";
    case PhosphorAmber: return "phosphor-amber";
    case ChartRecorder: return "recorder";
    case Custom:        return "custom";
    default:            return "neutral";
  }
}

DMMGraph::ColorVariant DMMGraph::variantFromName(const QString &name)
{
  for (ColorVariant v : { ScopeBlue, PhosphorGreen, PhosphorAmber, ChartRecorder, Custom })
    if (name == variantName(v))
      return v;
  return Neutral;
}

// Background, grid and lettering. Neutral follows the window design (on
// System the palette), Custom takes the settings page, the others bring
// their own; the scope-like ones also switch to 10 x 8 divisions.
void DMMGraph::applyThemeColors()
{
  QBrush background, plot(Qt::NoBrush);
  QPen grid, minor, line;
  QColor labels;
  int minors = 0;
  auto vgrad = [](const QColor &top, const QColor &bottom)
  {
    QLinearGradient g(0, 0, 0, 1);
    g.setCoordinateMode(QGradient::ObjectBoundingMode);
    g.setColorAt(0, top);
    g.setColorAt(1, bottom);
    return QBrush(g);
  };
  switch (m_variant)
  {
    case ScopeBlue:
      background = QColor("#0b1633");
      plot = vgrad(QColor("#16306a"), QColor("#081338"));
      grid = QPen(QColor(90, 130, 200, 150), 1, Qt::DashLine);
      line = QPen(QColor("#5a78b0"));
      labels = QColor("#b9c9e8");
      break;
    case PhosphorGreen:
    case PhosphorAmber:
    {
      const bool green = m_variant == PhosphorGreen;
      background = green ? QColor("#0a0d0a") : QColor("#0d0b08");
      plot = green ? QColor("#060d08") : QColor("#0e0a04");
      grid = QPen(green ? QColor(60, 150, 85, 110) : QColor(190, 120, 30, 110), 1);
      line = grid;
      labels = green ? QColor("#5fbf78") : QColor("#d99a2b");
      m_centreTicks->setPen(QPen(green ? QColor(80, 190, 110, 150) : QColor(220, 150, 50, 150), 1));
      break;
    }
    case ChartRecorder:
      background = QColor("#f4eedb");
      plot = QColor("#fbf6e6");
      grid = QPen(QColor(214, 140, 130, 190), 1);
      minor = QPen(QColor(226, 176, 166, 110), 0.6);
      line = QPen(QColor(190, 120, 110));
      labels = QColor("#5a4636");
      minors = 4;   // millimetre-ish paper
      break;
    case Custom:
      background = m_bgColor;
      grid = QPen(m_gridColor);
      line = QPen(m_defaultAxisLine);
      // lettering that reads on the chosen background
      labels = m_bgColor.lightness() < 128 ? QColor(220, 220, 220) : QColor(30, 30, 30);
      break;
    case Neutral:
      if (m_themeBackground.style() != Qt::NoBrush)
      {
        background = m_themeBackground;
        grid = QPen(m_themeGrid);
        labels = m_themeLabels;
      }
      else
      {
        // the System design: the palette, as the table and the dialogs
        const QPalette pal = palette();
        background = pal.base();
        grid = QPen(pal.color(QPalette::Mid));
        labels = pal.color(QPalette::Text);
      }
      line = QPen(labels);
      break;
  }
  m_chart->setBackgroundBrush(background);
  // the scrollbar in the graph's colours: a dark graph gets no light strip
  {
    QPalette pal = scrollbar->palette();
    for (QPalette::ColorRole role : { QPalette::Window, QPalette::Base, QPalette::Button })
      pal.setBrush(role, background);
    pal.setColor(QPalette::WindowText, labels);
    pal.setColor(QPalette::ButtonText, labels);
    scrollbar->setPalette(pal);
    scrollbar->setAutoFillBackground(true);
  }
  // the time buttons in the lettering colour, the chosen one framed
  if (m_timeBar)
    m_timeBar->setStyleSheet(QString("QToolButton { color: %1; border: 1px solid transparent; border-radius: 2px;"
                                     " padding: 0px 4px; background: transparent; }"
                                     " QToolButton:checked { border-color: %1; }"
                                     " QToolButton:hover { border-color: %2; }")
                               .arg(labels.name(), QColor(labels.red(), labels.green(), labels.blue(), 110).name(QColor::HexArgb)));
  if (m_cursorLabel)
    m_cursorLabel->setStyleSheet(QString("QLabel { color: %1; background: transparent; }").arg(labels.name()));
  m_chart->setPlotAreaBackgroundBrush(plot);
  m_chart->setPlotAreaBackgroundVisible(plot.style() != Qt::NoBrush);
  for (QValueAxis *axis : { m_xAxis, m_yAxis })
  {
    axis->setGridLinePen(grid);
    axis->setMinorTickCount(minors);
    axis->setMinorGridLineVisible(minors > 0);
    if (minors > 0)
      axis->setMinorGridLinePen(minor);
    axis->setLabelsBrush(labels);
    axis->setTitleBrush(labels);
    axis->setLinePen(line);
  }
  // 10 x 8 divisions for the scope-like variants, Qt's default otherwise;
  // their round steps need no forced decimals. The x ticks are time steps
  // (updateXAxisRange()), labelled by updateXLabels(): Qt's own x labels
  // only keep the room for them.
  m_yAxis->setTickCount(divisions() ? 9 : 5);
  m_xAxis->setLabelFormat("%.4g");
  m_yAxis->setLabelFormat(divisions() ? "%.4g" : m_defaultLabelFormat);
  m_xLabelColor = labels;
  m_xAxis->setLabelsBrush(Qt::transparent);
  m_yTitle->setBrush(labels);

  // cursor and threshold lines: a colour chosen in the settings stays,
  // the defaults follow the variant (a black cursor on phosphor is lost)
  const bool own = m_variant == Custom;
  auto pick = [own](const QColor &setting, Qt::GlobalColor def, const QColor &variant)
  {
    return own || setting != QColor(def) || !variant.isValid() ? setting : variant;
  };
  QColor start, external;
  switch (m_variant)
  {
    case ScopeBlue:     start = QColor("#ff5fd2"); external = QColor("#6dff6d"); break;
    case PhosphorGreen: start = QColor::fromHsv(135, 190, 160); external = QColor::fromHsv(135, 120, 130); break;
    case PhosphorAmber: start = QColor::fromHsv(38, 230, 160); external = QColor::fromHsv(38, 150, 130); break;
    case ChartRecorder: start = QColor("#2e7d32"); external = QColor("#222222"); break;
    default: break;
  }
  const Qt::PenStyle lineStyle = phosphor() ? Qt::DashLine : Qt::SolidLine;
  const QColor cursor = pick(m_cursorColor, Qt::black, labels);
  m_crosshairVLine->setPen(QPen(cursor));
  m_crosshairHLine->setPen(QPen(cursor));
  m_triggerLine->setPen(QPen(pick(m_startColor, Qt::magenta, start), 1, lineStyle));
  m_externalLine->setPen(QPen(pick(m_externalColor, Qt::cyan, external), 1, lineStyle));
  // dotted: by default it takes the integration curve's colour and must not
  // pass for a curve (phosphor dashes the curve)
  m_integrationLine->setPen(QPen(pick(m_intThresholdColor, Qt::darkBlue, intColor()), 1, Qt::DotLine));
  updateXAxisRange();
  setYRange(m_scaleMin, m_scaleMax);
  updateThresholdLinePositions();
}

void DMMGraph::setLineStyle(int lineMode, int pointMode, int intLineMode, int intPointMode)
{
  m_lineMode = static_cast<LineMode>(lineMode);
  m_pointMode = static_cast<PointMode>(pointMode);
  m_intLineMode = static_cast<LineMode>(intLineMode);
  m_intPointMode = static_cast<PointMode>(intPointMode);

  updateSeriesAppearance();
}

void DMMGraph::setLine(int d, int i)
{
  m_lineWidth    = d;
  m_intLineWidth = i;

  updateSeriesAppearance();
}

void DMMGraph::setExternal(bool on, bool falling, double threshold)
{
  m_store->setExternal(on, falling, threshold);

  updateThresholdLinesVisibility();
}

Qt::PenStyle DMMGraph::penStyle(LineMode mode)
{
  switch (mode)
  {
    case NoLine:
      return Qt::NoPen;
    case Solid:
      return Qt::SolidLine;
    case Dot:
      return Qt::DotLine;
  }
  return Qt::SolidLine;
}

void DMMGraph::setIntegration(bool showInt, double sc, double th, double off)
{
  m_showIntegration = showInt;
  m_integrationScale = sc;
  m_store->setIntegrationThreshold(th);
  m_integrationOffset = off;

  rebuildSeries();
  updateSeriesAppearance();
  updateThresholdLinesVisibility();
}


void DMMGraph::popupSLOT(QAction *action)
{
  switch (action->property("ID").toInt())
  {
    case IDConnect:
      Q_EMIT connectDMM(true);
      break;
    case IDDisconnect:
      Q_EMIT connectDMM(false);
      break;
    case IDStopRecorder:
      stopSLOT();
      break;
    case IDStartRecorder:
      startSLOT();
      break;
    case IDClearGraph:
      clearSLOT();
      break;
    case IDConfigure:
      Q_EMIT configure();
      break;
    case IDExportData:
      Q_EMIT exportData();
      break;
    case IDImportData:
      Q_EMIT importData();
      break;
    case IDCopyImage:
      copyImageSLOT();
      break;
    case IDExportImage:
      exportImageSLOT();
      break;
    case IDColorVariant:
      setColorVariant(m_defaultVariant, action->property("variant").toInt());
      Q_EMIT colorVariantChanged(m_variantOverride);
      break;
  }
}

bool DMMGraph::handleChartKey(QKeyEvent *ev)
{
  const bool shift = ev->modifiers() & Qt::ShiftModifier;

  switch (ev->key())
  {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
      zoomInSLOT();
      return true;
    case Qt::Key_Minus:
      zoomOutSLOT();
      return true;
    case Qt::Key_0:
      zoomFitSLOT();
      return true;
    case Qt::Key_Left:
      pan(shift ? -0.5 : -0.1);
      return true;
    case Qt::Key_Right:
      pan(shift ? 0.5 : 0.1);
      return true;
    case Qt::Key_PageUp:
      pan(-1.0);
      return true;
    case Qt::Key_PageDown:
      pan(1.0);
      return true;
    case Qt::Key_Home:
      scrollToStart();
      return true;
    case Qt::Key_End:
      scrollToEnd();
      return true;
    default:
      return false;
  }
}

void DMMGraph::pan(double fraction)
{
  int step = qMax(1, qRound(qMax(1, m_size) * fabs(fraction)));
  if (fraction < 0)
    step = -step;
  scrollbar->setValue(qBound(0, scrollbar->value() + step, scrollbar->maximum()));
}

void DMMGraph::scrollToStart()
{
  scrollbar->setValue(0);
}

void DMMGraph::scrollToEnd()
{
  scrollbar->setValue(scrollbar->maximum());
}

void DMMGraph::copyImageSLOT()
{
  QGuiApplication::clipboard()->setPixmap(m_chartView->grab());
}

bool DMMGraph::exportImageSLOT()
{
  QFileInfo fileInfo(m_cfg->getString("QtDMM/LastImagePath"));
  const QStringList suffixes = { "svg", "pdf", "png", "jpg" };
  const QString suffix = suffixes.contains(fileInfo.suffix().toLower()) ? fileInfo.suffix().toLower() : "svg";
  const QString name = fileInfo.baseName().isEmpty() ? QString("untitled")
                                                     : fileInfo.absolutePath() + "/" + fileInfo.baseName();
  const QString svgFilter = tr("Scalable vector graphics (*.svg)"), pdfFilter = tr("PDF (*.pdf)"),
                pngFilter = tr("PNG image (*.png)"), jpgFilter = tr("JPEG image (*.jpg)");
  QString filter = suffix == "pdf" ? pdfFilter : suffix == "png" ? pngFilter
                 : suffix == "jpg" ? jpgFilter : svgFilter;
  QString fn = QFileDialog::getSaveFileName(this, tr("Export image"), name + "." + suffix,
                                            svgFilter + ";;" + pdfFilter + ";;" + pngFilter + ";;" + jpgFilter,
                                            &filter);
  if (fn.isNull())
    return false;
  // the dialog leaves the name alone when the user typed one: follow the
  // chosen filter, so picking "PDF" and typing "plot" does write a PDF
  const QString chosen = filter == pdfFilter ? "pdf" : filter == pngFilter ? "png"
                       : filter == jpgFilter ? "jpg" : "svg";
  if (!suffixes.contains(QFileInfo(fn).suffix().toLower()))
    fn += "." + chosen;

  m_cfg->setString("QtDMM/LastImagePath", QDir().absoluteFilePath(fn));
  return exportImageFile(fn);
}

bool DMMGraph::exportImageFile(const QString &fileName, QSize size)
{
  // the crosshair follows the mouse and would be baked into the picture at
  // whatever point the user right-clicked
  const bool crosshairShown = m_crosshairVLine->isVisible() || m_crosshairHLine->isVisible();
  if (crosshairShown)
  {
    m_crosshairVLine->hide();
    m_crosshairHLine->hide();
  }
  const bool ok = writeImage(fileName, size);
  if (crosshairShown)
  {
    m_crosshairVLine->show();
    m_crosshairHLine->show();
  }
  return ok;
}

bool DMMGraph::writeImage(const QString &fileName, QSize size)
{
  if (size.isEmpty())
    size = m_chartView->size();
  if (size.isEmpty())
    size = QSize(1024, 640);
  const QString suffix = QFileInfo(fileName).suffix().toLower();

  if (suffix == "svg")
  {
    QSvgGenerator svg;
    svg.setFileName(fileName);
    svg.setSize(size);
    svg.setViewBox(QRect(QPoint(0, 0), size));
    svg.setTitle(m_store->startDateTime().isValid()
                 ? tr("QtDMM recording, %1").arg(m_store->startDateTime().toString(Qt::ISODate))
                 : tr("QtDMM graph"));
    svg.setDescription(tr("%1 values, %2 s per sample, unit %3")
                       .arg(m_store->count()).arg(sampleTenths() / 10.0).arg(m_store->unit()));
    QPainter p;
    if (!p.begin(&svg))
    {
      Q_EMIT error(tr("Could not write %1").arg(fileName));
      return false;
    }
    // into the requested size, as PDF and PNG: at the view's own size the
    // drawing sat in a corner of a larger viewBox
    m_chartView->render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
    p.end();
  }
  else if (suffix == "pdf")
  {
    QPdfWriter pdf(fileName);
    pdf.setTitle(tr("QtDMM graph"));
    pdf.setCreator("QtDMM");
    // the page takes the graph's proportions, so nothing is stretched
    pdf.setPageSize(QPageSize(QSizeF(size.width(), size.height()), QPageSize::Point,
                              QString(), QPageSize::ExactMatch));
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter p;
    if (!p.begin(&pdf))
    {
      Q_EMIT error(tr("Could not write %1").arg(fileName));
      return false;
    }
    m_chartView->render(&p, QRectF(QPointF(0, 0), QSizeF(pdf.width(), pdf.height())));
    p.end();
  }
  else
  {
    QPixmap pixmap(size);
    pixmap.fill(Qt::white);
    QPainter p(&pixmap);
    m_chartView->render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
    p.end();
    if (!pixmap.save(fileName))
    {
      Q_EMIT error(tr("Could not write %1").arg(fileName));
      return false;
    }
  }
  Q_EMIT info(tr("Graph written to %1").arg(QFileInfo(fileName).fileName()));
  return true;
}
