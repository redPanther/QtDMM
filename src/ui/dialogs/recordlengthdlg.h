// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QComboBox;
class QSpinBox;

/// Asked when a recording is started by hand: how long it records.
///
/// Holds the last length (Sample/time with its unit); Enter starts with it.
/// 0 records until Stop.
class RecordLengthDlg : public QDialog
{
  Q_OBJECT
public:
  /// @p unit: 0 seconds, 1 minutes, 2 hours, 3 days.
  RecordLengthDlg(int value, int unit, QWidget *parent = nullptr);
  int value() const;
  int unit() const;

private:
  QSpinBox  *m_value;
  QComboBox *m_unit;
};
