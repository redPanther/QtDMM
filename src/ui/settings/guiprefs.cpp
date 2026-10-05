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
#include "core/settings.h"

GuiPrefs::GuiPrefs(QWidget *parent) : SettingsPage(parent)
{
  setupUi(this);
  ui_design->addItem(tr("System"), QString("system"));
  ui_design->addItem(tr("Silver"), QString("silver"));
  ui_design->addItem(tr("Dark"), QString("dark"));
  m_label = tr("General");
  m_description = tr("<b>QtDMM's appearance and behaviour, and the programs it runs.</b>");
  m_iconName = "preferences-desktop-theme-global";
  ui_iconSet->addItem(tr("Coloured"), QString("colored"));
  ui_iconSet->addItem(tr("Plain"), QString("plain"));
  // only where the desktop has an icon theme (not on Windows, macOS)
  if (Designs::systemIconsAvailable())
    ui_iconSet->addItem(tr("System"), QString("system"));

  // the programs QtDMM runs: sigrok-cli for the meters it reads through sigrok
  auto *programs = new QGroupBox(tr("Programs"), this);
  auto *row = new QHBoxLayout(programs);
  auto *label = new QLabel(tr("&sigrok-cli:"), programs);
  ui_sigrokExe = new QLineEdit(programs);
  ui_sigrokExe->setObjectName("ui_sigrokExe");
  ui_sigrokExe->setPlaceholderText("sigrok-cli");
  ui_sigrokExe->setToolTip(tr("The program QtDMM runs for the meters it reads through sigrok; "
                              "a name is searched in the PATH."));
  label->setBuddy(ui_sigrokExe);
  auto *browse = new QToolButton(programs);
  browse->setObjectName("ui_sigrokExeButton");
  browse->setIcon(QIcon::fromTheme("document-open"));
  browse->setToolTip(tr("Choose sigrok-cli"));
  connect(browse, &QToolButton::clicked, this, [this]
  {
    QString filter = "sigrok-cli";
#ifdef Q_OS_WIN
    filter += ".exe";
#endif
    const QString file = QFileDialog::getOpenFileName(this, tr("Sigrok-cli executable"), QString(),
                                                      QString("sigrok (%1)").arg(filter));
    if (!file.isEmpty())
      ui_sigrokExe->setText(file);
  });
  row->addWidget(label);
  row->addWidget(ui_sigrokExe, 1);
  row->addWidget(browse);
  // above the spacer at the end
  auto *page = qobject_cast<QBoxLayout *>(layout());
  page->insertWidget(page->count() - 1, programs);
}

QString GuiPrefs::sigrokExecutable() const
{
  const QString exe = ui_sigrokExe->text().trimmed();
  return exe.isEmpty() ? QStringLiteral("sigrok-cli") : exe;
}
GuiPrefs::~GuiPrefs()
{
}

void GuiPrefs::defaultsSLOT()
{
  ui_sigrokExe->setText(m_cfg->getString("Port settings/sigrok_exe", "sigrok-cli"));
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
  // Icons/system-theme was the checkbox before the sets (26.2)
  QString set = m_cfg->getString("Icons/set");
  if (set.isEmpty())
    set = m_cfg->getBool("Icons/system-theme", false) ? "system" : "colored";
  ui_iconSet->setCurrentIndex(qMax(0, ui_iconSet->findData(set)));

  ui_dmmToolBar->setChecked(m_cfg->getBool("Toolbar/dmm", true));
  ui_graphToolBar->setChecked(m_cfg->getBool("Toolbar/graph", true));
  ui_fileToolBar->setChecked(m_cfg->getBool("Toolbar/file", true));


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
  ui_iconSet->setCurrentIndex(ui_iconSet->findData(QString("colored")));

  ui_dmmToolBar->setChecked(true);
  ui_graphToolBar->setChecked(true);
  ui_fileToolBar->setChecked(true);


  ui_meterScale->setCurrentIndex(0);
  ui_meterStyle->setCurrentIndex(1);   // classic ivory
  ui_meterBallistics->setChecked(true);
  ui_meterRedZone->setValue(90);
  ui_sigrokExe->setText("sigrok-cli");
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
  m_cfg->setBool("Save/window-pos", saveWindowPosition());
  m_cfg->setBool("Save/window-size", saveWindowSize());
  m_cfg->setBool("Display/show", showDisplay());
  m_cfg->setColor("Display/display-background", ui_bgColorDisplay->color());
  m_cfg->setBool("Display/display-bar", showBar());
  m_cfg->setBool("Display/display-min-max", showMinMax());
  m_cfg->setString("Windows/design", ui_design->currentData().toString());
  m_cfg->setBool("Alert/unsaved-file", alertUnsavedData());
  m_cfg->setBool("Icons/text-label", useTextLabel());
  m_cfg->setString("Icons/set", iconSet());
  m_cfg->setBool("Toolbar/dmm", showDmmToolbar());
  m_cfg->setBool("Toolbar/graph", showGraphToolbar());
  m_cfg->setBool("Toolbar/file", showFileToolbar());
  m_cfg->setInt("Meter/scale-mode", meterScaleMode());
  m_cfg->setInt("Meter/style", meterStyle());
  m_cfg->setBool("Meter/ballistics", meterBallistics());
  m_cfg->setInt("Meter/red-zone", meterRedZone());
  m_cfg->setString("Port settings/sigrok_exe", sigrokExecutable());
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

QString GuiPrefs::iconSet() const
{
  return ui_iconSet->currentData().toString();
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

