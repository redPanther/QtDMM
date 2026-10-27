// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/limitdlg.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

#include "ui/alarmsound.h"
#include "ui/engnumbervalidator.h"

namespace
{
QComboBox *soundBox(const QString &current, QWidget *parent)
{
  auto *box = new QComboBox(parent);
  box->addItem(LimitDlg::tr("none"), QString());
  box->addItem(LimitDlg::tr("System beep"), QStringLiteral("beep"));
#ifdef QTDMM_WITH_MULTIMEDIA
  for (const auto &[name, shown] : AlarmSound::builtIn())
    box->addItem(shown, name);
#endif
  // a WAV file of an alarm, or a sound this build cannot play: kept
  if (box->findData(current) < 0)
    box->addItem(current, current);
  box->setCurrentIndex(box->findData(current));
  return box;
}

double number(const QLineEdit *edit)
{
  return edit->text().isEmpty() ? 0.0 : EngNumberValidator::value(edit->text());
}
}

LimitDlg::LimitDlg(const LimitTest &test, const QString &unit, double reading, QWidget *parent) :
  QDialog(parent),
  m_test(test),
  m_unit(unit)
{
  setWindowTitle(tr("Limit test"));
  auto *layout = new QVBoxLayout(this);
  auto *form = new QFormLayout;
  layout->addLayout(form);

  m_enabled = new QCheckBox(tr("&Check the limits (PASS/FAIL)"), this);
  m_enabled->setChecked(test.enabled);
  form->addRow(m_enabled);

  m_form = new QComboBox(this);
  m_form->addItem(tr("Nominal ± percent"), LimitTest::Percent);
  m_form->addItem(tr("Nominal ± amount"), LimitTest::Amount);
  m_form->addItem(tr("Lower and upper limit"), LimitTest::Range);
  m_form->setCurrentIndex(m_form->findData(test.form));
  form->addRow(tr("&Form:"), m_form);

  auto *validator = new EngNumberValidator(this);
  auto edit = [&](double v)
  {
    auto *e = new QLineEdit(EngNumberValidator::engText(v), this);
    e->setValidator(validator);
    connect(e, &QLineEdit::textChanged, this, &LimitDlg::updateFields);
    return e;
  };
  m_nominal = edit(test.nominal);
  m_take = new QPushButton(tr("= &reading"), this);
  m_take->setToolTip(tr("The value shown now as the nominal"));
  m_take->setEnabled(!std::isnan(reading));
  connect(m_take, &QPushButton::clicked, this, [this, reading] { m_nominal->setText(EngNumberValidator::engText(reading)); });
  auto *nominalRow = new QHBoxLayout;
  nominalRow->setContentsMargins(0, 0, 0, 0);
  nominalRow->addWidget(m_nominal, 1);
  nominalRow->addWidget(m_take);
  m_nominalLabel = new QLabel(this);
  m_nominalLabel->setBuddy(m_nominal);
  form->addRow(m_nominalLabel, nominalRow);
  m_tolerance = edit(test.tolerance);
  m_tolLabel = new QLabel(this);
  m_tolLabel->setBuddy(m_tolerance);
  form->addRow(m_tolLabel, m_tolerance);
  m_lower = edit(test.lower);
  m_lowerLabel = new QLabel(this);
  m_lowerLabel->setBuddy(m_lower);
  form->addRow(m_lowerLabel, m_lower);
  m_upper = edit(test.upper);
  m_upperLabel = new QLabel(this);
  m_upperLabel->setBuddy(m_upper);
  form->addRow(m_upperLabel, m_upper);
  m_limits = new QLabel(this);
  form->addRow(tr("PASS:"), m_limits);

  m_tint = new QCheckBox(tr("Display in &green or red"), this);
  m_tint->setToolTip(tr("The display takes the verdict's colour; a raised alarm's colour comes first."));
  m_tint->setChecked(test.tint);
  form->addRow(m_tint);
  // HI, LO and PASS each a sound, repeated while the verdict stays if wanted
  auto soundRow = [&](const QString &label, const QString &current, bool repeat, QComboBox *&box, QCheckBox *&check)
  {
    box = soundBox(current, this);
    check = new QCheckBox(tr("repeat"), this);
    check->setToolTip(tr("Again every second while the verdict stays"));
    check->setChecked(repeat);
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(box, 1);
    row->addWidget(check);
    auto *l = new QLabel(label, this);
    l->setBuddy(box);
    form->addRow(l, row);
  };
  soundRow(tr("Sound on &HI:"), test.highSound, test.highRepeat, m_highSound, m_highRepeat);
  soundRow(tr("Sound on L&O:"), test.lowSound, test.lowRepeat, m_lowSound, m_lowRepeat);
  soundRow(tr("Sound on &PASS:"), test.passSound, test.passRepeat, m_passSound, m_passRepeat);

  auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(bb);

  connect(m_form, qOverload<int>(&QComboBox::currentIndexChanged), this, &LimitDlg::updateFields);
  updateFields();
}

void LimitDlg::updateFields()
{
  const auto f = LimitTest::Form(m_form->currentData().toInt());
  const QString u = m_unit.isEmpty() ? QString() : QString(" (%1)").arg(m_unit);
  m_nominalLabel->setText(tr("&Nominal%1:").arg(u));
  m_tolLabel->setText(f == LimitTest::Percent ? tr("&Tolerance (± %):") : tr("&Tolerance (±)%1:").arg(u));
  m_lowerLabel->setText(tr("&Lower limit%1:").arg(u));
  m_upperLabel->setText(tr("&Upper limit%1:").arg(u));
  const bool range = f == LimitTest::Range;
  for (QWidget *w : { static_cast<QWidget *>(m_nominal), static_cast<QWidget *>(m_nominalLabel),
                      static_cast<QWidget *>(m_take), static_cast<QWidget *>(m_tolerance), static_cast<QWidget *>(m_tolLabel) })
    w->setVisible(!range);
  for (QWidget *w : { static_cast<QWidget *>(m_lower), static_cast<QWidget *>(m_lowerLabel),
                      static_cast<QWidget *>(m_upper), static_cast<QWidget *>(m_upperLabel) })
    w->setVisible(range);
  // the limits as the test will use them
  m_limits->setText(limitTest().describe(m_unit));
}

LimitTest LimitDlg::limitTest() const
{
  LimitTest t = m_test;
  t.enabled = m_enabled->isChecked();
  t.form = LimitTest::Form(m_form->currentData().toInt());
  t.nominal = number(m_nominal);
  t.tolerance = number(m_tolerance);
  t.lower = number(m_lower);
  t.upper = number(m_upper);
  t.tint = m_tint->isChecked();
  t.highSound = m_highSound->currentData().toString();
  t.lowSound = m_lowSound->currentData().toString();
  t.highRepeat = m_highRepeat->isChecked();
  t.lowRepeat = m_lowRepeat->isChecked();
  t.passSound = m_passSound->currentData().toString();
  t.passRepeat = m_passRepeat->isChecked();
  return t;
}
