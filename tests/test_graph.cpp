// Baseline behavior tests for GraphWidget, written before the planned QtGraph-based
// rewrite so the current CSV import/export contract has a regression net to
// compare the replacement against.
#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QTextStream>
#include <QDebug>
#include <QFileInfo>

#include "ui/views/graphwidget.h"
#include "recording/recordingfile.h"
#include "recording/recordingstore.h"
#include <QChartView>
#include <QValueAxis>
#include <QGraphicsSimpleTextItem>
#include <QXYSeries>
#include <QScrollBar>
#include <QToolButton>
#include "core/siprefix.h"
#include "ui/engnumbervalidator.h"
#include "core/settings.h"

static int failed = 0;

static void fail(const QString &what)
{
  qWarning() << "FAILED:" << what;
  failed++;
}

static void check(bool cond, const QString &what)
{
  if (!cond)
    fail(what);
}

// Reads a whole file as text (used to compare export output byte-for-byte).
// Text mode mirrors the exporter, which writes "\r\n" on Windows.
static QString readFile(const QString &fileName)
{
  QFile f(fileName);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return QString();
  return QString::fromUtf8(f.readAll());
}

// The readings a MeterController gives a store, on a clock of the test's
// own: value() is a main reading now, then the clock moves on by a step
// (0.1 s by default). NaN is an overload.
struct Feed
{
  RecordingStore *store;
  qint64 now = 0;
  qint64 step;

  explicit Feed(GraphWidget &graph, qint64 stepMs = 100) : Feed(graph.store(), stepMs) {}
  explicit Feed(RecordingStore *s, qint64 stepMs = 100) : store(s), step(stepMs)
  {
    store->setClock([this] { return now; }, [this] { return wall(now); });
  }
  Feed(const Feed &) = delete;
  Feed &operator=(const Feed &) = delete;

  static QDateTime wall(qint64 t) { return QDateTime(QDate(2026, 10, 4), QTime(12, 0, 0)).addMSecs(t); }
  void value(double v)
  {
    Reading r;
    r.overload = std::isnan(v);
    r.value = v;
    r.text = r.overload ? QStringLiteral("OL") : QString::number(v);
    r.t = now;
    r.msecs = wall(now).toMSecsSinceEpoch();
    store->setReading(r);
    now += step;
  }
};

