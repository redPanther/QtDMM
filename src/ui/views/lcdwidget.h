//======================================================================
// File:		displaywid.h
// Author:	Matthias Toussaint
// Created:	Thu Dec 26 12:04:35 CET 2002
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

#include <QWidget>
#include <QColor>
#include <QPixmap>

#include "core/sampletypes.h"

/// The digital display: an LCD panel in the same housing as the analog
/// meter. Seven-segment digits, unit, annunciators (HOLD, AUTO, MANU, AC,
/// DC, diode, continuity), the min/max memory, a bar graph and up to three
/// secondary values are all drawn with QPainter and scale with the widget.
/// Unlit segments and annunciators stay faintly visible, like on a real LCD.
struct Reading;

class LcdWidget : public QWidget
{
  Q_OBJECT
public:
  /// Width/height range the LCD panel keeps; beyond it the panel stays
  /// centred with room around it.
  static constexpr double kMinAspect = 1.8;
  static constexpr double kMaxAspect = 3.2;
  /// The LCD's colours (context menu "LCD colours", Display/lcd). Custom is
  /// the tint from the Appearance page, with dark segments, as before the
  /// variants.
  enum LcdVariant
  {
    Classic,        ///< yellow-green reflective LCD, the old default tint
    BacklightBlue,  ///< negative LCD: light segments on a blue backlight
    Amber,          ///< amber backlight, dark segments
    HighContrast,   ///< black on white
    Custom          ///< the tint from the settings (setFaceColor)
  };

  LcdWidget(QWidget *parent = nullptr);

  /// Value text as the meter shows it; id 0 is the main display, 1..3 the
  /// secondary values of multi-line meters.
  void setValue(int id, const QString &);
  void setUnit(int id, const QString &);
  /// The annunciators of value @p id: AC, DC and diode from @p flags
  /// (SampleFlag), the continuity buzzer from @p quantity.
  void setMode(int id, quint32 flags, Quantity quantity = Quantity::Unknown);
  /// @name Min/max memory row
  /// @{
  void setMinValue(const QString &);
  void setMaxValue(const QString &);
  void setMinUnit(const QString &);
  void setMaxUnit(const QString &);
  /// @}
  /// Layout: digit count from the display counts, which rows to show and how
  /// many secondary values.
  void setDisplayMode(int counts, bool minMax, bool bar, int numValues);
  void setShowBar(bool);   ///< per-reading flag from the decoder
  /// @name Annunciators
  /// @{
  void setHold(bool);
  void setAuto(bool);
  void setManu(bool);
  /// @}
  /// The tint for Custom (Appearance page); the others bring their own.
  void setFaceColor(const QColor &);
  void setLcdVariant(LcdVariant variant);
  LcdVariant lcdVariant() const { return m_variant; }
  /// "classic", "blue", "amber", "contrast", "custom"
  static QString lcdVariantName(LcdVariant variant);
  static LcdVariant lcdVariantFromName(const QString &name);
  /// The Appearance page's default tint (Classic's face).
  static QColor classicFace();

public Q_SLOTS:
  /// @name Fed by the MeterController
  /// @{
  /// A reading: annunciators, value and unit of its line; a held reading
  /// keeps the old value.
  void showReading(const Reading &reading);
  /// A new minimum/maximum for the MIN/MAX row (@p value is not used).
  void showMinimum(double value, const QString &text, const QString &unit);
  void showMaximum(double value, const QString &text, const QString &unit);
  void clearMinMax();
  /// @}

public:

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

  /// Seven-segment bit mask for a character, 0 for unknown (blank).
  /// Bits: 0 a (top), 1 b, 2 c, 3 d (bottom), 4 e, 5 f, 6 g (middle).
  static int segmentsFor(QChar ch);

  /// Where the rows sit and how big their glyphs are - all sizes are
  /// derived from the row height *and* the width, so nothing overlaps on
  /// a narrow panel (test_display checks the width constraints).
  struct Layout
  {
    QRectF bezel, face;
    QRectF flags, main, minMax, bar, extra;
    double digitH = 0;            // main digit height
    double smallH = 0;            // secondary digit height (MIN/MAX, second value)
    double flagsPx = 0;           // annunciator font size
    double minMaxBlockW = 0;      // width of one MIN/MAX block at smallH
  };
  Layout layout() const;
  /// Width the annunciator row needs at font size @p fontPx (all flags
  /// with their gaps), so layout() can shrink the font to fit.
  double flagsWidth(double fontPx) const;
  /// Width of one MIN/MAX block (label, number, unit) at digit height @p h.
  double minMaxBlockWidth(double h) const;

protected:
  void paintEvent(QPaintEvent *) override;
  void resizeEvent(QResizeEvent *) override;

private:
  int numDigits() const;
  void renderStatic();
  void drawFlags(QPainter &p, const Layout &l) const;
  void drawMain(QPainter &p, const Layout &l) const;
  void drawMinMax(QPainter &p, const Layout &l) const;
  void drawBar(QPainter &p, const Layout &l, bool staticPart) const;
  void drawExtra(QPainter &p, const Layout &l) const;

  /// Seven-segment rendering. Returns the width used.
  double drawNumber(QPainter &p, const QPointF &origin, double h, const QString &text, int minDigits, bool lit) const;
  void drawDigit(QPainter &p, const QPointF &origin, double h, int mask) const;
  double drawUnit(QPainter &p, const QPointF &origin, double h, const QString &unit) const;
  double numberWidth(double h, int digits) const;
  void drawAnnunciator(QPainter &p, const QRectF &r, const QString &text, bool on, double fontPx) const;

  QColor m_face;
  QColor m_segment;
  QColor m_customFace;             ///< setFaceColor()
  LcdVariant m_variant = Classic;
  void applyVariant();
  QColor ghost() const;

  QString m_value[4];
  QString m_unit[4];
  quint32  m_flags[4] = {};
  Quantity m_quantity[4] = { Quantity::Unknown, Quantity::Unknown, Quantity::Unknown, Quantity::Unknown };
  QString m_minValue, m_maxValue, m_minUnit, m_maxUnit;
  int m_counts = 4000;
  bool m_showMinMax = true;
  bool m_showBar = true;
  bool m_paintBar = false;
  int m_numValues = 1;
  bool m_hold = false;
  bool m_auto = false;
  bool m_manu = false;

  QPixmap m_static;
  bool m_staticDirty = true;
};
