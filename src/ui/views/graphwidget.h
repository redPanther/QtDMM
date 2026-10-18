//======================================================================
// File:		dmmgraph.h
// Author:	Matthias Toussaint
// Created:	Tue Apr 10 17:43:46 CEST 2001
//----------------------------------------------------------------------
// This file is part of QtDMM.
//
// QtDMM is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// QtDMM is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with QtDMM.  If not, see <http://www.gnu.org/licenses/>.
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

class GraphFlagItem;
class GraphMinimap;
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
/// The store's state shows: in Live the window follows the newest reading
/// (the scroll bar rests), while recording "● REC 0:42 / 5:00" stands top
/// left in the plot, in View where the curve comes from (the start of the
/// recording or the file).
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

  /// Which horizontal line the mouse is dragging.
  enum CursorMode
  {
    NoCursor = 0,
    Trigger,      ///< recording start threshold
    Integration,  ///< integration threshold
    Reference,    ///< the reference line
    TimeCursorA,  ///< the time cursor set with a click
    TimeCursorB   ///< the one set with Shift+click
  };

  /// What the reference line stands at: 0 (the default), a value of its
  /// own (dragged there), the mean, minimum or maximum of the window shown
  /// - those follow the readings - or an alarm threshold; or no line.
  enum ReferenceKind
  {
    RefZero = 0,
    RefValue,
    RefMean,
    RefMin,
    RefMax,
    RefAlarm,
    RefOff
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
    IDColorVariant,  ///< property "variant" says which
    IDLive,
    IDReference,     ///< properties "kind" and "value"
    IDClearCursors,
    IDMinimap,
    IDEnvelope,
    IDValueShown     ///< property "value": the id of the meter's value
  };

  GraphWidget(QWidget *parent, Settings *settings);
  GraphWidget(QWidget *parent = nullptr);
  ~GraphWidget();
  /// The window from the settings, in seconds (also how wide Live is
  /// without a recording length). A window other than the last one ends
  /// "All"; the same one keeps it, and the window of "All" stays.
  void             setGraphSize(int size);
  /// The visible window in seconds: the settings' one, or that of "All".
  int              windowSeconds() const { return m_windowSeconds; }
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
  void             setSampleLength(int v);
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
                             const QColor &start,
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
  /// The smallest time step (..., 1, 2, 5, 10, 15, 30 s, 1, 2, 5, 10, 15,
  /// 30 min, 1, 2, 3, 6, 12 h, days) that is at least @p v seconds: the
  /// x division. Public for the tests.
  static double    timeStep(double v);
  /// Puts the reference line at @p kind; @p value for RefValue and
  /// RefAlarm (a value of 0 is RefZero).
  void             setReference(ReferenceKind kind, double value = 0);
  ReferenceKind    referenceKind() const { return m_referenceKind; }
  /// Where the line is now, in base units; NaN when the mean, minimum or
  /// maximum has no reading in the window.
  double           referenceValue() const { return m_reference; }
  /// The reference as kept per window (Windows/graph-reference, the
  /// workspace): "" for 0, "mean", "min", "max", "alarm <value>", "<value>",
  /// "off".
  QString          referenceSetting() const;
  void             setReferenceSetting(const QString &text);
  /// The alarms' thresholds (base units): the reference line snaps to them.
  /// A line on a threshold that went stays where it is.
  void             setAlarmThresholds(const QList<double> &values);
  /// @name Time cursors
  /// A click into the graph sets cursor A, Shift+click cursor B; both can
  /// be dragged, Esc and the context menu take them away. A alone says the
  /// time and the value there; with B the graph says the time and the
  /// change of the value between them, their mean and the integral (Ah for
  /// a current, Wh for a power).
  /// @{
  /// Puts cursor @p which (0 = A, 1 = B) at @p t, ms since the start of
  /// the recording; -1 takes it away.
  void             setTimeCursor(int which, qint64 t);
  qint64           timeCursor(int which) const { return m_cursorT[which & 1]; }
  void             clearTimeCursors();
  /// What the cursors say, as shown top right in the plot; empty without
  /// cursor. Public for the tests.
  QString          timeCursorText() const;
  /// @}
  /// @name The overview strip below the graph (GraphMinimap)
  /// All the store keeps, the window between two brackets; in place of
  /// the scroll bar while it is shown (context menu "Overview", kept per
  /// window as Windows/graph-minimap). Moved back from the newest reading
  /// in Live, the window stays where it is ("Live paused") until the
  /// strip, a time button or Live bring it back.
  /// @{
  void             setMinimapVisible(bool on);
  bool             minimapVisible() const { return m_minimapOn; }
  GraphMinimap    *minimap() const { return m_minimap; }
  /// @}
  /// How often the curves were built anew from the whole store (for the tests).
  int              rebuilds() const { return m_rebuilds; }
  /// @name The min/max band behind the curve
  /// Per pixel column the lowest and the highest value of the columns
  /// within kEnvelopeColumns either side: a band as wide on screen whatever
  /// the window, which shows the spread around the curve. Off by default
  /// (context menu "Min/max band", kept per window as
  /// Windows/graph-envelope).
  /// @{
  void             setEnvelopeVisible(bool on);
  bool             envelopeVisible() const { return m_envelopeOn; }
  /// The band's outline in scene coordinates, empty while off. For the tests.
  QPainterPath     envelopePath() const;
  static constexpr int kEnvelopeColumns = 4;
  /// @}
  /// @name The meter's other values
  /// A curve for each series of the store's other values
  /// (RecordingStore::extra(), a second display), in a colour of its own.
  /// A value in the main value's unit shares its axis; the first other unit
  /// gets an axis on the right with a scale of its own; values in a third
  /// unit are not drawn (yet). A legend names the curves. They are off until
  /// switched on (context menu "Values", kept per window as
  /// Windows/graph-shown-values): the graph shows the main value, as it
  /// always did. The store records them all the same.
  /// Reference line, band, overview, cursor statistics and triggers stay the
  /// main value's.
  /// @{
  int              extraCurveCount() const { return int(m_extraCurves.size()); }
  /// The first segment of the curve of extra(@p i) (for the tests).
  QLineSeries     *extraCurve(int i) const { return m_extraCurves[size_t(i)].series; }
  bool             extraCurveShown(int i) const { return m_extraCurves[size_t(i)].shown; }
  bool             extraCurveOnRight(int i) const { return m_extraCurves[size_t(i)].right; }
  QValueAxis      *rightAxis() const { return m_yAxis2; }
  /// Its title, "[°C]" (empty while it is hidden).
  QString          rightAxisTitle() const { return m_y2Title->isVisible() ? m_y2Title->text() : QString(); }
  /// The legend's text, empty while there is only the main value.
  QString          legendText() const;
  void             setValueShown(int id, bool shown);
  QList<int>       shownValues() const;
  /// @}
  /// @{
  /// Live, but the window held where the overview put it.
  bool             livePaused() const { return m_liveHold; }
  /// @}
  /// Line widths of the data and the integration curve.
  void             setLine(int d, int i);
  /// Draws a vertical mark at the current sample (an alarm raised); marks
  /// move with the data and go with clearSLOT().
  void             addMark(const QColor &color, const QString &name);
  int              markCount() const { return int(m_marks.size()); }
  /// Unsaved recorded data in memory.
  bool             dirty() const { return m_store->dirty(); }
  void             setAlertUnsaved(bool on) { m_alertUnsaved = on; }
  void             setCrosshair(bool on) { m_crosshair = on; }
  /// LineMode and PointMode for the data and the integration curve.
  void             setLineStyle(int, int, int, int);
  /// Integration curve: shown, scale factor, threshold (values at or below
  /// it reset the sum) and offset.
  void             setIntegration(bool, double, double, double);
  /// Which value of the meter the integration curve integrates: 0 the main
  /// value, 1 the second display (Graph/int-value). Without that value the
  /// curve is not drawn; it goes on the axis its value is on.
  void             setIntegrationValue(int id);
  int              integrationValue() const { return m_intValueId; }
  void             setSettings(Settings *settings) { m_cfg = settings; }
  /// Since 26.2 the integral is one over time (unit x s), no longer the sum
  /// of the samples. Once per settings file Graph/int-scale is divided by
  /// the sample time in seconds, so the curve stays as it was;
  /// Graph/int-scale-per-second marks it done. True when the scale changed.
  static bool      migrateIntegralScale(Settings *cfg);

