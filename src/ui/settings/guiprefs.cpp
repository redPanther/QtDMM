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
#include "ui/settings/guiprefs.h"
#include "core/settings.h"

GuiPrefs::GuiPrefs(QWidget *parent) : SettingsPage(parent)
{
  setupUi(this);
  ui_design->addItem(tr("System"), QString("system"));
  ui_design->addItem(tr("Silver"), QString("silver"));
  ui_design->addItem(tr("Dark"), QString("dark"));
  m_label = tr("Appearance");
  m_description = tr("<b>Here you can configure QtDMM's visual"
                     " appearance and behaviour.</b>");
  m_iconName = "preferences-desktop-theme-global";
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  ui_systemIcons->hide();   // no desktop icon themes there
#endif
}
GuiPrefs::~GuiPrefs()
{
}

void GuiPrefs::defaultsSLOT()
{
  ui_design->setCurrentIndex(qMax(0, ui_design->findData(m_cfg->getString("Windows/design", "dark"))));
  ui_saveWindowPos->setChecked(m_cfg->getBool("Save/window-pos", true));
  ui_saveWindowSize->setChecked(m_cfg->getBool("Save/window-size", true));

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

  ui_alertUnsavedData->setChecked(m_cfg->getBool("Alert/unsaved-file", true));
  ui_textLabel->setChecked(m_cfg->getBool("Icons/text-label", false));
  ui_systemIcons->setChecked(m_cfg->getBool("Icons/system-theme", false));

  ui_dmmToolBar->setChecked(m_cfg->getBool("Toolbar/dmm", true));
  ui_graphToolBar->setChecked(m_cfg->getBool("Toolbar/graph", true));
  ui_fileToolBar->setChecked(m_cfg->getBool("Toolbar/file", true));

  ui_tipOfTheDay->setChecked(m_cfg->getBool("QtDMM/show-tip", true));

  ui_meterScale->setCurrentIndex(qBound(0, m_cfg->getInt("Meter/scale-mode", 0), 2));
  ui_meterStyle->setCurrentIndex(qBound(0, m_cfg->getInt("Meter/style", 1), 1));
  ui_meterBallistics->setChecked(m_cfg->getBool("Meter/ballistics", true));
  ui_meterRedZone->setValue(qBound(50, m_cfg->getInt("Meter/red-zone", 90), 100));
}

void GuiPrefs::factoryDefaultsSLOT()
{
  ui_design->setCurrentIndex(ui_design->findData(QString("dark")));
  ui_saveWindowPos->setChecked(true);
  ui_saveWindowSize->setChecked(true);

  ui_showDisplay->setChecked(true);
  ui_bgColorDisplay->setColor(QColor(0xda, 0xdc, 0x77));

  ui_showBar->setChecked(true);
  ui_showMinMax->setChecked(false);

  ui_alertUnsavedData->setChecked(true);
  ui_textLabel->setChecked(false);
  ui_systemIcons->setChecked(false);

  ui_dmmToolBar->setChecked(true);
  ui_graphToolBar->setChecked(true);
  ui_fileToolBar->setChecked(true);

  ui_tipOfTheDay->setChecked(true);

  ui_meterScale->setCurrentIndex(0);
  ui_meterStyle->setCurrentIndex(1);   // classic ivory
  ui_meterBallistics->setChecked(true);
  ui_meterRedZone->setValue(90);
}

void GuiPrefs::setToolbarVisibility(bool disp, bool dmm, bool graph, bool file)
{
  ui_showDisplay->setChecked(disp);
  ui_dmmToolBar->setChecked(dmm);
  ui_graphToolBar->setChecked(graph);
  ui_fileToolBar->setChecked(file);
}

void GuiPrefs::applySLOT()
{
  m_cfg->setInt("QtDMM/version", 0);   // TODO set version by cmake
  m_cfg->setInt("QtDMM/revision", 84); // TODO set revision by cmake
  m_cfg->setBool("QtDMM/show-tip", showTip());
  m_cfg->setBool("Save/window-pos", saveWindowPosition());
  m_cfg->setBool("Save/window-size", saveWindowSize());
  m_cfg->setBool("Display/show", showDisplay());
  m_cfg->setColor("Display/display-background", ui_bgColorDisplay->color());
  m_cfg->setBool("Display/display-bar", showBar());
  m_cfg->setBool("Display/display-min-max", showMinMax());
  m_cfg->setString("Windows/design", ui_design->currentData().toString());
  m_cfg->setBool("Alert/unsaved-file", alertUnsavedData());
  m_cfg->setBool("Icons/text-label", useTextLabel());
  m_cfg->setBool("Icons/system-theme", systemIcons());
  m_cfg->setBool("Toolbar/dmm", showDmmToolbar());
  m_cfg->setBool("Toolbar/graph", showGraphToolbar());
  m_cfg->setBool("Toolbar/file", showFileToolbar());
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

bool GuiPrefs::showTip() const
{
  return ui_tipOfTheDay->isChecked();
}

bool GuiPrefs::showDmmToolbar() const
{
  return ui_dmmToolBar->isChecked();
}

bool GuiPrefs::showGraphToolbar() const
{
  return ui_graphToolBar->isChecked();
}

bool GuiPrefs::showFileToolbar() const
{
  return ui_fileToolBar->isChecked();
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

bool GuiPrefs::alertUnsavedData() const
{
  return ui_alertUnsavedData->isChecked();
}

bool GuiPrefs::useTextLabel() const
{
  return ui_textLabel->isChecked();
}

bool GuiPrefs::systemIcons() const
{
  return ui_systemIcons->isChecked();
}

QColor GuiPrefs::displayBgColor() const
{
  return ui_bgColorDisplay->color();
}

bool GuiPrefs::saveWindowPosition() const
{
  return ui_saveWindowPos->isChecked();
}

bool GuiPrefs::saveWindowSize() const
{
  return ui_saveWindowSize->isChecked();
}

void GuiPrefs::on_ui_tipOfTheDay_toggled(bool on)
{
  ui_tipOfTheDay->setChecked(on);
}
