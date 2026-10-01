//======================================================================
// File:		graphprefs.cpp
// Author:	Matthias Toussaint
// Created:	Sat Oct 19 15:27:46 CEST 2002
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


#include "colorbutton.h"
#include "dmmgraph.h"
#include "graphprefs.h"
#include "settings.h"


GraphPrefs::GraphPrefs(QWidget *parent) : PrefWidget(parent)
{
  setupUi(this);
  m_label = tr("Graph");
  m_description = tr("<b>Here you can configure the colors and"
                     " drawing style for the graph.</b>");
  m_iconName = "office-chart-line";

  // the graphs' colours; the colour buttons below are the variant Custom
  // (cursor and thresholds always)
  m_variant = new QComboBox(this);
  for (auto v : { DMMGraph::Neutral, DMMGraph::ScopeBlue, DMMGraph::PhosphorGreen, DMMGraph::PhosphorAmber,
                  DMMGraph::ChartRecorder, DMMGraph::Custom })
    m_variant->addItem(v == DMMGraph::Custom ? tr("Custom: the colours below")
                                             : v == DMMGraph::Neutral ? tr("Neutral: follows the design")
                                                                      : DMMGraph::variantTitle(v),
                       DMMGraph::variantName(v));
  m_variant->setWhatsThis(tr("The colours of the graph: background, grid, lettering and curves. "
                             "Custom uses the colours below. A graph's context menu can choose "
                             "other colours for that graph only."));
  auto *row = new QHBoxLayout;
  auto *label = new QLabel(tr("Graph &colours:"), this);
  label->setBuddy(m_variant);
  row->addWidget(label);
  row->addWidget(m_variant, 1);
  if (auto *box = qobject_cast<QBoxLayout *>(layout()))
    box->insertLayout(0, row);
}

QString GraphPrefs::variant() const
{
  return m_variant->currentData().toString();
}
GraphPrefs::~GraphPrefs()
{
}

void GraphPrefs::defaultsSLOT()
{
  // mt: removed .rgb()
  if (!m_cfg->fileConverted())
  {
    ui_bgColor->setColor(m_cfg->getColor("Graph/background"));
    ui_gridColor->setColor(m_cfg->getColor("Graph/grid", Qt::gray));
    ui_dataColor->setColor(m_cfg->getColor("Graph/data", Qt::blue));
    ui_cursorColor->setColor(m_cfg->getColor("Graph/cursor", Qt::black));
    ui_startColor->setColor(m_cfg->getColor("Graph/start-trigger", Qt::magenta));
    ui_extColor->setColor(m_cfg->getColor("Graph/external-trigger", Qt::cyan));
  }
  else
  {
    ui_bgColor->setColor(Qt::white);
    ui_gridColor->setColor(Qt::gray);
    ui_dataColor->setColor(Qt::blue);
    ui_cursorColor->setColor(Qt::black);
    ui_startColor->setColor(Qt::magenta);
    ui_extColor->setColor(Qt::cyan);
    m_cfg->save();
  }
  // a config from before the variants, whose colours were set here, keeps
  // them as Custom
  QString variant = m_cfg->getString("Graph/variant");
  if (variant.isEmpty())
    variant = m_cfg->getString("Graph/background").isEmpty() ? "neutral" : "custom";
  m_variant->setCurrentIndex(qMax(0, m_variant->findData(variant)));
  ui_lineMode->setCurrentIndex(m_cfg->getInt("Graph/line-mode", 1));
  ui_pointMode->setCurrentIndex(m_cfg->getInt("Graph/point-mode"));
  ui_crosshair->setChecked(m_cfg->getBool("Graph/crosshair-cursor", true));

  ui_lineWidth->setValue(m_cfg->getInt("Graph/line-width", 2));

}

void GraphPrefs::factoryDefaultsSLOT()
{
  ui_bgColor->setColor(Qt::white);
  ui_gridColor->setColor(Qt::gray);
  ui_dataColor->setColor(Qt::blue);
  ui_cursorColor->setColor(Qt::black);
  ui_startColor->setColor(Qt::magenta);   // mt: removed .rgb()
  ui_extColor->setColor(Qt::cyan);   // mt: removed .rgb()
  m_variant->setCurrentIndex(0);
  ui_lineMode->setCurrentIndex(1);
  ui_pointMode->setCurrentIndex(0);
  ui_lineWidth->setValue(2);
  ui_crosshair->setChecked(true);

}

void GraphPrefs::applySLOT()
{
  m_cfg->setColor("Graph/background", ui_bgColor->color());
  m_cfg->setColor("Graph/grid", ui_gridColor->color());
  m_cfg->setColor("Graph/data", ui_dataColor->color());
  m_cfg->setColor("Graph/cursor", ui_cursorColor->color());
  m_cfg->setColor("Graph/start-trigger", ui_startColor->color());
  m_cfg->setColor("Graph/external-trigger", ui_extColor->color());
  m_cfg->setString("Graph/variant", variant());
  m_cfg->setInt("Graph/line-width", ui_lineWidth->value());
  m_cfg->setInt("Graph/line-mode", ui_lineMode->currentIndex());
  m_cfg->setInt("Graph/point-mode", ui_pointMode->currentIndex());
  m_cfg->setBool("Graph/crosshair-cursor", ui_crosshair->isChecked());
}

QColor GraphPrefs::bgColor() const
{
  return ui_bgColor->color();
}

QColor GraphPrefs::gridColor() const
{
  return ui_gridColor->color();
}

QColor GraphPrefs::dataColor() const
{
  return ui_dataColor->color();
}

QColor GraphPrefs::startColor() const
{
  return ui_startColor->color();
}

QColor GraphPrefs::externalColor() const
{
  return ui_extColor->color();
}

QColor GraphPrefs::cursorColor() const
{
  return ui_cursorColor->color();
}

int GraphPrefs::lineWidth() const
{
  return ui_lineWidth->value();
}

int GraphPrefs::lineMode() const
{
  return ui_lineMode->currentIndex();
}

int GraphPrefs::pointMode() const
{
  return ui_pointMode->currentIndex();
}

bool GraphPrefs::crosshair() const
{
  return ui_crosshair->isChecked();
}
