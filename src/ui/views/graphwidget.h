//======================================================================
// File:		dmmgraph.h
// Author:	Matthias Toussaint
// Created:	Tue Apr 10 17:43:46 CEST 2001
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

#pragma once

#include <QtGui>
#include <QtWidgets>
#include <QPrinter>
#include <QChartView>
#include <QChart>
#include <QLineSeries>
#include <QScatterSeries>
#include <QValueAxis>
#include <QGraphicsLineItem>

#include "ui/views/gapline.h"
#include "recording/recordingstore.h"

class Settings;

/// The recorder's view: plots the curve a RecordingStore keeps.
///
/// The store does the recording (every reading with its time, triggers,
/// integral, marks); the graph shows a window of it in time (scrollable),
/// scales the y axis, and draws the integral with its scale and offset, the
/// cursor and threshold lines and the marks. The x axis is the time since
/// the start of the recording; more readings than pixel columns are thinned
/// to the minimum and maximum of each column. Rendering uses Qt Charts; the
/// cursor and threshold lines are QGraphicsLineItems on top of the chart.
/// The recorder settings (setSampleTime(), setMode(), setThresholds(), ...)
/// are passed on to the store, so the graph can still be driven as a whole.
/// Data can be exported/imported as CSV and printed.
///
/// The sample time is in tenths of a second, like the settings: the grid
/// of the export, not the rate the graph shows.
class GraphWidget : public QWidget
{
  Q_OBJECT
public:
  /// How recording is started.
  enum SampleMode
  {
    Manual = 0,   ///< Start button
    Time,         ///< at setStartTime()
    Raising,      ///< when the reading rises through the raising threshold
    Falling       ///< when the reading falls through the falling threshold
  };

  /// Marker drawn at each sample.
  enum PointMode
  {
    NoPoint = 0,
    Circle,
    Square,
    Diamond,
    X,
    LargeCircle,
    LargeSquare,
    LargeDiamond,
    LargeX
  };

  /// Line style between samples.
  enum LineMode
  {
    NoLine = 0,
    Solid,
    Dot
  };

  /// Which horizontal threshold line the mouse is dragging.
  enum CursorMode
  {
    NoCursor = 0,
    Trigger,      ///< recording start threshold
    External,     ///< external application threshold
    Integration   ///< integration threshold
  };

  /// The graph's colours (context menu "Graph colours", Graph/variant).
  /// Each brings background, grid, lettering and curve colours; a curve
  /// colour chosen in the settings stays. Custom is the colours from the
  /// settings page, as before the variants.
  enum ColorVariant
  {
    Neutral,         ///< follows the window design
    ScopeBlue,       ///< oscilloscope, blue screen
    PhosphorGreen,   ///< green phosphor tube: one colour, 10 x 8 divisions
    PhosphorAmber,   ///< the same in amber
    ChartRecorder,   ///< paper and ink
    Custom           ///< the colours from the settings page
  };

  /// Entries of the context menu.
  enum PopupID
  {
    IDConnect = 1,
    IDDisconnect,
    IDStopRecorder,
    IDStartRecorder,
    IDClearGraph,
    IDConfigure,
    IDExportData,
    IDImportData,
    IDCopyImage,
    IDExportImage,
    IDColorVariant   ///< property "variant" says which
  };

