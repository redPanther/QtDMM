//======================================================================
// File:		guiprefs.h
// Author:	Matthias Toussaint
// Created:	Sat Oct 19 15:29:05 CEST 2002
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
class QSpinBox;

/// Settings page "Appearance": design and symbols, the LCD display (colour,
/// bar graph, min/max) and the analog meter (scale mode, style, ballistics,
/// red zone).
class GuiPrefs : public SettingsPage
{
  Q_OBJECT
public:
  GuiPrefs(QWidget *parent = Q_NULLPTR);
  ~GuiPrefs();
  bool      showBar() const;
  bool      showMinMax() const;
  /// The symbols: "colored", "plain" or "system" (Designs::IconSet).
  QString   iconSet() const;
  QColor    displayBgColor() const;
  bool      showDisplay() const;
  void      setShowDisplay(bool show);
  int       meterScaleMode() const;   ///< 0 auto, 1 unipolar, 2 bipolar
  int       meterStyle() const;   ///< 0 dark, 1 ivory
  void      setMeterStyle(int style);
  bool      meterBallistics() const;
  int       meterRedZone() const;   ///< percent of full scale

public Q_SLOTS:
  virtual void defaultsSLOT();
  virtual void factoryDefaultsSLOT();
  virtual void applySLOT();

private:
  QComboBox   *ui_design, *ui_iconSet, *ui_meterScale, *ui_meterStyle;
  QCheckBox   *ui_showDisplay, *ui_showBar, *ui_showMinMax, *ui_meterBallistics;
  ColorButton *ui_bgColorDisplay;
  QSpinBox    *ui_meterRedZone;
};
