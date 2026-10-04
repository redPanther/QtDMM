//======================================================================
// File:		recorderprefs.cpp
// Author:	Matthias Toussaint
// Created:	Sat Oct 19 14:59:20 CEST 2002
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
#include <limits>

#include "ui/engnumbervalidator.h"
#include "ui/settings/recorderprefs.h"
#include  "core/settings.h"

#define MINUTE_SECS   60
#define HOUR_SECS     60*60
#define DAY_SECS      60*60*24

RecorderPrefs::RecorderPrefs(QWidget *parent) : SettingsPage(parent)
{
  setupUi(this);
  m_label = tr("Recording");
  m_description = tr("<b>Here you can configure the sampling"
                     " frequency and start options for the"
                     " recorder.</b>");
  m_iconName = "media-record";

  EngNumberValidator *validator = new EngNumberValidator(this);

  ui_raisingThreshold->setValidator(validator);
  ui_fallingThreshold->setValidator(validator);

  // the pre-trigger belongs to the threshold start, its time to the box
  auto enablePre = [this]
  {
    ui_preTrigger->setEnabled(triggerBut->isChecked());
    const bool on = triggerBut->isChecked() && ui_preTrigger->isChecked();
    ui_preTriggerTime->setEnabled(on);
    ui_preTriggerUnit->setEnabled(on);
  };
  connect(triggerBut, &QRadioButton::toggled, this, enablePre);
  connect(ui_preTrigger, &QCheckBox::toggled, this, enablePre);
  enablePre();
}
RecorderPrefs::~RecorderPrefs()
{
}

void RecorderPrefs::defaultsSLOT()
{
  sampleEvery->setValue(m_cfg->getInt("Sample/rate", 1));
  ui_sampleUnit->setCurrentIndex(m_cfg->getInt("Sample/rate-unit", 1));
  sampleTime->setValue(m_cfg->getInt("Sample/time", 500));
  timeUnit->setCurrentIndex(m_cfg->getInt("Sample/time-unit"));

  GraphWidget::SampleMode mode = static_cast<GraphWidget::SampleMode>(m_cfg->getInt("Start/mode"));
  if (mode == GraphWidget::Manual)
    manualBut->setChecked(true);
  else if (mode == GraphWidget::Time)
    predefinedBut->setChecked(true);
  if (mode == GraphWidget::Raising)
  {
    triggerBut->setChecked(true);
    raisingBut->setChecked(true);
  }
  if (mode == GraphWidget::Falling)
  {
    triggerBut->setChecked(true);
    fallingBut->setChecked(true);
  }

  hour->setValue(m_cfg->getInt("Start/hour"));
  minute->setValue(m_cfg->getInt("Start/minute"));
  second->setValue(m_cfg->getInt("Start/second"));
  ui_raisingThreshold->setText(m_cfg->getString("Start/raising-threshold", "0.0"));
  ui_fallingThreshold->setText(m_cfg->getString("Start/falling-threshold", "0.0"));
  ui_preTrigger->setChecked(m_cfg->getBool("Start/pre-trigger", false));
  ui_preTriggerTime->setValue(m_cfg->getInt("Start/pre-trigger-time", 10));
  ui_preTriggerUnit->setCurrentIndex(m_cfg->getInt("Start/pre-trigger-unit", 0));
}

void RecorderPrefs::factoryDefaultsSLOT()
{
  sampleEvery->setValue(1);
  ui_sampleUnit->setCurrentIndex(1);
  sampleTime->setValue(500);
  timeUnit->setCurrentIndex(0);

  manualBut->setChecked(true);

  hour->setValue(0);
  minute->setValue(0);
  second->setValue(0);
  ui_raisingThreshold->setText("0.0");
  ui_fallingThreshold->setText("0.0");
  ui_preTrigger->setChecked(false);
  ui_preTriggerTime->setValue(10);
  ui_preTriggerUnit->setCurrentIndex(0);
}

void RecorderPrefs::applySLOT()
{
  m_cfg->setInt("Sample/rate", sampleEvery->value());
  m_cfg->setInt("Sample/rate-unit", ui_sampleUnit->currentIndex());
  m_cfg->setInt("Sample/time", sampleTime->value());
  m_cfg->setInt("Sample/time-unit", timeUnit->currentIndex());

  m_cfg->setInt("Start/mode", sampleMode());
  m_cfg->setInt("Start/hour", hour->value());
  m_cfg->setInt("Start/minute", minute->value());
  m_cfg->setInt("Start/second", second->value());
  m_cfg->setString("Start/raising-threshold", ui_raisingThreshold->text());
  m_cfg->setString("Start/falling-threshold", ui_fallingThreshold->text());
  m_cfg->setBool("Start/pre-trigger", ui_preTrigger->isChecked());
  m_cfg->setInt("Start/pre-trigger-time", ui_preTriggerTime->value());
  m_cfg->setInt("Start/pre-trigger-unit", ui_preTriggerUnit->currentIndex());
}

GraphWidget::SampleMode RecorderPrefs::sampleMode() const
{
  if (predefinedBut->isChecked())
    return GraphWidget::Time;
  if (triggerBut->isChecked())
  {
    if (raisingBut->isChecked())
      return GraphWidget::Raising;
    return GraphWidget::Falling;
  }
  return GraphWidget::Manual;
}

int RecorderPrefs::sampleStep() const
{
  int thenthOfSec = sampleEvery->text().toInt();

  switch (ui_sampleUnit->currentIndex())
  {
    case 1: return thenthOfSec * 10;
    case 2: return thenthOfSec * 10 * MINUTE_SECS;
    case 3: return thenthOfSec * 10 * HOUR_SECS;
    case 4: return thenthOfSec * 10 * DAY_SECS;
  }
  return thenthOfSec;
}

int RecorderPrefs::sampleLength() const
{
  int thenthOfSec = sampleTime->text().toInt();

  switch (timeUnit->currentIndex())
  {
    case 0: return thenthOfSec * 10;
    case 1: return thenthOfSec * 10 * MINUTE_SECS;
    case 2: return thenthOfSec * 10 * HOUR_SECS;
    case 3: return thenthOfSec * 10 * DAY_SECS ;
  }
  return thenthOfSec;
}

int RecorderPrefs::preTrigger() const
{
  if (!ui_preTrigger->isChecked())
    return 0;
  static const int unitSeconds[] = { 1, MINUTE_SECS, HOUR_SECS, DAY_SECS };
  const qint64 ms = qint64(ui_preTriggerTime->value()) * unitSeconds[qBound(0, ui_preTriggerUnit->currentIndex(), 3)] * 1000;
  return int(qMin<qint64>(ms, std::numeric_limits<int>::max()));
}

double RecorderPrefs::fallingThreshold() const
{
  return EngNumberValidator::value(ui_fallingThreshold->text());
}

double RecorderPrefs::raisingThreshold() const
{
  return EngNumberValidator::value(ui_raisingThreshold->text());
}

void RecorderPrefs::setThreshold(double value)
{
  if (raisingBut->isChecked())
    ui_raisingThreshold->setText(EngNumberValidator::engText(value));
  else
    ui_fallingThreshold->setText(EngNumberValidator::engText(value));
}

QTime RecorderPrefs::startTime() const
{
  QTime time(hour->value(), minute->value(), second->value());

  return time;
}

void RecorderPrefs::setSampleTimeSLOT(int sampleTime)
{
  sampleEvery->setValue(sampleTime);
}
