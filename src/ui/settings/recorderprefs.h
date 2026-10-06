//======================================================================
// File:		recorderprefs.h
// Author:	Matthias Toussaint
// Created:	Sat Oct 19 14:34:19 CEST 2002
//----------------------------------------------------------------------
// This file is part of QtDMM.
//
// QtDMM is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 3
// as published by the Free Software Foundation.
//
// QtDMM is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
//----------------------------------------------------------------------
// Copyright (c) 2002 Matthias Toussaint
//======================================================================

#pragma once

#include "ui/settings/settingspage.h"
#include "ui/views/graphwidget.h"

class QComboBox;
class QFormLayout;
class QLineEdit;
class QRadioButton;
class QSpinBox;
class QTimeEdit;

/// Settings page "Recording": the export grid (sample every) and how a
/// recording starts (GraphWidget::SampleMode) with its thresholds and
/// pre-trigger.
///
/// The recording length (Sample/time) is not on the page: the window asks
/// for it when a recording is started by hand (RecordLengthDlg). The page
/// keeps it, so OK writes it back as it was.
class RecorderPrefs : public SettingsPage
{
  Q_OBJECT
public:
  RecorderPrefs(QWidget *parent = Q_NULLPTR);
  ~RecorderPrefs();
  GraphWidget::SampleMode sampleMode() const;
  int         sampleStep() const;
  /// The recording length in tenths of a second, 0 = until stopped.
  int         sampleLength() const;
  /// The recording length as stored: a value (0 = until stopped) and its
  /// unit (0 s, 1 min, 2 h, 3 d).
  int         lengthValue() const { return m_lengthValue; }
  int         lengthUnit() const { return m_lengthUnit; }
  void        setLength(int value, int unit);
  /// The pre-trigger time in ms, 0 when it is off.
  int         preTrigger() const;
  double      fallingThreshold() const;
  double      raisingThreshold() const;
  QTime       startTime() const;
  /// Follows the trigger threshold line dragged in the graph.
  void        setThreshold(double);

public Q_SLOTS:
  virtual void  defaultsSLOT()Q_DECL_OVERRIDE;
  virtual void  factoryDefaultsSLOT()Q_DECL_OVERRIDE;
  virtual void  applySLOT()Q_DECL_OVERRIDE;
  void          setSampleTimeSLOT(int sampleTime);

private:
  /// The fields of the start chosen, the threshold of the edge chosen.
  void          updateRows();

  QFormLayout  *m_form;
  QSpinBox     *sampleEvery, *ui_preTriggerTime;
  QComboBox    *ui_sampleUnit, *ui_edge, *ui_preTriggerUnit;
  QRadioButton *manualBut, *predefinedBut, *triggerBut;
  QTimeEdit    *ui_startTime;
  QLineEdit    *ui_raisingThreshold, *ui_fallingThreshold;
  QCheckBox    *ui_preTrigger;
  QWidget      *m_thresholdRow, *m_preRow;
  int           m_lengthValue = 500;
  int           m_lengthUnit = 0;
};