Q_SIGNALS:
  /// Status bar text: sample time, window and remaining length.
  void             info(const QString &);
  void             error(const QString &);
  /// Recording started/stopped.
  void             running(bool);
  /// The store's state changed (RecordingStore::State).
  void             stateChanged(int state);
  /// Window/total size changed by zooming (seconds).
  /// Sample time changed by a CSV import (tenths of a second).
  void             sampleTime(int);
  /// The context menu chose this graph's colours: a ColorVariant, or -1
  /// for the default from the settings page.
  void             colorVariantChanged(int override);
  void             zoomIn(double);
  void             zoomOut(double);
  /// Show the whole recording (key 0).
  /// A time button asks for this visible window (seconds).
  void             windowRequested(int seconds);
  /// A threshold line was dragged with the mouse.
  void             thresholdChanged(GraphWidget::CursorMode, double);
  /// The reference line was dragged or chosen in the context menu.
  void             referenceChanged();
  /// @name Context menu requests, handled by InstanceWidget
  /// @{
  void             connectDMM(bool);
  void             configure();
  /// "Clear graph" in the context menu: the window asks about unsaved data
  /// first (InstanceWidget::clearSLOT()).
  void             clearRequested();
  /// "Start recording" (true) or "Stop recording" in the context menu: the
  /// window starts it as its Record button does (MainWindow::startSLOT()).
  void             recordRequested(bool start);
  /// "Live" in the context menu (InstanceWidget::liveSLOT()).
  void             liveRequested();
  void             exportData();
  void             importData();
  /// @}

