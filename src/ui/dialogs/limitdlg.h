// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

#include "core/limittest.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

/// The limit test (PASS/FAIL) of the display's context menu: nominal ±
/// percent, nominal ± an amount, or lower and upper limit, the colour and
/// the sounds for HI, LO and PASS (repeated if wanted). "= reading" takes the value shown as the nominal.
class LimitDlg : public QDialog
{
  Q_OBJECT
public:
  /// @p unit the value's base unit (empty before the first reading);
  /// @p reading the value shown now, NaN for none.
  LimitDlg(const LimitTest &test, const QString &unit, double reading, QWidget *parent = nullptr);
  LimitTest limitTest() const;

private:
  void updateFields();

  LimitTest m_test;   ///< what the dialog does not show (the value) stays
  QString m_unit;
  QCheckBox *m_enabled, *m_tint, *m_highRepeat, *m_lowRepeat, *m_passRepeat;
  QComboBox *m_form, *m_highSound, *m_lowSound, *m_passSound;
  QLineEdit *m_nominal, *m_tolerance, *m_lower, *m_upper;
  QLabel *m_tolLabel, *m_nominalLabel, *m_lowerLabel, *m_upperLabel, *m_limits;
  class QPushButton *m_take;
};
