// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/recordlengthdlg.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

RecordLengthDlg::RecordLengthDlg(int value, int unit, QWidget *parent) : QDialog(parent)
{
  setWindowTitle(tr("Record"));
  auto *layout = new QVBoxLayout(this);
  auto *form = new QFormLayout;
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  layout->addLayout(form);

  m_value = new QSpinBox(this);
  m_value->setObjectName("ui_length");
  m_value->setRange(0, 99999);
  m_value->setSpecialValueText(QStringLiteral("∞"));
  m_value->setValue(value);
  m_value->setToolTip(tr("How long the recording runs; ∞ (0) records until Stop."));
  m_unit = new QComboBox(this);
  m_unit->setObjectName("ui_lengthUnit");
  m_unit->addItems({ tr("Seconds"), tr("Minutes"), tr("Hours"), tr("Days") });
  m_unit->setCurrentIndex(qBound(0, unit, 3));
  auto *row = new QHBoxLayout;
  row->addWidget(m_value);
  row->addWidget(m_unit);
  auto *label = new QLabel(tr("&Length:"), this);
  label->setBuddy(m_value);
  form->addRow(label, row);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("&Record"));
  buttons->button(QDialogButtonBox::Ok)->setDefault(true);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  m_value->setFocus();
  m_value->selectAll();
  setFixedSize(sizeHint());
}

int RecordLengthDlg::value() const
{
  return m_value->value();
}

int RecordLengthDlg::unit() const
{
  return m_unit->currentIndex();
}
