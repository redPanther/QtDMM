// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "readinglogwid.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyle>
#include <QTableView>
#include <QToolButton>
#include <QStyleOptionViewItem>
#include <QVBoxLayout>

#include "siprefix.h"

ReadingLogWid::ReadingLogWid(QWidget *parent) :
  QWidget(parent),
  m_log(new ReadingLog(this))
{
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);

  m_view = new QTableView(this);
  m_view->setModel(m_log);
  m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_view->setAlternatingRowColors(true);
  m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_view->setWordWrap(false);
  m_view->verticalHeader()->setDefaultSectionSize(m_view->fontMetrics().height() + 4);
  m_view->verticalHeader()->setVisible(false);
  m_view->horizontalHeader()->setStretchLastSection(false);
  // widths come from fitColumns(): every column as wide as its content, the
  // rest of the width shared out; a table narrower than that scrolls
  m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  m_view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_view->horizontalHeader()->setHighlightSections(false);
  m_view->viewport()->installEventFilter(this);
  m_view->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_view, &QTableView::customContextMenuRequested, this, [this](const QPoint &pos)
  {
    QMenu menu(this);
    menu.addAction(tr("&Copy"), this, &ReadingLogWid::copySLOT);
    menu.addAction(tr("Select &all"), m_view, &QTableView::selectAll);
    menu.addSeparator();
    menu.addAction(tr("&Export..."), this, &ReadingLogWid::exportSLOT);
    menu.addAction(tr("C&lear"), this, &ReadingLogWid::clearSLOT);
    menu.exec(m_view->viewport()->mapToGlobal(pos));
  });
  // Ctrl+C copies whole rows while the table has the focus. Elsewhere in
  // the window it connects the meter, so this is not a QAction shortcut
  // (that would be ambiguous) but a ShortcutOverride: the key event then
  // reaches the view before the window's shortcuts see it.
  m_view->installEventFilter(this);
  layout->addWidget(m_view, 1);

  QHBoxLayout *bar = new QHBoxLayout;
  // one button, pause while logging, play while paused
  m_pause = new QToolButton(this);
  m_pause->setCheckable(true);
  m_pause->setAutoRaise(true);
  m_pause->setToolTip(tr("Pause logging"));
  connect(m_pause, &QToolButton::toggled, this, [this](bool paused)
  {
    m_log->setPaused(paused);
    m_pause->setIcon(QIcon::fromTheme(paused ? "media-playback-start" : "media-playback-pause"));
    m_pause->setToolTip(paused ? tr("Resume logging") : tr("Pause logging"));
  });
  m_pause->setIcon(QIcon::fromTheme("media-playback-pause"));
  bar->addWidget(m_pause);

  m_follow = new QCheckBox(tr("&Follow"), this);
  m_follow->setChecked(true);
  m_follow->setToolTip(tr("Keep the newest reading in view. Scrolling up switches this off."));
  bar->addWidget(m_follow);

  bar->addStretch(1);
  QLabel *keep = new QLabel(tr("Keep"), this);
  bar->addWidget(keep);
  m_maxRows = new QSpinBox(this);
  m_maxRows->setRange(100, 1000000);
  m_maxRows->setSingleStep(1000);
  m_maxRows->setValue(m_log->maxRows());
  m_maxRows->setSuffix(tr(" rows"));
  m_maxRows->setToolTip(tr("How many readings the table keeps; the oldest are dropped."));
  connect(m_maxRows, qOverload<int>(&QSpinBox::valueChanged), m_log, &ReadingLog::setMaxRows);
  bar->addWidget(m_maxRows);

  QPushButton *exportButton = new QPushButton(tr("&Export..."), this);
  connect(exportButton, &QPushButton::clicked, this, &ReadingLogWid::exportSLOT);
  bar->addWidget(exportButton);
  QPushButton *clearButton = new QPushButton(tr("C&lear"), this);
  connect(clearButton, &QPushButton::clicked, this, &ReadingLogWid::clearSLOT);
  bar->addWidget(clearButton);
  layout->addLayout(bar);

  m_stats = new QLabel(this);
  m_stats->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_stats->setWordWrap(true);   // a long line must not dictate the width
  layout->addWidget(m_stats);

  connect(m_log, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &, int first, int last)
  {
    measureRows(first, last);
  });
  connect(m_log, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &tl, const QModelIndex &br)
  {
    // the Time column gained or lost its date: measure it afresh
    if (tl.column() <= ReadingLog::Time && br.column() >= ReadingLog::Time && br.row() - tl.row() + 1 == m_log->rowCount())
    {
      m_need[ReadingLog::Time] = 0;
      measureRows(qMax(0, m_log->rowCount() - kMeasureRows), m_log->rowCount() - 1);
    }
  });
  connect(m_log, &QAbstractItemModel::modelReset, this, [this]
  {
    m_need.fill(0);
    measureRows(0, -1);
  });
  connect(m_log, &QAbstractItemModel::rowsInserted, this, &ReadingLogWid::followSLOT);
  connect(m_log, &QAbstractItemModel::rowsInserted, this, &ReadingLogWid::updateStats);
  connect(m_log, &QAbstractItemModel::rowsRemoved, this, &ReadingLogWid::updateStats);
  connect(m_log, &QAbstractItemModel::modelReset, this, &ReadingLogWid::updateStats);
  // scrolling away from the end pauses following; scrolling back resumes it
  connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value)
  {
    if (m_view->verticalScrollBar()->isSliderDown() || QApplication::mouseButtons() != Qt::NoButton
        || m_view->verticalScrollBar()->hasFocus())
      m_follow->setChecked(value >= m_view->verticalScrollBar()->maximum());
  });
  m_need.fill(0, ReadingLog::ColumnCount);
  measureRows(0, -1);
  updateStats();
}

