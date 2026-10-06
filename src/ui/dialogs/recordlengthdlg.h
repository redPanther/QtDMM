// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QComboBox;
class QRadioButton;
class QSpinBox;

/// Asked when a recording is started by hand: how long it records.
///
/// Holds the last length (Sample/time with its unit); Enter starts with it.
/// "Until Stop" is a choice of its own (value 0).
class RecordLengthDlg : public QDialog
{
  Q_OBJECT
public:
  /// @p unit: 0 seconds, 1 minutes, 2 hours, 3 days; @p startHint a line on
  /// a start the settings set up (clock time, threshold), empty for none.
  RecordLengthDlg(int value, int unit, const QString &startHint = QString(), QWidget *parent = nullptr);
  /// The length, 0 = until Stop.
  int value() const;
  int unit() const;

private:
  QRadioButton *m_untilStop, *m_forLength;
  QSpinBox     *m_value;
  QComboBox    *m_unit;
};