int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");

  QApplication app(argc, argv);

  if (argc != 2)
  {
    qCritical() << "Usage: test_graph <data-dir>";
    return 1;
  }

  QString dataDir = QString::fromLocal8Bit(argv[1]);
  QTemporaryDir tmpDir;
  check(tmpDir.isValid(), "could not create temp dir for test settings");

  Settings settings("test_graph", tmpDir.path());

  // --- 1. all known fixture formats (legacy tab-separated, legacy with nan,
  //         new semicolon CSV, CSV with comma-decimal ms, larger recording)
  //         must import without error ---
  const QStringList fixtures = {
    "legacy.txt", "legacy_nan.txt", "new_dpoint.csv", "new_komma_ms.csv", "new_larger.csv"
  };

  for (const QString &name : fixtures)
  {
    GraphWidget graph(nullptr, &settings);
    QString path = dataDir + "/" + name;
    bool ok = graph.importCsvFile(path);
    check(ok, QString("importCsvFile() failed for fixture '%1'").arg(name));
  }

  // --- 1b. an import keeps all of the file and leaves the window as it
  //          was; the time buttons go up to the file's length ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.setGraphSize(60);
    check(graph.importCsvFile(dataDir + "/new_larger.csv"), "import size: import failed");
    const int count = graph.store()->count();
    const double seconds = (count - 1) * graph.store()->sampleTime() / 10.0;
    const double limit = graph.store()->lengthLimit() / 1000.0;
    check(count > 1 && graph.store()->origin() == 0 && limit >= seconds && limit < seconds + 1,
          QString("import size: %1 points, limit %2 s for %3 s").arg(count).arg(limit).arg(seconds));
    check(graph.store()->state() == RecordingStore::View, "import size: viewed");
  }

  // --- 2. malformed input must be rejected, not crash or half-import ---
  {
    QTemporaryDir badDir;
    QString badFile = badDir.path() + "/broken.csv";
    QFile f(badFile);
    check(f.open(QIODevice::WriteOnly), "temp file opens for writing");
    QTextStream(&f) << "this is not a valid QtDMM export\n";
    f.close();

    GraphWidget graph(nullptr, &settings);
    bool ok = graph.importCsvFile(badFile);
    check(!ok, "importCsvFile() should reject a non-matching file, but reported success");
  }

  // --- 3. import -> export -> re-import must round-trip to the same CSV
  //         (this is the contract the QtGraph replacement needs to preserve) ---
  {
    GraphWidget graph(nullptr, &settings);
    check(graph.importCsvFile(dataDir + "/new_larger.csv"), "round-trip: initial import failed");

    QTemporaryDir outDir;
    QString exported1 = outDir.path() + "/export1.csv";
    check(graph.exportCsvFile(exported1), "round-trip: first export failed");

    GraphWidget graph2(nullptr, &settings);
    check(graph2.importCsvFile(exported1), "round-trip: re-import of exported file failed");

    QString exported2 = outDir.path() + "/export2.csv";
    check(graph2.exportCsvFile(exported2), "round-trip: second export failed");

    QString c1 = readFile(exported1);
    QString c2 = readFile(exported2);
    check(!c1.isEmpty() && c1 == c2,
          "round-trip: re-exporting a re-imported file produced different CSV content");
  }

  // --- 3b. time buttons (All / 1 min / 5 min / 30 min) top right ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.setSampleTime(10);   // one sample per second
    graph.setGraphSize(300);
    const QList<QToolButton *> buttons = graph.findChildren<QToolButton *>();
    auto button = [&](const QString &text) -> QToolButton *
    {
      for (QToolButton *b : buttons)
        if (b->text() == text)
          return b;
      return nullptr;
    };
    QToolButton *all = button("All"), *one = button("1 min"), *five = button("5 min"), *thirty = button("30 min");
    check(all && one && five && thirty, "time buttons: All, 1 min, 5 min, 30 min exist");
    if (all && one && five && thirty)
    {
      check(five->isChecked() && !one->isChecked() && !all->isChecked(), "time buttons: a 300 s window marks 5 min");
      QSignalSpy spy(&graph, &GraphWidget::windowRequested);
      one->click();
      check(spy.size() == 1 && spy.last().at(0).toInt() == 60, "time buttons: 1 min asks for a 60 s window");
      // InstanceWidget applies the request through the settings, like a zoom
      graph.setGraphSize(60);
      check(one->isChecked() && !five->isChecked(), "time buttons: the applied window is marked");
      graph.setGraphSize(45);
      check(!one->isChecked() && !five->isChecked() && !thirty->isChecked(), "time buttons: a zoomed window marks none");
      graph.setGraphSize(60);
      graph.setSampleLength(6000);   // a recording of 10 min, waited for in Live
      graph.liveSLOT();
      check(thirty->isHidden() && !five->isHidden(), "time buttons: 30 min is hidden for a 10 min recording");
      graph.setSampleLength(0);
      check(!thirty->isHidden(), "time buttons: 30 min is back without a length");

      // All: the recording so far (at least 10 s), growing with it
      spy.clear();
      graph.setGraphSize(10);
      all->click();
      check(all->isChecked(), "time buttons: All stays marked");
      Feed feed(graph);
      graph.startSLOT();
      for (int i = 0; i < 150; ++i)   // 15 s
        feed.value(1.0);
      bool grew = !spy.isEmpty();
      for (const QList<QVariant> &args : spy)
        grew = grew && args.at(0).toInt() > 10 && args.at(0).toInt() <= 600;
      check(grew, QString("time buttons: All grows the window with the recording (%1 requests)").arg(spy.size()));
      graph.zoomInSLOT();
      graph.setGraphSize(8);
      check(!all->isChecked(), "time buttons: zooming ends All");
    }
  }

  // --- 3c. All on a longer recording: from one minute on it asks for whole
  //         minutes (the settings keep seconds only up to 99999; an odd
  //         value above was cut and asked for again with every sample) ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.setSampleTime(10);   // one sample per second
    graph.setGraphSize(10);
    QToolButton *all = nullptr;
    for (QToolButton *b : graph.findChildren<QToolButton *>())
      if (b->text() == "All")
        all = b;
    check(all, "All on a long recording: button exists");
    if (all)
    {
      all->click();
      QList<int> requests;
      // InstanceWidget applies each request through the settings
      QObject::connect(&graph, &GraphWidget::windowRequested, &graph, [&](int seconds)
      {
        requests << seconds;
        graph.setGraphSize(seconds);
      });
      Feed feed(graph);
      graph.startSLOT();
      for (int i = 0; i < 3000; ++i)   // 300 s
        feed.value(1.0);
      bool minutes = !requests.isEmpty();
      for (int seconds : requests)
        minutes = minutes && (seconds <= 60 || seconds % 60 == 0);
      check(minutes, QString("All on a long recording: whole minutes above 60 s (%1)").arg(
              [&] { QStringList l; for (int r : requests) l << QString::number(r); return l.join(' '); }()));
      check(requests.size() < 20, QString("All on a long recording: grows in steps, not per sample (%1 requests)").arg(requests.size()));
    }
  }

  // --- 4. regression test for the CSV-import sample-time bug (dmmgraph.cpp):
  //         m_sampleTime used to be computed by summing a growing offset on
  //         every row instead of once after the loop, inflating it with row
  //         count. For new_larger.csv (~271 rows over ~54s) the correct
  //         sample interval is on the order of the actual ~0.2s spacing,
  //         not something that grows with the number of rows. ---
  {
    GraphWidget graph(nullptr, &settings);
    QSignalSpy spy(&graph, &GraphWidget::sampleTime);
    check(graph.importCsvFile(dataDir + "/new_larger.csv"), "sampleTime regression: import failed");
    check(spy.count() == 1, "sampleTime regression: expected exactly one sampleTime() signal");
    if (spy.count() == 1)
    {
      int sampleTime = spy.at(0).at(0).toInt();
      // buggy version summed ~271 growing offsets -> computed a wildly larger
      // value than the true ~54s / 271 rows interval; a healthy value stays small.
      check(sampleTime == 2,
            QString("sampleTime regression: got sampleTime %1 for a 54s/272-row recording, expected 2 (0.2 s)").arg(sampleTime));
    }
  }

  // --- 4b. slower recordings: the sample time must follow the timestamps,
  //         not collapse to 0.1 s (a 5 min / 0.5 s recording used to import
  //         as one minute) ---
  {
    QTemporaryDir dir;
    const QString slow = dir.path() + "/slow.csv";
    QFile f(slow);
    check(f.open(QIODevice::WriteOnly | QIODevice::Text), "slow: create fixture");
    QTextStream out(&f);
    out << "timestamp;time (s);value;unit\n";
    QDateTime t0(QDate(2026, 9, 20), QTime(14, 2, 17, 385));
    for (int i = 0; i < 600; ++i)
      out << t0.addMSecs(i * 500).toString("yyyy-MM-ddTHH:mm:ss,zzz") << ";" << i * 0.5 << ";12.0;V\n";
    f.close();

    GraphWidget graph(nullptr, &settings);
    QSignalSpy spy(&graph, &GraphWidget::sampleTime);
    check(graph.importCsvFile(slow), "slow: import failed");
    check(spy.count() == 1 && spy.at(0).at(0).toInt() == 5,
          QString("slow: sample time %1, expected 5 (0.5 s)").arg(spy.count() ? spy.at(0).at(0).toInt() : -1));

    // and it exports with the original spacing
    const QString back = dir.path() + "/back.csv";
    check(graph.exportCsvFile(back), "slow: export failed");
    const QStringList lines = readFile(back).split('\n', Qt::SkipEmptyParts);
    check(lines.size() == 601 && lines.last().startsWith("2026-09-20T14:07:16,885"),
          QString("slow: exported %1 lines, last '%2'").arg(lines.size()).arg(lines.value(lines.size() - 1).left(23)));
  }

  // --- 5. smoke test for the live recording against the Qt Charts series
  //         sync (rebuildSeries()/append()): must survive the store
  //         dropping its oldest readings without crashing, and
  //         setGraphSize() must be callable again afterwards while data
  //         already exists. ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.setSampleTime(1);
    graph.setGraphSize(5);
    graph.store()->setMaxPoints(10);   // drops quickly
    graph.setMode(GraphWidget::Manual);
    Feed feed(graph, 500);
    graph.startSLOT();

    for (int i = 0; i < 20; i++)
      feed.value(i * 0.1);

    check(graph.dirty(), "ring-buffer smoke test: expected graph to be marked dirty after recording");
    check(graph.store()->origin() > 0, "ring-buffer smoke test: the store must have dropped old readings");

    graph.setGraphSize(10);
    feed.value(1.23);
  }

  // --- 5a. integration: over time (unit x s) of the readings above the
  //          threshold, 0 at or below it - the first reading, too (it used
  //          to be the threshold itself, a spike at the left edge). A value
  //          counts for the time it held, so 0.8 adds from the 0.9 on; with
  //          the scale 10 a reading of 0.1 s adds its value ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.setSampleTime(1);
    graph.setGraphSize(100);
    graph.setIntegration(true, 10.0, 0.5, 0.0);
    graph.setMode(GraphWidget::Manual);
    Feed feed(graph);
    graph.startSLOT();
    for (double v : { 0.1, 0.2, 0.8, 0.9, 0.1, 0.6 })
      feed.value(v);
    const QList<QAbstractSeries *> series = graph.findChild<QChartView *>()->chart()->series();
    auto *integral = series.size() > 2 ? qobject_cast<QXYSeries *>(series[2]) : nullptr;   // data line, data points, integration
    QStringList got;
    if (integral)
      for (const QPointF &p : integral->points())
        got << QString::number(p.y());
    check(got.join(' ') == "0 0 0 0.8 0 0",
          QString("integration: expected '0 0 0 0.8 0 0', got '%1'").arg(got.join(' ')));
  }

  // --- 5b. window size and sample time in either order: InstanceWidget used to set
  //          the window (counted in samples) before the sample time, and any
  //          later x axis update showed it scaled by the ratio of the two ---
  {
    Settings cfg("ordertest", tmpDir.path());
    GraphWidget graph(nullptr, &cfg);
    graph.resize(800, 500);
    auto *x = qobject_cast<QValueAxis *>(graph.findChild<QChartView *>()->chart()->axes(Qt::Horizontal).first());
    graph.setGraphSize(600);   // with the default sample time of 0.1 s
    graph.setSampleTime(10);         // then 1 s
    // the error showed on the next x axis update, e.g. scrolling
    auto *bar = graph.findChild<QScrollBar *>(QString(), Qt::FindDirectChildrenOnly);   // not the chart view's
    bar->setValue(1);
    bar->setValue(0);
    check(qAbs(x->max() - x->min() - 599) < 1.5,
          QString("window stays 600 s after a new sample time, got %1 s").arg(x->max() - x->min()));
    graph.setSampleTime(10);         // unchanged: nothing to do
    graph.setGraphSize(300);
    check(qAbs(x->max() - x->min() - 299) < 1.5, QString("300 s window, got %1 s").arg(x->max() - x->min()));
  }

  // --- 5c-5h: the recorder behind the graph, through its public API ---

  // y values of series @p index (0 data line, 2 integration) as "1 4 8"
  auto seriesY = [](GraphWidget &graph, int index) -> QString
  {
    const QList<QAbstractSeries *> series = graph.findChild<QChartView *>()->chart()->series();
    auto *s = series.size() > index ? qobject_cast<QXYSeries *>(series[index]) : nullptr;
    QStringList got;
    if (s)
      for (const QPointF &p : s->points())
        got << QString::number(p.y());
    return got.join(' ');
  };
  auto seriesX = [](GraphWidget &graph) -> QString
  {
    auto *s = qobject_cast<QXYSeries *>(graph.findChild<QChartView *>()->chart()->series().value(0));
    QStringList got;
    if (s)
      for (const QPointF &p : s->points())
        got << QString::number(p.x());
    return got.join(' ');
  };

  // --- 5c. every reading is drawn, at its time; the sample time is the
  //          grid of the export only ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.setSampleTime(2);          // a grid of 0.2 s
    graph.setGraphSize(100);
    graph.setMode(GraphWidget::Manual);
    Feed feed(graph);
    graph.startSLOT();
    for (double v : { 1.0, 3.0, 5.0, 7.0, 9.0 })
      feed.value(v);
    check(seriesY(graph, 0) == "1 3 5 7 9",
          QString("every reading: expected '1 3 5 7 9', got '%1'").arg(seriesY(graph, 0)));
    check(seriesX(graph) == "0 0.1 0.2 0.3 0.4",
          QString("every reading: expected x '0 0.1 0.2 0.3 0.4', got '%1'").arg(seriesX(graph)));
    // not recording: readings pass by
    graph.stopSLOT();
    feed.value(100);
    feed.value(100);
    check(seriesY(graph, 0) == "1 3 5 7 9", "every reading: stopped recorder must not store");
    // the export: the start value, then the mean of each 0.2 s
    const QVector<double> grid = graph.store()->toRecording().values;
    QStringList g;
    for (double v : grid)
      g << QString::number(v);
    check(g.join(' ') == "1 2 6", "every reading: grid export got " + g.join(' '));
  }

  // --- 5d. start trigger, rising: starts on the reading that crosses the
  //          threshold from below and records it as the first sample; the
  //          very first reading (nothing to compare with) never triggers ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.setSampleTime(1);
    graph.setGraphSize(100);
    graph.setThresholds(0.0, 1.0);
    graph.setMode(GraphWidget::Raising);
    QSignalSpy running(&graph, &GraphWidget::running);
    Feed feed(graph);
    graph.liveSLOT();            // the triggers wait in Live
    feed.value(1.5);             // first reading, above: no start
    feed.value(0.5);
    feed.value(0.8);
    check(running.isEmpty(), "rising trigger: started before the crossing");
    feed.value(1.0);             // crosses (>=)
    check(running.size() == 1 && running.first().first().toBool(), "rising trigger: did not start on the crossing");
    feed.value(2.0);
    check(seriesY(graph, 0) == "1 2", QString("rising trigger: expected '1 2', got '%1'").arg(seriesY(graph, 0)));
  }

  // --- 5e. start trigger, falling ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.setSampleTime(1);
    graph.setGraphSize(100);
    graph.setThresholds(-1.0, 5.0);
    graph.setMode(GraphWidget::Falling);
    QSignalSpy running(&graph, &GraphWidget::running);
    Feed feed(graph);
    graph.liveSLOT();            // the triggers wait in Live
    feed.value(-2.0);            // first reading, below: no start
    feed.value(0.0);
    feed.value(-0.5);
    check(running.isEmpty(), "falling trigger: started before the crossing");
    feed.value(-1.0);            // crosses (<=)
    feed.value(-3.0);
    check(running.size() == 1, "falling trigger: did not start on the crossing");
    check(seriesY(graph, 0) == "-1 -3", QString("falling trigger: expected '-1 -3', got '%1'").arg(seriesY(graph, 0)));
  }

  // --- 5f. start trigger, clock time: starts within two seconds after the
  //          start time (the store's clock looks every second), not before ---
  {
    GraphWidget later(nullptr, &settings);
    later.setGraphSize(100);
    Feed laterFeed(later);
    later.setStartTime(Feed::wall(0).time().addSecs(120));
    later.setMode(GraphWidget::Time);
    later.liveSLOT();
    QSignalSpy notYet(&later, &GraphWidget::running);
    later.store()->poll();
    check(notYet.isEmpty(), "time trigger: started before the start time");

    GraphWidget now(nullptr, &settings);
    now.setGraphSize(100);
    Feed nowFeed(now);
    now.setStartTime(Feed::wall(0).time());
    now.setMode(GraphWidget::Time);
    now.liveSLOT();
    QSignalSpy started(&now, &GraphWidget::running);
    nowFeed.now = 1000;
    now.store()->poll();
    check(started.size() == 1, "time trigger: did not start at the start time");
  }

  // --- 5g. integral and marks when the store is full: the window moves on
  //          with what the store keeps, the integral goes on summing, a
  //          mark falls off with the readings around it ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.setSampleTime(1);
    graph.setGraphSize(1);
    graph.store()->setMaxPoints(10);  // keeps 1 s
    graph.setIntegration(true, 10.0, 0.0, 0.0);
    graph.setMode(GraphWidget::Manual);
    Feed feed(graph);
    graph.startSLOT();
    for (int i = 1; i <= 5; i++)
      feed.value(i);
    feed.now -= 100;                 // the mark comes with the reading of 5
    graph.addMark(Qt::red, "m");
    feed.now += 100;
    check(graph.markCount() == 1, "marks: addMark() did not add");
    for (int i = 6; i <= 15; i++)
      feed.value(i);
    auto *x = qobject_cast<QValueAxis *>(graph.findChild<QChartView *>()->chart()->axes(Qt::Horizontal).first());
    // as of the newest reading; the clock has moved on by one step since
    check(graph.store()->origin() > 0 && qAbs(x->min() - graph.store()->origin() / 1000.0) <= 0.1 + 1e-9,
          QString("full: the window starts at %1 s, the store keeps from %2 ms").arg(x->min()).arg(graph.store()->origin()));
    check(seriesY(graph, 0).endsWith("12 13 14 15"), "full: the newest are drawn, got " + seriesY(graph, 0));
    // the integral: 1 + ... + 14, each held 0.1 s, scaled by 10
    check(seriesY(graph, 2).endsWith(" 105"), QString("full: integral got '%1'").arg(seriesY(graph, 2)));
    for (int i = 16; i <= 30; i++)
      feed.value(i);                 // the mark's readings leave the store
    check(graph.markCount() == 0, "marks: still there after its readings left the store");
    graph.addMark(Qt::red, "m2");
    graph.clearSLOT();
    check(graph.markCount() == 0, "marks: clearSLOT() kept a mark");
  }

  // --- 5h. recording length: stops on its own after setSampleLength()
  //          (tenths of a second); 0 records until stopped ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.setSampleTime(1);
    graph.setGraphSize(100);
    graph.setSampleLength(5);
    graph.setMode(GraphWidget::Manual);
    QSignalSpy running(&graph, &GraphWidget::running);
    Feed feed(graph);
    graph.startSLOT();
    for (int i = 1; i <= 8; i++)
      feed.value(i);
    check(running.size() == 2 && !running.last().first().toBool(),
          QString("length: expected start and stop, got %1 signals").arg(running.size()));
    check(seriesY(graph, 0) == "1 2 3 4 5",
          QString("length: expected '1 2 3 4 5', got '%1'").arg(seriesY(graph, 0)));

    graph.setSampleLength(0);
    graph.startSLOT();
    for (int i = 1; i <= 50; i++)
      feed.value(i);
    check(running.size() == 3, "length 0: must record until stopped");
  }

  // --- 5j. a store from outside (the MeterController's recorder): the graph
  //          shows it, takes what it already holds, and follows its signals ---
  {
    RecordingStore store;
    Feed feed(&store);
    store.setStartMode(RecordingStore::Manual);
    store.start();
    feed.value(1);
    feed.value(2);

    GraphWidget graph(nullptr, &settings);
    graph.setStore(&store);
    check(graph.store() == &store, "setStore: store() must return the new store");
    check(seriesY(graph, 0) == "1 2",
          QString("setStore: expected the store's '1 2', got '%1'").arg(seriesY(graph, 0)));
    feed.value(3);
    check(seriesY(graph, 0) == "1 2 3",
          QString("setStore: expected '1 2 3' after a new reading, got '%1'").arg(seriesY(graph, 0)));
    graph.stopSLOT();
    check(!store.isRunning(), "setStore: the graph's stop must reach the shared store");
    store.clear();
    check(seriesY(graph, 0).isEmpty(), "setStore: a cleared store must empty the graph");
  }

  // --- 5k. thinning: more readings in the window than pixel columns are
  //          drawn as a minimum and a maximum per column (a spike survives),
  //          the store keeps every reading, and growing the series reading
  //          by reading gives what a rebuild gives ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.resize(300, 200);
    graph.setSampleTime(1);
    graph.setGraphSize(200);            // 2000 readings in the window
    graph.setMode(GraphWidget::Manual);
    Feed feed(graph);
    graph.startSLOT();
    const int samples = 2000;
    for (int i = 0; i < samples; i++)
      feed.value(i == 777 ? 1000.0 : i == 1234 ? -500.0 : (i * 37) % 101);

    QChart *chart = graph.findChild<QChartView *>()->chart();
    const double plotWidth = chart->plotArea().width();
    const int columns = int(plotWidth > 0 ? plotWidth : graph.width());
    auto *series = qobject_cast<QXYSeries *>(chart->series().value(0));
    const int points = series ? series->count() : -1;
    check(points > 0 && points <= 2 * columns,
          QString("thinning: expected at most %1 points, got %2").arg(2 * columns).arg(points));
    check(graph.store()->count() == samples,
          QString("thinning: the store must keep all %1 samples, has %2").arg(samples).arg(graph.store()->count()));
    // the grid of 0.1 s: one step for each reading, and the end of the last
    check(int(graph.store()->toRecording().values.size()) == samples + 1, "thinning: the export must have every step");

    double lo = 1e9, hi = -1e9;
    if (series)
      for (const QPointF &p : series->points())
      {
        lo = qMin(lo, p.y());
        hi = qMax(hi, p.y());
      }
    check(hi == 1000.0 && lo == -500.0, QString("thinning: spike and dip must be drawn, got %1 .. %2").arg(lo).arg(hi));

    const QString grown = seriesY(graph, 0) + "|" + seriesY(graph, 2);
    graph.setGraphSize(200);            // rebuilds the series
    check(grown == seriesY(graph, 0) + "|" + seriesY(graph, 2),
          "thinning: appended series must equal the rebuilt one");
  }

  // --- 5l. thinning while the full store moves on: a reading stays in its
  //          bucket (columns of time from the start), so the drawn minima
  //          and maxima of the older readings do not change from one new
  //          reading to the next ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.resize(300, 200);
    graph.setSampleTime(1);
    graph.setGraphSize(200);
    graph.store()->setMaxPoints(2000);  // keeps 200 s
    graph.setMode(GraphWidget::Manual);
    Feed feed(graph);
    graph.startSLOT();
    auto noise = [](int i) { return double((i * 37) % 101); };
    int i = 0;
    for (; i < 2500; i++)
      feed.value(noise(i));

    QChart *chart = graph.findChild<QChartView *>()->chart();
    auto *series = qobject_cast<QXYSeries *>(chart->series().value(0));
    const RecordingStore *store = graph.store();
    // the drawn points by time, without the oldest and the newest 20 s (a
    // bucket falls off or fills there)
    auto drawn = [&](double from, double to)
    {
      QStringList list;
      for (const QPointF &p : series->points())
        if (p.x() >= from && p.x() < to)
          list << QString("%1:%2").arg(p.x()).arg(p.y());
      return list.join(' ');
    };
    const double from = store->origin() / 1000.0 + 20, to = store->duration() / 1000.0 - 20;
    const QString before = drawn(from, to);
    feed.value(noise(i));
    check(store->origin() > 0, "scrolling: the store must be full and move on");
    check(!before.isEmpty() && before == drawn(from, to),
          "scrolling: the thinned points of the older readings must stay as they were");
  }

  // --- 5m. gaps: a reading without a value (NaN: overload, stale) breaks
  //          the line - Qt Charts would draw straight across it - and has no
  //          point; growing reading by reading gives what a rebuild gives,
  //          thinned or not ---
  {
    // the drawn lines: the y values of each line segment that has points
    auto lines = [](GraphWidget &graph)
    {
      QStringList out;
      for (QAbstractSeries *a : graph.findChild<QChartView *>()->chart()->series())
      {
        auto *line = qobject_cast<QLineSeries *>(a);
        if (!line || line->count() == 0)
          continue;
        QStringList ys;
        for (const QPointF &p : line->points())
          ys << QString::number(p.y());
        out << ys.join(' ');
      }
      return out;
    };
    auto dataPoints = [](GraphWidget &graph)
    {
      return qobject_cast<QScatterSeries *>(graph.findChild<QChartView *>()->chart()->series().value(1))->count();
    };

    GraphWidget graph(nullptr, &settings);
    graph.resize(800, 300);
    graph.setSampleTime(1);
    graph.setGraphSize(100);
    graph.setMode(GraphWidget::Manual);
    Feed feed(graph);
    graph.startSLOT();
    for (int i = 0; i < 30; i++)
      feed.value(i >= 10 && i < 20 ? qQNaN() : i);
    const QStringList grown = lines(graph);
    // data and integral, two segments each
    check(grown.size() == 4 && grown.first() == "0 1 2 3 4 5 6 7 8 9" && grown.contains("20 21 22 23 24 25 26 27 28 29"),
          "gaps: two segments, got " + grown.join(" | "));
    check(dataPoints(graph) == 20, QString("gaps: no point in the gap, got %1").arg(dataPoints(graph)));
    graph.setGraphSize(100);   // rebuilds the series
    check(lines(graph) == grown, "gaps: rebuilt = grown, got " + lines(graph).join(" | "));
    graph.clearSLOT();
    check(lines(graph).isEmpty() && dataPoints(graph) == 0, "gaps: cleared");

    // thinned: a bucket with a value draws it, a bucket of gaps is a gap
    GraphWidget thin(nullptr, &settings);
    thin.resize(300, 200);
    thin.setSampleTime(1);
    thin.setGraphSize(200);
    thin.setMode(GraphWidget::Manual);
    Feed thinFeed(thin);
    thin.startSLOT();
    for (int i = 0; i < 2000; i++)
      thinFeed.value((i / 300) % 2 ? qQNaN() : i % 7 == 3 ? qQNaN() : (i * 37) % 101);
    const QStringList thinGrown = lines(thin);
    check(thinGrown.size() == 2 * 4, QString("gaps thinned: 4 runs, data and integral, got %1").arg(thinGrown.size()));
    thin.setGraphSize(200);
    check(lines(thin) == thinGrown, "gaps thinned: rebuilt = grown");
  }

  // --- 5n0. Live starts with "All": the window as long as what there is
  //           (at least 10 s), growing with it ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.setGraphSize(600);
    int asked = -1;
    QObject::connect(&graph, &GraphWidget::windowRequested, [&](int s) { asked = s; });
    graph.liveSLOT();
    QToolButton *all = nullptr;
    for (QToolButton *b : graph.findChildren<QToolButton *>())
      if (b->text() == "All")
        all = b;
    check(all && all->isChecked() && asked == 10, QString("live: starts with All, window asked %1 s").arg(asked));
  }

  // --- 5n. a meter silent for minutes (every 2 s, then 5.5 min nothing),
  //          Live with "All": thinned, the column of the last value also
  //          holds the gap - the line must not bridge it, whatever the
  //          column width; and a reading is not drawn twice when the window
  //          grows with it ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.resize(220, 300);   // columns of several seconds, wider than the stale limit
    graph.setSampleTime(10);
    graph.setSampleLength(300000);   // 8:20:00
    graph.setGraphSize(60);
    // the window comes back at once, as through the settings
    QObject::connect(&graph, &GraphWidget::windowRequested, [&](int s) { graph.setGraphSize(s); });
    Feed feed(graph, 2000);
    graph.liveSLOT();
    for (QToolButton *b : graph.findChildren<QToolButton *>())
      if (b->text() == "All")
        b->click();
    auto *data = qobject_cast<QLineSeries *>(graph.findChild<QChartView *>()->chart()->series().first());
    auto bridged = [&](double from, double to)
    {
      for (QAbstractSeries *a : graph.findChild<QChartView *>()->chart()->series())
      {
        auto *line = qobject_cast<QLineSeries *>(a);
        if (!line || line->count() == 0 || line->pen() != data->pen())
          continue;
        if (line->at(0).x() < from && line->at(line->count() - 1).x() > to)
          return true;
      }
      return false;
    };
    int bridges = 0, doubled = 0;
    QString first;
    for (int i = 0; i < 570; i++)
    {
      if (i == 285)
      {
        // silent from 9:30: stale after the meter's interval, nothing until 15:00
        feed.now += 6000;
        graph.store()->setStale(true);
        feed.now = 900000;
        continue;
      }
      if (i > 285 && i < 450)
        continue;
      feed.value(650 + i * 0.3);
      if (i > 450 && bridged(570, 899))
      {
        bridges++;
        if (first.isEmpty())
          first = QString("reading %1").arg(i);
      }
      // the newest point once
      if (data->count() >= 2 && data->at(data->count() - 1) == data->at(data->count() - 2))
        doubled++;
    }
    check(bridges == 0, QString("silent meter: the line bridged the gap %1 times, first at %2").arg(bridges).arg(first));
    check(doubled == 0, QString("silent meter: the newest reading drawn twice %1 times").arg(doubled));
  }

  // --- 5n. the axis names the recording's unit: a meter switched from V to
  //          Ohm after a recording does not relabel it; a new one does ---
  {
    auto yTitle = [](GraphWidget &graph)
    {
      for (QGraphicsItem *item : graph.findChild<QChartView *>()->chart()->childItems())
        if (auto *text = dynamic_cast<QGraphicsSimpleTextItem *>(item); text && text->text().startsWith('['))
          return text->text();
      return QString();
    };
    GraphWidget graph(nullptr, &settings);
    graph.setUnit("mV");
    graph.setMode(GraphWidget::Manual);
    Feed feed(graph);
    graph.startSLOT();
    feed.value(0.5);
    graph.stopSLOT();
    graph.setUnit("kOhm");
    // 0.5 V of a meter that measures ohms now: the prefix that suits the values
    check(yTitle(graph) == "[mV]" && graph.store()->unit() == "V", "unit: the recorded V stay V, got " + yTitle(graph));
    graph.startSLOT();
    feed.value(4700);   // ohms now; the prefix by the axis
    check(yTitle(graph) == "[kOhm]", "unit: a new recording is in kOhm as the meter shows, got " + yTitle(graph));
  }

  // --- 5n2. the y labels in the meter's prefix: 350 mV, not 0.35 V ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.resize(800, 500);
    graph.setUnit("mV");
    graph.setScale(true, true, 0, 0);
    graph.setMode(GraphWidget::Manual);
    Feed feed(graph);
    graph.startSLOT();
    for (double v : { 0.05, 0.2, 0.35, -0.04 })
      feed.value(v);
    graph.show();
    QTest::qWait(50);
    QChart *chart = graph.findChild<QChartView *>()->chart();
    QStringList labels;
    for (QGraphicsItem *item : chart->childItems())
      if (auto *text = dynamic_cast<QGraphicsSimpleTextItem *>(item); text && text->isVisible()
          && text->pos().x() + text->boundingRect().width() < chart->plotArea().left())
        labels << text->text();
    bool mv = !labels.isEmpty();
    double hi = 0;
    for (const QString &l : labels)
    {
      mv = mv && !l.startsWith("-0") && !l.contains("0.");
      hi = qMax(hi, l.toDouble());
    }
    check(mv && hi >= 300, "y labels in mV, got " + labels.join(' '));
    check(qobject_cast<QValueAxis *>(chart->axes(Qt::Vertical).first())->labelsBrush().color().alpha() == 0,
          "y labels: Qt's own invisible");
  }

  // --- 5n3. a meter that changes range keeps the axis: up to 16 V, the
  //          reading now in mV - the labels stay volts (they were mV with
  //          1.6e+04) ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.resize(800, 500);
    graph.setUnit("V");
    graph.setScale(true, true, 0, 0);
    graph.setMode(GraphWidget::Manual);
    Feed feed(graph);
    graph.startSLOT();
    for (double v : { 5.0, 12.0, 15.5 })
      feed.value(v);
    graph.setUnit("mV");
    feed.value(0.004);
    graph.show();
    QTest::qWait(50);
    QChart *chart = graph.findChild<QChartView *>()->chart();
    QStringList labels;
    for (QGraphicsItem *item : chart->childItems())
      if (auto *text = dynamic_cast<QGraphicsSimpleTextItem *>(item); text && text->isVisible()
          && text->pos().x() + text->boundingRect().width() < chart->plotArea().left())
        labels << text->text();
    double hi = 0;
    for (const QString &l : labels)
      hi = qMax(hi, l.toDouble());
    check(!labels.isEmpty() && hi >= 15 && hi < 100 && !labels.join(' ').contains('e'),
          "range change: the y labels stay volts, got " + labels.join(' '));
  }

  // --- 5o. the integral's scale from before 26.2: once divided by the
  //          sample time, so the curve stays as it was - the sum of the
  //          0.5 s samples times 2 is the integral over time times 4 (up to
  //          the first sample, which had no time yet) ---
  {
    Settings old("intscale", tmpDir.path());
    old.setInt("Sample/rate", 5);
    old.setInt("Sample/rate-unit", 0);   // 0.5 s
    old.setString("Graph/int-scale", "2");
    old.save();
    check(GraphWidget::migrateIntegralScale(&old) && old.getString("Graph/int-scale") == "4",
          "int-scale: 2 at 0.5 s should become 4, got " + old.getString("Graph/int-scale"));
    check(!GraphWidget::migrateIntegralScale(&old) && old.getString("Graph/int-scale") == "4",
          "int-scale: converted twice");

    GraphWidget graph(nullptr, &old);
    graph.setSampleTime(5);
    graph.setGraphSize(100);
    graph.setIntegration(true, EngNumberValidator::value(old.getString("Graph/int-scale")), 0.0, 0.0);
    graph.setMode(GraphWidget::Manual);
    Feed feed(graph, 500);
    graph.startSLOT();
    for (int i = 0; i < 4; ++i)
      feed.value(1.0);
    // before: 2 4 6 8 (the samples 1 1 1 1, summed, times 2)
    check(seriesY(graph, 2) == "0 2 4 6", "int-scale: the curve got " + seriesY(graph, 2));

    Settings fresh("intscale-new", tmpDir.path());   // 1 s: nothing to do
    check(!GraphWidget::migrateIntegralScale(&fresh) && fresh.getBool("Graph/int-scale-per-second"),
          "int-scale: a sample time of 1 s keeps the scale");
  }

  // --- 6. engineering-prefix export/import: setUnit() must strip a leading
  //         G or p prefix too (previously only n/u/m/k/M were recognized), and
  //         a value re-imported from a prefix-scaled export (e.g. "2.5;pF")
  //         must round-trip back to the same raw value, not get double-scaled
  //         into something like "ppF" on the next export. ---
  {
    QTemporaryDir outDir;

    auto exportedUnitFor = [&](const QString &unit, double rawValue) -> QString
    {
      GraphWidget graph(nullptr, &settings);
      graph.setUnit(unit);
      graph.setSampleTime(10);
      graph.setGraphSize(5);
      graph.setMode(GraphWidget::Manual);
      Feed feed(graph);
      graph.startSLOT();
      feed.value(rawValue);

      QString path = outDir.path() + "/prefix_probe.csv";
      if (!graph.exportCsvFile(path))
        return QString();

      QStringList lines = readFile(path).split('\n', Qt::SkipEmptyParts);
      if (lines.size() < 2)
        return QString();
      return lines[1].split(';').value(3); // timestamp;time;value;unit
    };

    check(exportedUnitFor("GHz", 2.5e9) == "GHz",
          "setUnit() should strip a leading 'G' prefix so re-exporting a GHz-range value stays 'GHz', not 'GGHz' or 'Hz'");
    check(exportedUnitFor("pF", 2.5e-12) == "pF",
          "setUnit() should strip a leading 'p' prefix so re-exporting a pF-range value stays 'pF', not doubled to 'ppF'");

    // Full round trip at an extreme prefix: import a pF-range export, export
    // again, and the two exports must be byte-for-byte identical.
    {
      GraphWidget graph(nullptr, &settings);
      graph.setUnit("F");
      graph.setSampleTime(10);
      graph.setGraphSize(5);
      graph.setMode(GraphWidget::Manual);
      Feed feed(graph);
      graph.startSLOT();
      feed.value(2.5e-12);

      QString exported1 = outDir.path() + "/pf_export1.csv";
      check(graph.exportCsvFile(exported1), "pF round-trip: first export failed");

      GraphWidget graph2(nullptr, &settings);
      check(graph2.importCsvFile(exported1), "pF round-trip: re-import failed");

      QString exported2 = outDir.path() + "/pf_export2.csv";
      check(graph2.exportCsvFile(exported2), "pF round-trip: second export failed");

      QString c1 = readFile(exported1);
      QString c2 = readFile(exported2);
      check(!c1.isEmpty() && c1 == c2,
            "pF round-trip: re-exporting a re-imported pF-range file produced different CSV content");
    }
  }

  // --- 7. micro: export writes "µ", and both "µ" and the ASCII "u" of older
  //         exports must import with the same 1e-6 factor. Before the shared
  //         SiPrefix table the importer only knew "u", so a "µA" file came back
  //         a million times too large. ---
  {
    QTemporaryDir outDir;

    {
      GraphWidget graph(nullptr, &settings);
      graph.setUnit("A");
      graph.setSampleTime(10);
      graph.setGraphSize(5);
      graph.setMode(GraphWidget::Manual);
      Feed feed(graph);
      graph.startSLOT();
      feed.value(2.5e-6);
      QString path = outDir.path() + "/micro_export.csv";
      check(graph.exportCsvFile(path), "micro: export failed");
      QStringList lines = readFile(path).split('\n', Qt::SkipEmptyParts);
      check(lines.size() >= 2 && lines[1].split(';').value(3) == QString::fromUtf8("µA"),
            "a 2.5e-6 A value should export with the unit 'µA'");
    }

    auto importedValue = [&](const QString &unitInFile) -> double
    {
      QString path = outDir.path() + "/micro_" + QString::number(qHash(unitInFile)) + ".csv";
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return -1;
      QTextStream out(&f);
      out << "timestamp;time (s);value;unit\n"
          << "2026-09-19T10:00:00,000;0;2.5;" << unitInFile << "\n"
          << "2026-09-19T10:00:01,000;1;2.5;" << unitInFile << "\n";
      f.close();

      GraphWidget graph(nullptr, &settings);
      if (!graph.importCsvFile(path))
        return -1;
      QString exported = path + ".out.csv";
      if (!graph.exportCsvFile(exported))
        return -1;
      QStringList lines = readFile(exported).split('\n', Qt::SkipEmptyParts);
      if (lines.size() < 2)
        return -1;
      QStringList cols = lines[1].split(';');
      return cols.value(2).toDouble() * SiPrefix::factor(SiPrefix::split(cols.value(3)).prefix);
    };

    check(qFuzzyCompare(importedValue("µA"), 2.5e-6),
          "importing '2.5;µA' should yield 2.5e-6 A");
    check(qFuzzyCompare(importedValue("uA"), 2.5e-6),
          "importing the older ASCII spelling '2.5;uA' should yield 2.5e-6 A as well");
  }

  // --- 7b. image export: SVG and PDF keep the curve as vectors, the pixel
  //          formats come out with the asked-for size ---
  {
    Settings cfg("imgtest", tmpDir.path());
    GraphWidget graph(nullptr, &cfg);
    graph.resize(800, 500);
    check(graph.importCsvFile(dataDir + "/new_larger.csv"), "image export: import failed");
    // as in the program: the graph is on screen and laid out (a hidden
    // widget gets no resize event, and Qt Charts lays out one turn later)
    graph.show();
    QTest::qWait(100);

    const QString svg = tmpDir.filePath("graph.svg");
    check(graph.exportImageFile(svg, QSize(1000, 600)), "SVG written");
    QFile f(svg);
    check(f.open(QIODevice::ReadOnly), "SVG opens");
    const QByteArray content = f.readAll();
    f.close();
    QXmlStreamReader xml(content);
    while (!xml.atEnd())
      xml.readNext();
    check(!xml.hasError(), "SVG is well-formed XML: " + xml.errorString());
    // QSvgGenerator writes width/height in millimetres; the pixel size the
    // drawing was made for is the viewBox
    check(content.contains("<svg") && content.contains("viewBox=\"0 0 1000 600\""),
          "SVG carries the requested size: " + QString::fromUtf8(content.left(200)));
    // vector output, not a pixel dump: the curve and the axis labels are
    // paths and text, so there is no embedded raster image
    // the curve itself: Qt Charts writes a line series as one short
    // polyline per segment, so a curve of ~270 samples gives hundreds of
    // them; the frame and the grid alone are a few dozen
    const int shapes = content.count("<polyline") + content.count("<path");
    check(shapes >= 200, QString("SVG holds the curve as vectors (%1 polylines/paths)").arg(shapes));
    check(!content.contains("<image"), "SVG holds no embedded bitmap");
    check(content.contains("QtDMM"), "SVG names its origin in the title");

    const QString pdf = tmpDir.filePath("graph.pdf");
    check(graph.exportImageFile(pdf, QSize(1000, 600)), "PDF written");
    QFile pf(pdf);
    check(pf.open(QIODevice::ReadOnly) && pf.read(5) == "%PDF-", "PDF has its magic");
    pf.close();

    const QString png = tmpDir.filePath("graph.png");
    check(graph.exportImageFile(png, QSize(320, 200)), "PNG written");
    QImage image(png);
    check(!image.isNull() && image.size() == QSize(320, 200), "PNG has the requested size");

    check(!graph.exportImageFile(tmpDir.filePath("no/such/dir/graph.svg")), "an unwritable path fails");
  }

  // --- 7c. colour variants: 1-2-5 divisions and 10 x 8 for the scope-like
  //          ones, names round-trip ---
  {
    check(GraphWidget::niceStep(0.3) == 0.5 && GraphWidget::niceStep(1) == 1 && GraphWidget::niceStep(1.01) == 2
          && GraphWidget::niceStep(4.9) == 5 && GraphWidget::niceStep(6) == 10 && qFuzzyCompare(GraphWidget::niceStep(0.0021), 0.005),
          "niceStep follows 1-2-5");
    for (auto v : { GraphWidget::Neutral, GraphWidget::ScopeBlue, GraphWidget::PhosphorGreen, GraphWidget::PhosphorAmber,
                    GraphWidget::ChartRecorder, GraphWidget::Custom })
      check(GraphWidget::variantFromName(GraphWidget::variantName(v)) == v, "variant name round-trip " + GraphWidget::variantName(v));
    check(GraphWidget::variantFromName("nonsense") == GraphWidget::Neutral, "unknown variant is neutral");

    Settings cfg("varianttest", tmpDir.path());
    GraphWidget graph(nullptr, &cfg);
    graph.resize(800, 500);
    graph.setScale(false, false, -0.3, 11.7);
    auto *y = graph.findChild<QChartView *>()->chart()->axes(Qt::Vertical).first();
    auto *yAxis = qobject_cast<QValueAxis *>(y);
    check(qFuzzyCompare(yAxis->min(), -0.3) && qFuzzyCompare(yAxis->max(), 11.7), "neutral keeps the scale as set");
    graph.setColorVariant(GraphWidget::Neutral, GraphWidget::PhosphorGreen);   // this graph only
    const double div = (yAxis->max() - yAxis->min()) / 8;
    check(yAxis->tickCount() == 9 && qFuzzyCompare(div, 2.0) && qFuzzyCompare(yAxis->min(), -2.0),
          QString("phosphor: 8 divisions of 2 from -2, got %1..%2").arg(yAxis->min()).arg(yAxis->max()));
    check(graph.colorOverride() == GraphWidget::PhosphorGreen, "the override is kept");
    graph.setColorVariant(GraphWidget::Custom);   // the default, no override
    check(graph.colorVariant() == GraphWidget::Custom && graph.colorOverride() == -1, "default without override");
    check(yAxis->tickCount() == 5 && qFuzzyCompare(yAxis->max(), 11.7), "custom goes back to the plain scale");
  }

  // --- 7d. the time axis: whole time steps, labelled in s, min or h; the
  //          scope-like variants make 600 s 10 x 1 min (not 10 x 100 s) ---
  {
    check(GraphWidget::timeStep(45) == 60 && GraphWidget::timeStep(61) == 120 && GraphWidget::timeStep(3) == 5
          && GraphWidget::timeStep(12) == 15 && GraphWidget::timeStep(400) == 600 && GraphWidget::timeStep(2000) == 3600
          && GraphWidget::timeStep(0.15) == 0.2 && GraphWidget::timeStep(100000) == 172800,
          "timeStep: 1, 2, 5, 10, 15, 30 s, 1, 2, 5, 10, 15, 30 min, h, days");
    Settings cfg("timeaxis", tmpDir.path());
    GraphWidget graph(nullptr, &cfg);
    graph.resize(800, 500);
    graph.setSampleTime(10);
    graph.setGraphSize(600);
    graph.show();
    QTest::qWait(50);
    QChart *chart = graph.findChild<QChartView *>()->chart();
    auto *x = qobject_cast<QValueAxis *>(chart->axes(Qt::Horizontal).first());
    auto labels = [&]
    {
      QStringList out;
      for (QGraphicsItem *item : chart->childItems())
        if (auto *t = dynamic_cast<QGraphicsSimpleTextItem *>(item); t && t->isVisible()
            && t->pos().y() > chart->plotArea().bottom())
          out << t->text();
      return out;
    };
    check(x->tickType() == QValueAxis::TicksDynamic && x->tickInterval() == 120 && x->titleText() == "[min]",
          QString("neutral 600 s: a tick every 2 min, got %1 s, '%2'").arg(x->tickInterval()).arg(x->titleText()));
    check(labels().join(' ') == "0 2 4 6 8", "neutral 600 s: labels 0 2 4 6 8 min, got " + labels().join(' '));
    graph.setColorVariant(GraphWidget::ScopeBlue);
    check(x->tickInterval() == 60 && qFuzzyCompare(x->max() - x->min(), 600.0),
          QString("scope 600 s: 10 x 1 min, got %1 x %2 s").arg((x->max() - x->min()) / x->tickInterval()).arg(x->tickInterval()));
    check(labels().join(' ') == "0 1 2 3 4 5 6 7 8 9 10", "scope 600 s: labels 0..10 min, got " + labels().join(' '));
    graph.setGraphSize(20);
    check(x->tickInterval() == 2 && x->titleText() == "[sec]", QString("scope 20 s: 10 x 2 s, got %1").arg(x->tickInterval()));
    graph.setGraphSize(7200);
    check(x->tickInterval() == 900 && x->titleText() == "[min]", QString("scope 2 h: 10 x 15 min, got %1").arg(x->tickInterval()));
    graph.setColorVariant(GraphWidget::Neutral);
    check(x->tickInterval() == 1800 && x->titleText() == "[min]", QString("neutral 2 h: every 30 min, got %1").arg(x->tickInterval()));
  }

  // --- 7d2. a graph without colours set (a test, a picture made
  //          offscreen) shows its x labels once: Qt's own stay invisible ---
  {
    GraphWidget graph(nullptr, &settings);
    auto *x = qobject_cast<QValueAxis *>(graph.findChild<QChartView *>()->chart()->axes(Qt::Horizontal).first());
    check(x && x->labelsBrush().color().alpha() == 0, "x labels: Qt's own must be invisible from the start");
  }

  // --- 7e. no value yet: the auto scale has no range (+-1e40, or 0..0
  //          with "include zero"), the axis still gets one, so grid and
  //          labels are there before the first reading ---
  {
    Settings cfg("emptyscale", tmpDir.path());
    GraphWidget graph(nullptr, &cfg);
    graph.resize(800, 500);
    auto *yAxis = qobject_cast<QValueAxis *>(graph.findChild<QChartView *>()->chart()->axes(Qt::Vertical).first());
    for (bool includeZero : { false, true })
    {
      graph.setScale(true, includeZero, 0, 0);
      graph.clearSLOT();
      check(yAxis->min() == 0 && yAxis->max() == 1,
            QString("empty, include zero %1: 0..1, got %2..%3")
              .arg(includeZero).arg(yAxis->min()).arg(yAxis->max()));
    }
  }

  // --- 8. EngNumberValidator: what engValue() writes, value() must read
  //         back. engValue() emits "µ" while value() used to recognise only
  //         "u", so micro thresholds silently lost their factor. ---
  {
    struct { double v; const char *text; } cases[] = {
      {1500.0,   "1.5k"},
      {0.0015,   "1.5m"},
      {1.5e-6,   "1.5µ"},
      {2.5e9,    "2.5G"},
      {42.0,     "42"},
    };
    for (const auto &c : cases)
    {
      QString written = EngNumberValidator::engValue(c.v);
      check(written == QString::fromUtf8(c.text),
            QString("engValue(%1) should be '%2', got '%3'").arg(c.v).arg(c.text).arg(written));
      check(qFuzzyCompare(EngNumberValidator::value(written) + 1.0, c.v + 1.0),
            QString("value(engValue(%1)) should round-trip, got %2")
              .arg(c.v).arg(EngNumberValidator::value(written)));
    }
    check(qFuzzyCompare(EngNumberValidator::value("1.5u"), 1.5e-6),
          "value() should accept the ASCII 'u' for micro");
  }

  // --- 9. the modes: Live follows the newest reading; recording shows
  //         "● REC" top left, View where the curve comes from; export while
  //         live saves the window up to now; a file loaded is viewed ---
  {
    GraphWidget graph(nullptr, &settings);
    graph.setSampleTime(10);
    graph.setGraphSize(10);        // a window of 10 s
    graph.setSampleLength(300);         // live keeps 30 s
    graph.setMode(GraphWidget::Manual);
    Feed feed(graph, 500);
    QSignalSpy states(&graph, &GraphWidget::stateChanged);
    graph.liveSLOT();
    check(states.size() == 1 && states.first().first().toInt() == RecordingStore::Live, "modes: Live said");
    check(graph.stateText().isEmpty(), "modes: nothing top left while live");
    for (int i = 0; i < 80; ++i)        // 40 s
      feed.value(i);
    auto *x = qobject_cast<QValueAxis *>(graph.findChild<QChartView *>()->chart()->axes(Qt::Horizontal).first());
    const double newest = graph.store()->series().last().t / 1000.0;
    check(qAbs(x->max() - newest) < 0.01 && qAbs(x->max() - x->min() - 9) < 0.01,
          QString("live: the window %1..%2 s follows the newest at %3 s").arg(x->min()).arg(x->max()).arg(newest));
    check(graph.store()->origin() == graph.store()->duration() - 30000, "live: keeps the recording length");
    check(!graph.findChild<QScrollBar *>()->isEnabled(), "live: the scroll bar rests");

    const QString liveFile = tmpDir.filePath("live.csv");
    check(graph.exportCsvFile(liveFile, true), "live export: written");
    const std::optional<Recording> saved = RecordingFile::read(liveFile);
    check(saved && saved->values.size() >= 60 && saved->values.last() == 79,
          QString("live export: %1 values up to the newest").arg(saved ? saved->values.size() : -1));
    check(graph.store()->state() == RecordingStore::Live, "live export: still live");

    graph.setSampleLength(50);          // 5 s
    graph.startSLOT();
    check(graph.store()->count() == 1 && graph.findChild<QScrollBar *>()->isEnabled(), "record: cleared");
    for (int i = 0; i < 4; ++i)         // 2 s
      feed.value(i);
    graph.store()->poll();              // the clock of the label, every second
    check(graph.stateText() == QString::fromUtf8("● REC 0:02 / 0:05"),
          "record: top left " + graph.stateText());
    auto *label = graph.findChild<QLabel *>("ui_graphState");
    check(label && label->isVisible() == graph.isVisible() && label->text() == graph.stateText(), "record: the label shows it");
    graph.setSampleLength(0);
    check(graph.stateText() == QString::fromUtf8("● REC 0:02"), "record without a length: " + graph.stateText());
    graph.stopSLOT();
    const QString shown = QLocale().toString(Feed::wall(40000), QLocale::ShortFormat);
    check(graph.stateText() == QString::fromUtf8("Recording of %1 · 0:02").arg(shown),
          "view: top left " + graph.stateText());

    check(graph.importCsvFile(dataDir + "/new_larger.csv"), "view: import");
    check(graph.store()->state() == RecordingStore::View && graph.stateText().startsWith("new_larger.csv · "),
          "view: the file top left, got " + graph.stateText());
    graph.liveSLOT();
    check(graph.stateText().isEmpty() && graph.store()->count() <= 1, "live again: the file goes");
  }

  if (failed == 0)
    qInfo() << "All GraphWidget baseline tests passed.";
  else
    qWarning() << failed << "GraphWidget baseline test(s) failed.";

  return failed == 0 ? 0 : 1;
}