public Q_SLOTS:
  /// Discards the recorded data.
  void             clearSLOT();
  /// @name Keyboard zoom/pan, also reachable from MainWindow's shortcuts
  /// @{
  void             zoomInSLOT()  { leaveAll(); Q_EMIT zoomIn(1.25); }
  void             zoomOutSLOT() { leaveAll(); Q_EMIT zoomOut(1.25); }
  /// The window as long as what there is (kept in the settings, unlike "All").
  void             zoomFitSLOT() { m_followAll = false; Q_EMIT windowRequested(allTarget(false)); }
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
  /// The store goes Live (RecordingStore::live()), the curve viewed goes.
  void             liveSLOT();
  /// Export with a file dialog; returns false when cancelled or failed.
  bool             exportDataSLOT();
  void             importDataSLOT();
  void             connectSLOT(bool on) { m_connected = on; }

  /// File-path-driven, non-interactive halves of export/importDataSLOT (no QFileDialog),
  /// split out so the CSV parsing/writing logic can be exercised from tests.
  bool             exportCsvFile(const QString &fileName, bool raw = false);
  bool             importCsvFile(const QString &fileName);
  /// The text top left in the plot: "● REC 0:42 / 5:00", the source of what
  /// is viewed, or empty (Live). Public for the tests.
  QString          stateText() const;
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
  GraphMinimap    *m_minimap = nullptr;
  bool             m_minimapOn = true;
  bool             m_envelopeOn = false;
  QGraphicsPathItem *m_envelope = nullptr;
  /// The curve's points (pointsOf(), gaps as NaN) for the band
  QList<QPointF>   m_curvePts;
  void             updateEnvelope();
  bool             m_liveHold = false;   ///< Live, the window held at m_holdStart
  qint64           m_holdStart = 0;      ///< ms since the start of the recording
  /// The chart, and below it the overview or the scroll bar.
  void             layoutParts();
  /// The overview asks for the window to start at @p start.
  void             showFrom(qint64 start);
  /// The window follows the newest reading again (Live), or goes to the end.
  void             followEnd();
  /// The overview asks for a window of @p width ms from @p start.
  void             showSpan(qint64 start, qint64 width, bool atEnd);
  /// Tells the overview the window shown.
  void             updateMinimapView();
  qint64           m_bucket = 1;    ///< ms per drawn min/max pair (see bucketSize())
  int              m_tailData = 0;  ///< points the newest bucket gave the data line (1 or 2; a gap is 1)
  int              m_tailInt = 0;   ///< the same for the integral
  int              m_tailDataPts = 0;  ///< and the data points (a gap has none)
  int              m_tailIntPts = 0;
  int              m_windowSeconds = 600;   ///< the visible window, applyWindow()
  int              m_savedWindow = 0;       ///< the settings' window, setGraphSize(); 0 = none yet
  int              m_rebuilds = 0;          ///< rebuildSeries() calls, see onAppended()
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
  /// "All": a window that holds the recording so far (plus room to grow
  /// when @p grow), at least 10 s and at most the recording length.
  int              allTarget(bool grow) const;
  /// "All": shows allTarget() - in the graph only, the settings keep
  /// their window (it would be saved with them).
  void             requestAll(bool grow);
  /// Shows a window of @p seconds (setGraphSize(), requestAll()).
  void             applyWindow(int seconds);
  /// A zoom from "All" starts from the window shown: it goes to the
  /// settings first.
  void             leaveAll();
  void             updateTimeButtons();
  /// The longest window worth a time button, in seconds (0 = none).
  int              lengthLimit() const;
  /// The scroll bar over what the store keeps.
  void             updateScrollRange();
  void             placeTimeBar();
  /// @}
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
  static QList<QPointF> withoutGaps(const QList<QPointF> &points);
  static int       finiteTail(const QList<QPointF> &points, int tail);
  void             appendToSeries();
  void             appendIntegral(const RecordingSeries &series, int first, int last, bool filling);
  static int       replaceTail(QScatterSeries *series, int remove, const QList<QPointF> &points);
  void             onAppended(bool shifted);
  void             onCleared();
  void             onStateChanged();
  /// @name The state top left in the plot (stateText())
  /// @{
  QLabel          *m_stateLabel = nullptr;
  QColor           m_stateColor;          ///< the lettering colour, for View
  QString          m_sourceFile;          ///< the file viewed (importCsvFile()), empty = a recording
  void             updateStateLabel();
  /// @}
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
  /// A curve of one of the store's other series; kept for reuse when there
  /// are fewer.
  struct ExtraCurve
  {
    QLineSeries *series = nullptr;   ///< the first segment
    std::unique_ptr<GapLine> line;
    int  tail = 0;                   ///< points the newest bucket gave (see m_tailData)
    bool right = false;              ///< on the right axis
    bool shown = false;              ///< drawn: an axis has its unit, not hidden
  };
  std::vector<ExtraCurve> m_extraCurves;
  QValueAxis      *m_yAxis2 = nullptr;   ///< the right axis: the first unit other than the main one
  QString          m_unit2;              ///< its unit; empty while it is hidden
  QGraphicsSimpleTextItem *m_y2Title = nullptr;   ///< its title above it, as m_yTitle
  double           m_scale2Min = 1e40;
  double           m_scale2Max = -1e40;
  QSet<int>        m_shownValues;        ///< the meter's other values switched on
  int              m_intValueId = 0;     ///< setIntegrationValue()
  QLabel          *m_legend = nullptr;
  QColor           extraColor(int i) const;
  void             rebuildExtras();
  void             onExtraAppended(int i);
  void             rebuildOtherIntegral();
  /// The integration curve (line and points) on @p axis.
  void             integralOnAxis(QAbstractAxis *axis);
  void             updateLegend();
  /// The right axis from m_scale2Min/Max, with the left axis' tick count.
  void             updateRightAxis();
  /// The drawn points of readings first..last of @p series, thinned per bucket
  /// (see the comment at the definition); returns the number of points.
  int              pointsOf(const RecordingSeries &series, int first, int last, bool integral, QList<QPointF> &out) const;
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
  /// Phosphor: 5 fine ticks per division on the reference line (else the
  /// bottom edge) and on the trigger's mark (else the left edge).
  QGraphicsPathItem *m_centreTicks;
  void             updateCentreTicks();
  /// @name The reference line
  /// Across the plot at the reference value (0) while that lies inside the
  /// y range, with the value small at its right end ("0 A"): a current
  /// that charges and discharges reads against it; without the value in
  /// range there is no line. Phosphor draws it with its fine ticks
  /// (updateCentreTicks()).
  /// @{
  QGraphicsLineItem       *m_referenceLine = nullptr;
  QGraphicsSimpleTextItem *m_referenceLabel = nullptr;
  double           m_reference = 0;
  ReferenceKind    m_referenceKind = RefZero;
  QList<double>    m_alarmThresholds;
  /// The statistics of the window shown, for RefMean, RefMin, RefMax.
  RecordingStore::Stats windowStats() const;
  /// The line dragged to @p y in the scene: it snaps to 0, the window's
  /// mean, minimum and maximum and the alarm thresholds within a few
  /// pixels, else takes the value there, rounded to a hundredth of the
  /// scale.
  void             dragReference(double y);
  /// The reference line's y in the scene, NaN when the value is not
  /// inside the y range (on its edge the axis is the line).
  double           referenceY() const;
  void             updateReferenceLine();
  /// @}
  /// @name Time cursors (setTimeCursor())
  /// @{
  qint64           m_cursorT[2] = { -1, -1 };      ///< ms since the start, -1 = none
  QGraphicsLineItem *m_cursorLine[2] = {};
  GraphFlagItem    *m_cursorName[2] = {};   ///< "A" and "B" as flags under the lines
  /// across the plot at the value under each cursor: the change is the
  /// distance between the two
  QGraphicsLineItem *m_cursorLevel[2] = {};
  QLabel          *m_cursorInfo = nullptr;          ///< timeCursorText(), top right in the plot
  QPoint           m_pressPos;                      ///< where the left button went down
  bool             m_clickCandidate = false;        ///< a press that may become a click: sets a cursor
  bool             m_pressShift = false;
  /// Places the lines and the text anew.
  void             updateTimeCursors();
  /// The reading's value that holds at @p t; NaN in a gap or outside.
  double           valueAt(qint64 t) const;
  /// @}
  QColor           m_defaultAxisLine;   ///< the chart's own axis line colour
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
  /// @name y labels with the SI prefix of the reading
  /// The same for the y axis: Qt's labels can only print base units
  /// (0.35 V), these print them with the prefix the meter shows (350 mV).
  /// @{
  QList<QGraphicsSimpleTextItem *> m_yLabels;
  QString          m_yPrefix;           ///< the prefix the y labels use now
  /// The meter's prefix while it measures what the store holds; else the
  /// one that suits the axis range.
  QString          yPrefix() const;
  QString          m_livePrefix;        ///< the prefix of the meter's unit (setUnit()), for an axis without readings
  QString          m_liveBase;          ///< and its base unit
  void             updateYLabels();
  /// @}
  QGraphicsLineItem *m_crosshairVLine;
  QGraphicsLineItem *m_crosshairHLine;
  QGraphicsLineItem *m_triggerLine;
  QGraphicsLineItem *m_integrationLine;
  /// Alarm marks: a vertical line at the sample the alarm raised on, one
  /// per RecordingStore::marks() in the same order.
  QList<QGraphicsLineItem *> m_marks;
  QList<GraphFlagItem *> m_markFlags;   ///< number and name under each mark
  /// The flags under the plot, the cursors' and the marks', in one row.
  void             updateFlags();
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
  void             rebuildSeries();
  /// Whether the points of the curve and of the integral are drawn. Hidden
  /// ones get no points: Qt Charts lays out a scatter series anew with every
  /// change, also a hidden one (that was most of the time of a reading).
  bool             pointsShown() const { return m_pointMode != NoPoint; }
  bool             intPointsShown() const { return m_showIntegration && m_intPointMode != NoPoint; }
  void             updateXAxisRange();
  void             updateSeriesAppearance();
  void             updateThresholdLinesVisibility();
  void             updateThresholdLinePositions();
  QString          formatEngineeringValue(double value, QString *unit = nullptr) const;

private:
  Qt::PenStyle     penStyle(LineMode);
  Settings        *m_cfg;

};

