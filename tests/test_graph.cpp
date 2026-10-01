// Baseline behavior tests for DMMGraph, written before the planned QtGraph-based
// rewrite so the current CSV import/export contract has a regression net to
// compare the replacement against.
#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QTextStream>
#include <QDebug>
#include <QFileInfo>

#include "dmmgraph.h"
#include <QChartView>
#include <QValueAxis>
#include <QGraphicsSimpleTextItem>
#include <QXYSeries>
#include <QScrollBar>
#include <QToolButton>
#include "siprefix.h"
#include "engnumbervalidator.h"
#include "settings.h"

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
    DMMGraph graph(nullptr, &settings);
    QString path = dataDir + "/" + name;
    bool ok = graph.importCsvFile(path);
    check(ok, QString("importCsvFile() failed for fixture '%1'").arg(name));
  }

  // --- 2. malformed input must be rejected, not crash or half-import ---
  {
    QTemporaryDir badDir;
    QString badFile = badDir.path() + "/broken.csv";
    QFile f(badFile);
    check(f.open(QIODevice::WriteOnly), "temp file opens for writing");
    QTextStream(&f) << "this is not a valid QtDMM export\n";
    f.close();

    DMMGraph graph(nullptr, &settings);
    bool ok = graph.importCsvFile(badFile);
    check(!ok, "importCsvFile() should reject a non-matching file, but reported success");
  }

  // --- 3. import -> export -> re-import must round-trip to the same CSV
  //         (this is the contract the QtGraph replacement needs to preserve) ---
  {
    DMMGraph graph(nullptr, &settings);
    check(graph.importCsvFile(dataDir + "/new_larger.csv"), "round-trip: initial import failed");

    QTemporaryDir outDir;
    QString exported1 = outDir.path() + "/export1.csv";
    check(graph.exportCsvFile(exported1), "round-trip: first export failed");

    DMMGraph graph2(nullptr, &settings);
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
    DMMGraph graph(nullptr, &settings);
    graph.setSampleTime(10);   // one sample per second
    graph.setGraphSize(300, 3600);
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
      QSignalSpy spy(&graph, &DMMGraph::windowRequested);
      one->click();
      check(spy.size() == 1 && spy.last().at(0).toInt() == 60, "time buttons: 1 min asks for a 60 s window");
      // MainWid applies the request through the settings, like a zoom
      graph.setGraphSize(60, 3600);
      check(one->isChecked() && !five->isChecked(), "time buttons: the applied window is marked");
      graph.setGraphSize(45, 3600);
      check(!one->isChecked() && !five->isChecked() && !thirty->isChecked(), "time buttons: a zoomed window marks none");
      graph.setGraphSize(60, 600);
      check(thirty->isHidden() && !five->isHidden(), "time buttons: 30 min is hidden for a 10 min recording");

      // All: the recording so far (at least 10 s), growing with it
      spy.clear();
      graph.setGraphSize(10, 600);
      all->click();
      check(all->isChecked(), "time buttons: All stays marked");
      graph.startSLOT();
      for (int i = 0; i < 150; ++i)   // 15 samples at 1 s
        graph.addValue(1.0);
      bool grew = !spy.isEmpty();
      for (const QList<QVariant> &args : spy)
        grew = grew && args.at(0).toInt() > 10 && args.at(0).toInt() <= 600;
      check(grew, QString("time buttons: All grows the window with the recording (%1 requests)").arg(spy.size()));
      graph.zoomInSLOT();
      graph.setGraphSize(8, 600);
      check(!all->isChecked(), "time buttons: zooming ends All");
    }
  }

  // --- 3c. All on a longer recording: from one minute on it asks for whole
  //         minutes (the settings keep seconds only up to 99999; an odd
  //         value above was cut and asked for again with every sample) ---
  {
    DMMGraph graph(nullptr, &settings);
    graph.setSampleTime(10);   // one sample per second
    graph.setGraphSize(10, 3600);
    QToolButton *all = nullptr;
    for (QToolButton *b : graph.findChildren<QToolButton *>())
      if (b->text() == "All")
        all = b;
    check(all, "All on a long recording: button exists");
    if (all)
    {
      all->click();
      QList<int> requests;
      // MainWid applies each request through the settings
      QObject::connect(&graph, &DMMGraph::windowRequested, &graph, [&](int seconds)
      {
        requests << seconds;
        graph.setGraphSize(seconds, 3600);
      });
      graph.startSLOT();
      for (int i = 0; i < 3000; ++i)   // 300 s
        graph.addValue(1.0);
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
    DMMGraph graph(nullptr, &settings);
    QSignalSpy spy(&graph, &DMMGraph::sampleTime);
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

    DMMGraph graph(nullptr, &settings);
    QSignalSpy spy(&graph, &DMMGraph::sampleTime);
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

  // --- 5. smoke test for addValue()'s live-recording ring buffer against the
  //         Qt Charts series sync (rebuildSeries()/append()): must survive a
  //         buffer wrap without crashing, and setGraphSize() must be callable
  //         again afterwards while data already exists. ---
  {
    DMMGraph graph(nullptr, &settings);
    graph.setSampleTime(1);
    graph.setGraphSize(5, 5); // small window -> wraps quickly
    graph.setMode(DMMGraph::Manual);
    graph.startSLOT();

    for (int i = 0; i < 20; i++)
      graph.addValue(i * 0.1);

    check(graph.dirty(), "ring-buffer smoke test: expected graph to be marked dirty after recording");

    graph.setGraphSize(10, 10);
    graph.addValue(1.23);
  }

  // --- 5a. integration: the running sum of the readings above the threshold,
  //          0 at or below it - the first sample, too (it used to be the
  //          threshold itself, a spike at the left edge) ---
  {
    DMMGraph graph(nullptr, &settings);
    graph.setSampleTime(1);
    graph.setGraphSize(100, 100);
    graph.setIntegration(true, 1.0, 0.5, 0.0);
    graph.setMode(DMMGraph::Manual);
    graph.startSLOT();
    for (double v : { 0.1, 0.2, 0.8, 0.9, 0.1, 0.6 })
      graph.addValue(v);
    const QList<QAbstractSeries *> series = graph.findChild<QChartView *>()->chart()->series();
    auto *integral = series.size() > 2 ? qobject_cast<QXYSeries *>(series[2]) : nullptr;   // data line, data points, integration
    QStringList got;
    if (integral)
      for (const QPointF &p : integral->points())
        got << QString::number(p.y());
    check(got.join(' ') == "0 0 0.8 1.7 0 0.6",
          QString("integration: expected '0 0 0.8 1.7 0 0.6', got '%1'").arg(got.join(' ')));
  }

  // --- 5b. window size and sample time in either order: MainWid used to set
  //          the window (counted in samples) before the sample time, and any
  //          later x axis update showed it scaled by the ratio of the two ---
  {
    Settings cfg("ordertest", tmpDir.path());
    DMMGraph graph(nullptr, &cfg);
    graph.resize(800, 500);
    auto *x = qobject_cast<QValueAxis *>(graph.findChild<QChartView *>()->chart()->axes(Qt::Horizontal).first());
    graph.setGraphSize(600, 3600);   // with the default sample time of 0.1 s
    graph.setSampleTime(10);         // then 1 s
    // the error showed on the next x axis update, e.g. scrolling
    auto *bar = graph.findChild<QScrollBar *>(QString(), Qt::FindDirectChildrenOnly);   // not the chart view's
    bar->setValue(1);
    bar->setValue(0);
    check(qAbs(x->max() - x->min() - 599) < 1.5,
          QString("window stays 600 s after a new sample time, got %1 s").arg(x->max() - x->min()));
    graph.setSampleTime(10);         // unchanged: nothing to do
    graph.setGraphSize(300, 3600);
    check(qAbs(x->max() - x->min() - 299) < 1.5, QString("300 s window, got %1 s").arg(x->max() - x->min()));
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
      DMMGraph graph(nullptr, &settings);
      graph.setUnit(unit);
      graph.setSampleTime(10);
      graph.setGraphSize(5, 5);
      graph.setMode(DMMGraph::Manual);
      graph.startSLOT();
      graph.addValue(rawValue);

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
      DMMGraph graph(nullptr, &settings);
      graph.setUnit("F");
      graph.setSampleTime(10);
      graph.setGraphSize(5, 5);
      graph.setMode(DMMGraph::Manual);
      graph.startSLOT();
      graph.addValue(2.5e-12);

      QString exported1 = outDir.path() + "/pf_export1.csv";
      check(graph.exportCsvFile(exported1), "pF round-trip: first export failed");

      DMMGraph graph2(nullptr, &settings);
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
      DMMGraph graph(nullptr, &settings);
      graph.setUnit("A");
      graph.setSampleTime(10);
      graph.setGraphSize(5, 5);
      graph.setMode(DMMGraph::Manual);
      graph.startSLOT();
      graph.addValue(2.5e-6);
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

      DMMGraph graph(nullptr, &settings);
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
    DMMGraph graph(nullptr, &cfg);
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
    check(DMMGraph::niceStep(0.3) == 0.5 && DMMGraph::niceStep(1) == 1 && DMMGraph::niceStep(1.01) == 2
          && DMMGraph::niceStep(4.9) == 5 && DMMGraph::niceStep(6) == 10 && qFuzzyCompare(DMMGraph::niceStep(0.0021), 0.005),
          "niceStep follows 1-2-5");
    for (auto v : { DMMGraph::Neutral, DMMGraph::ScopeBlue, DMMGraph::PhosphorGreen, DMMGraph::PhosphorAmber,
                    DMMGraph::ChartRecorder, DMMGraph::Custom })
      check(DMMGraph::variantFromName(DMMGraph::variantName(v)) == v, "variant name round-trip " + DMMGraph::variantName(v));
    check(DMMGraph::variantFromName("nonsense") == DMMGraph::Neutral, "unknown variant is neutral");

    Settings cfg("varianttest", tmpDir.path());
    DMMGraph graph(nullptr, &cfg);
    graph.resize(800, 500);
    graph.setScale(false, false, -0.3, 11.7);
    auto *y = graph.findChild<QChartView *>()->chart()->axes(Qt::Vertical).first();
    auto *yAxis = qobject_cast<QValueAxis *>(y);
    check(qFuzzyCompare(yAxis->min(), -0.3) && qFuzzyCompare(yAxis->max(), 11.7), "neutral keeps the scale as set");
    graph.setColorVariant(DMMGraph::Neutral, DMMGraph::PhosphorGreen);   // this graph only
    const double div = (yAxis->max() - yAxis->min()) / 8;
    check(yAxis->tickCount() == 9 && qFuzzyCompare(div, 2.0) && qFuzzyCompare(yAxis->min(), -2.0),
          QString("phosphor: 8 divisions of 2 from -2, got %1..%2").arg(yAxis->min()).arg(yAxis->max()));
    check(graph.colorOverride() == DMMGraph::PhosphorGreen, "the override is kept");
    graph.setColorVariant(DMMGraph::Custom);   // the default, no override
    check(graph.colorVariant() == DMMGraph::Custom && graph.colorOverride() == -1, "default without override");
    check(yAxis->tickCount() == 5 && qFuzzyCompare(yAxis->max(), 11.7), "custom goes back to the plain scale");
  }

  // --- 7d. the time axis: whole time steps, labelled in s, min or h; the
  //          scope-like variants make 600 s 10 x 1 min (not 10 x 100 s) ---
  {
    check(DMMGraph::timeStep(45) == 60 && DMMGraph::timeStep(61) == 120 && DMMGraph::timeStep(3) == 5
          && DMMGraph::timeStep(12) == 15 && DMMGraph::timeStep(400) == 600 && DMMGraph::timeStep(2000) == 3600
          && DMMGraph::timeStep(0.15) == 0.2 && DMMGraph::timeStep(100000) == 172800,
          "timeStep: 1, 2, 5, 10, 15, 30 s, 1, 2, 5, 10, 15, 30 min, h, days");
    Settings cfg("timeaxis", tmpDir.path());
    DMMGraph graph(nullptr, &cfg);
    graph.resize(800, 500);
    graph.setSampleTime(10);
    graph.setGraphSize(600, 3600);
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
    graph.setColorVariant(DMMGraph::ScopeBlue);
    check(x->tickInterval() == 60 && qFuzzyCompare(x->max() - x->min(), 600.0),
          QString("scope 600 s: 10 x 1 min, got %1 x %2 s").arg((x->max() - x->min()) / x->tickInterval()).arg(x->tickInterval()));
    check(labels().join(' ') == "0 1 2 3 4 5 6 7 8 9 10", "scope 600 s: labels 0..10 min, got " + labels().join(' '));
    graph.setGraphSize(20, 3600);
    check(x->tickInterval() == 2 && x->titleText() == "[sec]", QString("scope 20 s: 10 x 2 s, got %1").arg(x->tickInterval()));
    graph.setGraphSize(7200, 36000);
    check(x->tickInterval() == 900 && x->titleText() == "[min]", QString("scope 2 h: 10 x 15 min, got %1").arg(x->tickInterval()));
    graph.setColorVariant(DMMGraph::Neutral);
    check(x->tickInterval() == 1800 && x->titleText() == "[min]", QString("neutral 2 h: every 30 min, got %1").arg(x->tickInterval()));
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

  if (failed == 0)
    qInfo() << "All DMMGraph baseline tests passed.";
  else
    qWarning() << failed << "DMMGraph baseline test(s) failed.";

  return failed == 0 ? 0 : 1;
}
