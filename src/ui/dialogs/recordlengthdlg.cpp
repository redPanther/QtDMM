// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/recordlengthdlg.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

RecordLengthDlg::RecordLengthDlg(int value, int unit, const QString &startHint, QWidget *parent) : QDialog(parent)
{
  setWindowTitle(tr("Start recording"));
  auto *layout = new QVBoxLayout(this);
  layout->setSpacing(fontMetrics().height() / 2);

  QString text = tr("How long should it record? Afterwards the recording stands in the graph to look at and to save.");
  if (!startHint.isEmpty())
    text += "<p>" + startHint;
  auto *intro = new QLabel(text, this);
  intro->setObjectName("ui_intro");
  intro->setWordWrap(true);
  layout->addWidget(intro);

  auto *grid = new QGridLayout;
  grid->setContentsMargins(fontMetrics().horizontalAdvance('x') * 2, 0, 0, 0);
  m_forLength = new QRadioButton(tr("&For"), this);
  m_forLength->setObjectName("ui_forLength");
  m_value = new QSpinBox(this);
  m_value->setObjectName("ui_length");
  m_value->setRange(1, 99999);
  m_value->setValue(value > 0 ? value : 10);
  m_unit = new QComboBox(this);
  m_unit->setObjectName("ui_lengthUnit");
  m_unit->addItems({ tr("Seconds"), tr("Minutes"), tr("Hours"), tr("Days") });
  m_unit->setCurrentIndex(qBound(0, unit, 3));
  m_untilStop = new QRadioButton(tr("&Until Stop"), this);
  m_untilStop->setObjectName("ui_untilStop");
  m_untilStop->setToolTip(tr("Records until you press Stop (Space)."));
  grid->addWidget(m_forLength, 0, 0);
  grid->addWidget(m_value, 0, 1);
  grid->addWidget(m_unit, 0, 2);
  grid->addWidget(m_untilStop, 1, 0, 1, 3);
  grid->setColumnStretch(3, 1);
  layout->addLayout(grid);
  auto *group = new QButtonGroup(this);
  group->addButton(m_forLength);
  group->addButton(m_untilStop);
  (value > 0 ? m_forLength : m_untilStop)->setChecked(true);
  auto enable = [this] { m_value->setEnabled(m_forLength->isChecked()); m_unit->setEnabled(m_forLength->isChecked()); };
  connect(m_forLength, &QRadioButton::toggled, this, enable);
  enable();
  // typing a length means a length
  connect(m_value, &QSpinBox::valueChanged, this, [this] { m_forLength->setChecked(true); });

  layout->addStretch(1);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("&Record"));
  buttons->button(QDialogButtonBox::Ok)->setDefault(true);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  (value > 0 ? static_cast<QWidget *>(m_value) : m_untilStop)->setFocus();
  m_value->selectAll();
  setMinimumWidth(fontMetrics().horizontalAdvance('x') * 52);
}

int RecordLengthDlg::value() const
{
  return m_untilStop->isChecked() ? 0 : m_value->value();
}

int RecordLengthDlg::unit() const
{
  return m_unit->currentIndex();
}