  GraphWidget(QWidget *parent, Settings *settings);
  GraphWidget(QWidget *parent = Q_NULLPTR);
  ~GraphWidget();
  /// Visible window and total recording length, both in seconds.
  void             setGraphSize(int size, int length);
  /// The recorder behind the graph.
  RecordingStore  *store() const { return m_store; }
  /// Shows @p store instead of the graph's own one (the MeterController's
  /// recorder). The graph does not take ownership; the store has to outlive it.
  void             setStore(RecordingStore *store);
  /// Unit of the recorded quantity for the axis label; the SI prefix is
  /// stripped because values arrive in base units (see DmmDecoder::DmmResponse).
  void             setUnit(const QString &);
  /// Sample time in tenths of a second.
  /// The window and the length are kept in seconds: a new sample time
  /// recounts them in samples, so the order of the two calls does not matter.
  void             setSampleTime(int v);
  /// Recording duration in tenths of a second after which recording stops
  /// on its own (0 = until stopped).
  void             setSampleLength(int v) { m_store->setSampleLength(v); }
  /// Clock time for SampleMode::Time.
  void             setStartTime(const QTime &time) { m_store->setStartTime(time); }
  void             setMode(GraphWidget::SampleMode mode);
  /// Prints the curve with title and comment.
  void             print(QPrinter *prt, const QString &, const QString &);
  /// Thresholds for the Raising/Falling start modes.
  void             setThresholds(double falling, double raising);
  /// Y axis: automatic (optionally always including zero) or fixed min/max.
  void             setScale(bool autoScale, bool includeZero, double min, double max);
  void             setColors(const QColor &bg, const QColor &grid,
                             const QColor &data, const QColor &cursor,
                             const QColor &start, const QColor &external,
                             const QColor &integration, const QColor &intThreshold);
  /// Colours of the window design (background, grid, axis lettering); a
  /// background of Qt::NoBrush means the colours from the settings. @p data
  /// replaces the curve colour only while that is the default blue.
  void             setThemeColors(const QBrush &background, const QColor &grid, const QColor &labels,
                                  const QColor &data = QColor());
  /// The colours in use: @p override for this graph (context menu), or
  /// the default from the settings page when @p override is -1.
  void             setColorVariant(ColorVariant defaultVariant, int override = -1);
  ColorVariant     colorVariant() const { return m_variant; }
  int              colorOverride() const { return m_variantOverride; }
  /// "neutral", "scope", "phosphor-green", "phosphor-amber", "recorder", "custom"
  static QString   variantName(ColorVariant variant);
  static ColorVariant variantFromName(const QString &name);
  /// The variant's name in the menus ("Scope blue").
  static QString   variantTitle(ColorVariant variant);
  /// The smallest step of the 1-2-5 series (..., 0.5, 1, 2, 5, 10, ...) that
  /// is at least @p v; the division of the scope variants. Public for the tests.
  static double    niceStep(double v);
  /// The smallest time step (..., 1, 2, 5, 10, 15, 30 s, 1, 2, 5, 10, 15,
  /// 30 min, 1, 2, 3, 6, 12 h, days) that is at least @p v seconds: the
  /// x division. Public for the tests.
  static double    timeStep(double v);
  /// Line widths of the data and the integration curve.
  void             setLine(int d, int i);
  /// Draws a vertical mark at the current sample (an alarm raised); marks
  /// move with the data and go with clearSLOT().
  void             addMark(const QColor &color, const QString &name);
  int              markCount() const { return int(m_marks.size()); }
  /// External application trigger: fire externalTriggered() once per
  /// recording when the reading crosses @p threshold in the given direction.
  void             setExternal(bool on, bool falling = false, double threshold = 0);
  /// Unsaved recorded data in memory.
  bool             dirty() const { return m_store->dirty(); }
  void             setAlertUnsaved(bool on) { m_alertUnsaved = on; }
  void             setCrosshair(bool on) { m_crosshair = on; }
  /// LineMode and PointMode for the data and the integration curve.
  void             setLineStyle(int, int, int, int);
  /// Integration curve: shown, scale factor, threshold (values at or below
  /// it reset the sum) and offset.
  void             setIntegration(bool, double, double, double);
  void             setSettings(Settings *settings) { m_cfg = settings; }

Q_SIGNALS:
  /// Status bar text: sample time, window and remaining length.
  void             info(const QString &);
  void             error(const QString &);
  /// Recording started/stopped.
  void             running(bool);
  /// Window/total size changed by zooming (seconds).
  void             graphSize(int, int);
  /// Sample time changed by a CSV import (tenths of a second).
  void             sampleTime(int);
  /// The external application threshold was crossed.
  void             externalTriggered();
  /// The context menu chose this graph's colours: a ColorVariant, or -1
  /// for the default from the settings page.
  void             colorVariantChanged(int override);
  void             zoomIn(double);
  void             zoomOut(double);
  /// Show the whole recording (key 0).
  void             zoomFit();
  /// A time button asks for this visible window (seconds).
  void             windowRequested(int seconds);
  /// A threshold line was dragged with the mouse.
  void             thresholdChanged(GraphWidget::CursorMode, double);
  /// @name Context menu requests, handled by InstanceWidget
  /// @{
  void             connectDMM(bool);
  void             configure();
  void             exportData();
  void             importData();
  /// @}

public Q_SLOTS:
  /// Discards the recorded data.
  void             clearSLOT();
  /// @name Keyboard zoom/pan, also reachable from MainWindow's shortcuts
  /// @{
  void             zoomInSLOT()  { m_followAll = false; Q_EMIT zoomIn(1.25); }
  void             zoomOutSLOT() { m_followAll = false; Q_EMIT zoomOut(1.25); }
  void             zoomFitSLOT() { m_followAll = false; Q_EMIT zoomFit(); }
  /// Shifts the visible window by a fraction of its width (negative = back).
  void             pan(double fraction);
  void             scrollToStart();
  void             scrollToEnd();
  /// Puts a picture of the graph on the clipboard.
  void             copyImageSLOT();
  /// Writes the graph to a file chosen in a dialog (SVG, PDF, PNG).
  bool             exportImageSLOT();
  /// @}
  void             startSLOT();
  void             stopSLOT();
  /// Export with a file dialog; returns false when cancelled or failed.
  bool             exportDataSLOT();
  void             importDataSLOT();
  void             connectSLOT(bool on) { m_connected = on; }