void ReadingLogWid::measureRows(int first, int last)
{
  // Header and the given rows, measured with the view's own item size hints
  // (padding and style included). m_need only grows: a column that jumps
  // back and forth with every reading would be worse than one a bit wide.
  // Only new rows are measured, so a full table costs nothing per reading.
  QHeaderView *header = m_view->horizontalHeader();
  const QStyleOptionViewItem option = [this]
  {
    QStyleOptionViewItem o;
    o.initFrom(m_view);
    o.font = m_view->font();
    return o;
  }();
  bool grown = false;
  for (int c = 0; c < ReadingLog::ColumnCount; ++c)
  {
    int w = qMax(m_need[c], header->sectionSizeHint(c));
    for (int r = qMax(first, last - kMeasureRows + 1); r <= last; ++r)
      w = qMax(w, m_view->itemDelegateForColumn(c) ? m_view->itemDelegateForColumn(c)->sizeHint(option, m_log->index(r, c)).width()
                                                    : m_view->itemDelegate()->sizeHint(option, m_log->index(r, c)).width());
    if (w != m_need[c])
    {
      m_need[c] = w;
      grown = true;
    }
  }
  if (grown || first > last)
    fitColumns();
}

void ReadingLogWid::fitColumns()
{
  int sum = 0;
  for (int w : m_need)
    sum += w;
  const int extra = qMax(0, m_view->viewport()->width() - sum);
  const int n = ReadingLog::ColumnCount;
  for (int c = 0; c < n; ++c)
    m_view->horizontalHeader()->resizeSection(c, m_need[c] + extra / n + (c == n - 1 ? extra % n : 0));
}

int ReadingLogWid::maxRows() const
{
  return m_maxRows->value();
}

void ReadingLogWid::setMaxRows(int rows)
{
  m_maxRows->setValue(rows);
}

void ReadingLogWid::followSLOT()
{
  if (m_follow->isChecked())
    m_view->scrollToBottom();
}

void ReadingLogWid::updateStats()
{
  const ReadingLog::Stats s = m_log->stats();
  if (s.count == 0)
  {
    m_stats->setText(tr("No readings yet."));
    return;
  }
  auto fmt = [&](double v)
  {
    QString prefix;
    return QString::number(SiPrefix::scale(v, &prefix), 'g', 5) + " " + prefix + s.unit;
  };
  QString text = tr("%n reading(s)", "", s.count);
  if (s.numeric)
    text += "   " + tr("Min %1   Max %2   Mean %3   Span %4")
              .arg(fmt(s.min), fmt(s.max), fmt(s.mean), fmt(s.max - s.min));
  m_stats->setText(text);
}

void ReadingLogWid::clearSLOT()
{
  m_log->clear();
}

void ReadingLogWid::exportSLOT()
{
  const QString csvFilter = tr("CSV (*.csv)"), xlsxFilter = tr("Excel (*.xlsx)"), odsFilter = tr("OpenDocument (*.ods)");
  QString filter = csvFilter;
  QString path = QFileDialog::getSaveFileName(this, tr("Export readings"), QString(),
                                              csvFilter + ";;" + xlsxFilter + ";;" + odsFilter, &filter);
  if (path.isEmpty())
    return;
  if (QFileInfo(path).suffix().isEmpty())
    path += filter == xlsxFilter ? ".xlsx" : filter == odsFilter ? ".ods" : ".csv";
  QString error;
  if (!m_log->writeAny(path, &error))
    QMessageBox::warning(this, tr("Export readings"), error);
}

void ReadingLogWid::copySLOT()
{
  QList<int> rows;
  for (const QModelIndex &index : m_view->selectionModel()->selectedRows())
    rows << index.row();
  QApplication::clipboard()->setText(m_log->toText(rows));
}

bool ReadingLogWid::eventFilter(QObject *watched, QEvent *event)
{
  if (watched == m_view->viewport() && event->type() == QEvent::Resize)
    fitColumns();
  if (watched == m_view && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress))
  {
    QKeyEvent *key = static_cast<QKeyEvent *>(event);
    if (key->matches(QKeySequence::Copy))
    {
      if (event->type() == QEvent::KeyPress)
        copySLOT();
      event->accept();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}
