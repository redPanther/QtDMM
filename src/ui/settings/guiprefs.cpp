//======================================================================
// File:		guiprefs.cpp
// Author:	Matthias Toussaint
// Created:	Sat Oct 19 15:29:34 CEST 2002
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


#include <QtGui>
#include <QtWidgets>

#include "ui/colorbutton.h"
#include "ui/designs.h"
#include "ui/settings/guiprefs.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QSpinBox>
#include "core/settings.h"

GuiPrefs::GuiPrefs(QWidget *parent) : SettingsPage(parent)
{
  m_label = tr("Appearance");
  m_description = tr("Design and symbols, the digital display and the analog meter.");
  m_iconName = "qtdmm-display";

  QFormLayout *form = createForm();

  addSection(form, tr("Design"));
  ui_design = new QComboBox(this);
  ui_design->setObjectName("ui_design");
  ui_design->addItem(tr("System"), QString("system"));
  ui_design->addItem(tr("Silver"), QString("silver"));
  ui_design->addItem(tr("Dark"), QString("dark"));
  ui_design->setToolTip(tr("The look of the whole window: the colours of the desktop (System), brushed metal "
                           "(Silver) or dark. The same as Design in the menu."));
  form->addRow(tr("D&esign:"), ui_design);
  ui_iconSet = new QComboBox(this);
  ui_iconSet->setObjectName("ui_iconSet");
  ui_iconSet->addItem(tr("Coloured"), QString("colored"));
  ui_iconSet->addItem(tr("Plain"), QString("plain"));
  // only where the desktop has an icon theme (not on Windows, macOS)
  if (Designs::systemIconsAvailable())
    ui_iconSet->addItem(tr("System"), QString("system"));
  ui_iconSet->setToolTip(tr("<b>Coloured</b>: QtDMM's coloured symbols (Oxygen). <b>Plain</b>: monochrome "
                            "symbols (Breeze), light or dark to match the design. <b>System</b>: the icon "
                            "theme of your desktop, where it has one; QtDMM's own symbols and what the theme "
                            "lacks come from the plain set."));
  form->addRow(tr("S&ymbols:"), ui_iconSet);

  addSection(form, tr("Digital display (LCD)"));
  ui_showDisplay = new QCheckBox(tr("Sh&ow"), this);
  ui_showDisplay->setObjectName("ui_showDisplay");
  ui_showDisplay->setToolTip(tr("The digital display in the window."));
  ui_showBar = new QCheckBox(tr("&Bargraph"), this);
  ui_showBar->setObjectName("ui_showBar");
  ui_showBar->setToolTip(tr("The bar under the digits, as on the meter."));
  ui_showMinMax = new QCheckBox(tr("&Min/Max"), this);
  ui_showMinMax->setObjectName("ui_showMinMax");
  ui_showMinMax->setToolTip(tr("The smallest and largest reading beside the value."));
  ui_bgColorDisplay = new ColorButton(this);
  ui_bgColorDisplay->setObjectName("ui_bgColorDisplay");
  ui_bgColorDisplay->setToolTip(tr("Tint of the LCD face of the digital display. The default is a classic "
                                   "greenish LCD."));
  form->addRow(QString(), ui_showDisplay);
  form->addRow(QString(), ui_showBar);
  form->addRow(QString(), ui_showMinMax);
  form->addRow(tr("LCD &colour:"), ui_bgColorDisplay);

  addSection(form, tr("Analog meter"));
  ui_meterScale = new QComboBox(this);
  ui_meterScale->setObjectName("ui_meterScale");
  ui_meterScale->addItems({ tr("Automatic"), tr("Zero left"), tr("Centre zero") });
  ui_meterScale->setToolTip(tr("How the analog meter lays out its scale. <b>Automatic</b> starts with zero at "
                               "the left and switches to a centre-zero scale as soon as a clearly negative "
                               "reading arrives (until the min/max memory is reset). <b>Zero left</b> and "
                               "<b>Centre zero</b> fix one layout."));
  form->addRow(tr("&Scale:"), ui_meterScale);
  ui_meterStyle = new QComboBox(this);
  ui_meterStyle->setObjectName("ui_meterStyle");
  ui_meterStyle->addItems({ tr("Dark studio"), tr("Classic ivory") });
  ui_meterStyle->setToolTip(tr("Colour scheme of the analog meter: a dark studio dial with a white scale, or "
                               "a classic ivory dial with a black scale."));
  form->addRow(tr("S&tyle:"), ui_meterStyle);
  ui_meterBallistics = new QCheckBox(tr("&Needle inertia"), this);
  ui_meterBallistics->setObjectName("ui_meterBallistics");
  ui_meterBallistics->setToolTip(tr("Move the needle with the inertia of a real moving-coil instrument instead "
                                    "of jumping to each new reading."));
  form->addRow(QString(), ui_meterBallistics);
  ui_meterRedZone = new QSpinBox(this);
  ui_meterRedZone->setObjectName("ui_meterRedZone");
  ui_meterRedZone->setRange(50, 100);
  ui_meterRedZone->setSuffix(" %");
  ui_meterRedZone->setToolTip(tr("Start of the red zone at the top end of the scale, as a percentage of full "
                                 "scale."));
  form->addRow(tr("&Red zone from:"), ui_meterRedZone);

  // the fields of a section line up
  const int width = qMax(ui_meterStyle->sizeHint().width(), ui_meterScale->sizeHint().width());
  for (QWidget *w : std::initializer_list<QWidget *>{ ui_design, ui_iconSet, ui_meterScale, ui_meterStyle })
    w->setMinimumWidth(width);

  // bargraph and min/max belong to the display
  connect(ui_showDisplay, &QCheckBox::toggled, this, [this](bool on)
  {
    ui_showBar->setEnabled(on);
    ui_showMinMax->setEnabled(on);
    ui_bgColorDisplay->setEnabled(on);
  });
}