  /// File-path-driven, non-interactive halves of export/importDataSLOT (no QFileDialog),
  /// split out so the CSV parsing/writing logic can be exercised from tests.
  bool             exportCsvFile(const QString &fileName);
  bool             importCsvFile(const QString &fileName);
  /// Writes the graph to @p fileName, format taken from the suffix (svg, pdf,
  /// png, jpg, bmp). SVG and PDF keep the curve, the axes and their labels as
  /// vectors; @p size is the drawing size in points, the widget's own when
  /// empty. Non-interactive half of exportImageSLOT(), for the test.
  bool             exportImageFile(const QString &fileName, QSize size = QSize());

protected Q_SLOTS:
  void             popupSLOT(QAction *action);

protected:
  /// exportImageFile() without hiding the crosshair.
  bool             writeImage(const QString &fileName, QSize size);

  QScrollBar      *scrollbar;
  qint64           m_bucket = 1;    ///< ms per drawn min/max pair (see bucketSize())
  int              m_tailData = 0;  ///< points the newest bucket gave the data line (1 or 2; a gap is 1)
  int              m_tailInt = 0;   ///< the same for the integral
  int              m_tailDataPts = 0;  ///< and the data points (a gap has none)
  int              m_tailIntPts = 0;
  int              m_windowSeconds = 600;   ///< the visible window, setGraphSize()
  /// @name Time buttons (All / 1 min / 5 min / 30 min) top right in the graph
  /// @{
  QWidget         *m_timeBar = nullptr;
  QList<QToolButton *> m_timeButtons;   ///< property "seconds": 0 = All
  /// Time and value under the crosshair, fixed top left above the plot (a
  /// tooltip window trailed behind the mouse).
  QLabel          *m_cursorLabel = nullptr;
  void             hideCrosshair();
  bool             m_followAll = false;   ///< "All": the window grows with the recording
  void             timeButtonClicked(int seconds);
  /// "All": asks for a window that holds the recording so far (plus room
  /// to grow when @p grow), at least 10 s and at most the recording length.
  void             requestAll(bool grow);
  void             updateTimeButtons();
  void             placeTimeBar();
  /// @}
  int              m_totalSeconds = 0;
  double           m_scaleMin;
  double           m_scaleMax;
  bool             m_autoScale;
  RecordingStore  *m_store;
  SampleMode       mode() const { return SampleMode(m_store->startMode()); }
  bool             m_connected;
  /// The store's signals: a new reading, discarded or replaced readings.
  void             connectStore();
  /// Where the window starts (ms since the start of the recording).
  qint64           windowStart() const;
  qint64           bucketSize() const;
  int              bucketStart(int i) const;
  int              bucketPoints(int first, int last, bool integral, QList<QPointF> &out) const;
  static QList<QPointF> withoutGaps(const QList<QPointF> &points);
  static int       finiteTail(const QList<QPointF> &points, int tail);
  void             appendToSeries();
  void             onAppended(bool shifted);
  void             onCleared();
  /// The mark lines anew from the store's marks.
  void             syncMarks();
  QPoint           m_mpos;
  bool             m_mouseDown;
  bool             m_mousePan;
  CursorMode       m_cursorMode;
  QColor           m_bgColor;
  QColor           m_gridColor;
  QColor           m_dataColor;
  QColor           m_cursorColor;
  QColor           m_startColor;
  QColor           m_externalColor;
  QColor           m_intColor;
  QColor           m_intThresholdColor;
  int              m_lineWidth;
  int              m_intLineWidth;
  bool             m_alertUnsaved;
  bool             m_crosshair;
  PointMode        m_pointMode;
  PointMode        m_intPointMode;
  LineMode         m_lineMode;
  LineMode         m_intLineMode;
  double           m_integrationScale;
  double           m_integrationOffset;
  bool             m_showIntegration;
  bool             m_includeZero;
  QMenu           *m_popup;

