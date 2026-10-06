//======================================================================
// File:		graphprefs.h
// Author:	Matthias Toussaint
// Created:	Sat Oct 19 15:28:30 CEST 2002
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

class ColorButton;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QLineEdit;
class QRadioButton;
class QSpinBox;

/// Settings page "Graph": colours, line and point style of the curve, the
/// Y axis (automatic or fixed), the visible window, the crosshair and the
/// integration curve (a section that opens when it is switched on).
class GraphPrefs : public SettingsPage
{
  Q_OBJECT
public:
  GraphPrefs(QWidget *parent = Q_NULLPTR);
  ~GraphPrefs();
  /// The graphs' default colours, a GraphWidget::variantName().
  QString       variant() const;
  QColor        bgColor() const;
  QColor        gridColor() const;
  QColor        dataColor() const;
  QColor        startColor() const;
  QColor        cursorColor() const;
  int           lineWidth() const;
  int           lineMode() const;
  int           pointMode() const;
  bool          crosshair() const;

  /// @name Y axis and the visible window
  /// @{
  bool          automaticScale() const;
  bool          includeZero() const;
  double        scaleMin() const;
  double        scaleMax() const;
  int           windowSeconds() const;
  /// @}

  /// @name Integration curve
  /// @{
  bool          showIntegration() const;
  QColor        intColor() const;
  QColor        intThresholdColor() const;
  int           intLineWidth() const;
  int           intLineMode() const;
  int           intPointMode() const;
  double        intScale() const;
  double        intThreshold() const;
  double        intOffset() const;
  void          setIntThreshold(double value);
  /// @}

public Q_SLOTS:
  virtual void  defaultsSLOT();
  virtual void  factoryDefaultsSLOT();
  virtual void  applySLOT();

  void          zoomInSLOT(double fac);
  void          zoomOutSLOT(double fac);
  /// Visible window from a time button: whole minutes in minutes, else seconds.
  void          setWindowSecondsSLOT(int seconds);

private:
  /// Shows what goes with the choices: the own colours with Custom, the
  /// limits with a fixed Y axis, the integration curve when it is on.
  void          updateRows();

  QFormLayout  *m_form;
  QComboBox    *m_variant;
  ColorButton  *ui_bgColor, *ui_gridColor, *ui_dataColor, *ui_cursorColor, *ui_startColor;
  QWidget      *m_ownColours;   ///< data, background, grid: only with Custom
  QComboBox    *ui_lineMode, *ui_pointMode;
  QSpinBox     *ui_lineWidth;
  QRadioButton *autoScaleBut, *manualScaleBut;
  QCheckBox    *ui_includeZero, *ui_crosshair;
  QLineEdit    *ui_scaleMin, *ui_scaleMax;
  QSpinBox     *ui_winSize;
  QComboBox    *sizeUnit;

  QCheckBox    *ui_showInt;
  QList<QWidget *> m_intRows;
  ColorButton  *ui_intColor, *ui_intThresholdColor;
  QComboBox    *ui_intLineMode, *ui_intPointMode;
  QSpinBox     *ui_intLineWidth;
  QLineEdit    *ui_intScale, *ui_intOffset, *ui_intThreshold;
};