GuiPrefs::~GuiPrefs()
{
}

void GuiPrefs::defaultsSLOT()
{
  ui_design->setCurrentIndex(qMax(0, ui_design->findData(m_cfg->getString("Windows/design", "dark"))));

  ui_showDisplay->setChecked(m_cfg->getBool("Display/show", true));
  if (!m_cfg->fileConverted())
    ui_bgColorDisplay->setColor(m_cfg->getColor("Display/display-background", QColor(0xda, 0xdc, 0x77)));
  else
  {
    ui_bgColorDisplay->setColor(QColor(0xda, 0xdc, 0x77));
    m_cfg->save();
  }
  ui_showBar->setChecked(m_cfg->getBool("Display/display-bar", true));
  ui_showMinMax->setChecked(m_cfg->getBool("Display/display-min-max", false));

  // Icons/system-theme was the checkbox before the sets (26.2)
  QString set = m_cfg->getString("Icons/set");
  if (set.isEmpty())
    set = m_cfg->getBool("Icons/system-theme", false) ? "system" : "colored";
  ui_iconSet->setCurrentIndex(qMax(0, ui_iconSet->findData(set)));



  ui_meterScale->setCurrentIndex(qBound(0, m_cfg->getInt("Meter/scale-mode", 0), 2));
  ui_meterStyle->setCurrentIndex(qBound(0, m_cfg->getInt("Meter/style", 1), 1));
  ui_meterBallistics->setChecked(m_cfg->getBool("Meter/ballistics", true));
  ui_meterRedZone->setValue(qBound(50, m_cfg->getInt("Meter/red-zone", 90), 100));
  Q_EMIT ui_showDisplay->toggled(ui_showDisplay->isChecked());
}

void GuiPrefs::factoryDefaultsSLOT()
{
  ui_design->setCurrentIndex(ui_design->findData(QString("dark")));

  ui_showDisplay->setChecked(true);
  ui_bgColorDisplay->setColor(QColor(0xda, 0xdc, 0x77));

  ui_showBar->setChecked(true);
  ui_showMinMax->setChecked(false);

  ui_iconSet->setCurrentIndex(ui_iconSet->findData(QString("colored")));



  ui_meterScale->setCurrentIndex(0);
  ui_meterStyle->setCurrentIndex(1);   // classic ivory
  ui_meterBallistics->setChecked(true);
  ui_meterRedZone->setValue(90);
}

void GuiPrefs::setShowDisplay(bool show)
{
  ui_showDisplay->setChecked(show);
}

void GuiPrefs::applySLOT()
{
  m_cfg->setInt("QtDMM/version", 0);   // TODO set version by cmake
  m_cfg->setInt("QtDMM/revision", 84); // TODO set revision by cmake
  m_cfg->setBool("Display/show", showDisplay());
  m_cfg->setColor("Display/display-background", ui_bgColorDisplay->color());
  m_cfg->setBool("Display/display-bar", showBar());
  m_cfg->setBool("Display/display-min-max", showMinMax());
  m_cfg->setString("Windows/design", ui_design->currentData().toString());
  m_cfg->setString("Icons/set", iconSet());
  m_cfg->setInt("Meter/scale-mode", meterScaleMode());
  m_cfg->setInt("Meter/style", meterStyle());
  m_cfg->setBool("Meter/ballistics", meterBallistics());
  m_cfg->setInt("Meter/red-zone", meterRedZone());
}

int GuiPrefs::meterScaleMode() const
{
  return ui_meterScale->currentIndex();
}

void GuiPrefs::setMeterStyle(int style)
{
  ui_meterStyle->setCurrentIndex(qBound(0, style, 1));
}

int GuiPrefs::meterStyle() const
{
  return ui_meterStyle->currentIndex();
}

bool GuiPrefs::meterBallistics() const
{
  return ui_meterBallistics->isChecked();
}

int GuiPrefs::meterRedZone() const
{
  return ui_meterRedZone->value();
}






bool GuiPrefs::showDisplay() const
{
  return ui_showDisplay->isChecked();
}

bool GuiPrefs::showBar() const
{
  return ui_showBar->isChecked();
}

bool GuiPrefs::showMinMax() const
{
  return ui_showMinMax->isChecked();
}


QString GuiPrefs::iconSet() const
{
  return ui_iconSet->currentData().toString();
}

QColor GuiPrefs::displayBgColor() const
{
  return ui_bgColorDisplay->color();
}