  // Qt Charts based rendering (core curve + axes, plus the integration curve
  // and the cursor/threshold overlays reintroduced as QGraphicsLineItems on
  // top of the chart scene).
  QChartView      *m_chartView;
  QChart          *m_chart;
  QLineSeries     *m_dataSeries;   ///< the data line's first segment (positions map with it)
  QScatterSeries  *m_dataPoints;
  QLineSeries     *m_intSeries;
  QScatterSeries  *m_intPoints;
  std::unique_ptr<GapLine> m_dataLine;   ///< the data line, split at the gaps
  std::unique_ptr<GapLine> m_intLine;
  QValueAxis      *m_xAxis;
  QValueAxis      *m_yAxis;
  /// The y axis title ("[V]"), written horizontally above the axis: turned
  /// by 90 degrees a "V" reads like ">".
  QGraphicsSimpleTextItem *m_yTitle;
  QBrush           m_themeBackground;   ///< Qt::NoBrush: m_bgColor
  QColor           m_themeGrid;
  QColor           m_themeLabels;
  QColor           m_themeData;         ///< proposed curve colour, invalid = none
  QColor           dataColor() const;   ///< the curve colour in use
  QColor           intColor() const;    ///< the integration curve colour in use
  ColorVariant     m_variant = Neutral;          ///< in use
  ColorVariant     m_defaultVariant = Neutral;   ///< from the settings page
  int              m_variantOverride = -1;       ///< this graph's choice, -1 = default
  /// Scope, phosphor and recorder: 10 x 8 divisions of 1-2-5 steps.
  bool             divisions() const;
  bool             phosphor() const { return m_variant == PhosphorGreen || m_variant == PhosphorAmber; }
  /// The y range, widened to 8 whole divisions of 1-2-5 steps when divisions().
  void             setYRange(double min, double max);
  /// Phosphor: the centre axes with 5 fine ticks per division.
  QGraphicsPathItem *m_centreTicks;
  void             updateCentreTicks();
  QBrush           m_defaultLabels;     ///< the chart's own label colour
  QColor           m_defaultAxisLine;   ///< the chart's own axis line colour
  QString          m_defaultLabelFormat; ///< Qt's, for the variants without divisions
  void             applyThemeColors();
  void             placeYTitle();
  void             showUnit();
  /// @name x labels in s, min or h
  /// Qt's axis can only print the seconds; its own labels are kept for the
  /// layout but drawn invisible, these are drawn over them at every tick.
  /// @{
  QList<QGraphicsSimpleTextItem *> m_xLabels;
  double           m_xStep = 0;         ///< seconds between the x ticks
  QColor           m_xLabelColor;
  void             updateXLabels();
  /// @}
  QGraphicsLineItem *m_crosshairVLine;
  QGraphicsLineItem *m_crosshairHLine;
  QGraphicsLineItem *m_triggerLine;
  QGraphicsLineItem *m_externalLine;
  QGraphicsLineItem *m_integrationLine;
  /// Alarm marks: a vertical line at the sample the alarm raised on, one
  /// per RecordingStore::marks() in the same order.
  QList<QGraphicsLineItem *> m_marks;
  void             updateMarkPositions();

  void             resizeEvent(QResizeEvent *)Q_DECL_OVERRIDE;
  bool             eventFilter(QObject *watched, QEvent *event) Q_DECL_OVERRIDE;

  void             handleChartMousePress(QMouseEvent *);
  void             handleChartMouseMove(QMouseEvent *);
  void             handleChartMouseRelease(QMouseEvent *);
  void             handleChartWheel(QWheelEvent *);
  /// Returns true when the key was used.
  bool             handleChartKey(QKeyEvent *);

  void             emitInfo();
  bool             computeMinMax(double);
  void             rebuildSeries();
  void             updateXAxisRange();
  void             updateSeriesAppearance();
  void             updateThresholdLinesVisibility();
  void             updateThresholdLinePositions();
  QString          formatEngineeringValue(double value, QString *unit = Q_NULLPTR) const;

private:
  Qt::PenStyle     penStyle(LineMode);
  Settings        *m_cfg;

};

