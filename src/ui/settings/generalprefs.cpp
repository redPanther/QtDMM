// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/settings/generalprefs.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/settings.h"

GeneralPrefs::GeneralPrefs(QWidget *parent) : SettingsPage(parent)
{
  m_label = tr("General");
  m_description = tr("<b>What QtDMM does at program exit, and the programs it runs.</b>");
  m_iconName = "configure";

  auto *layout = new QVBoxLayout(this);

  auto *exitGroup = new QGroupBox(tr("At program exit"), this);
  auto *exitLayout = new QVBoxLayout(exitGroup);
  ui_alertUnsavedData = new QCheckBox(tr("&Alert unsaved data"), exitGroup);
  ui_alertUnsavedData->setObjectName("ui_alertUnsavedData");
  ui_alertUnsavedData->setToolTip(tr("Asks before unsaved recordings are lost: at exit, and before an "
                                     "import replaces them."));
  ui_saveWindowPos = new QCheckBox(tr("Save window &position"), exitGroup);
  ui_saveWindowPos->setObjectName("ui_saveWindowPos");
  ui_saveWindowPos->setToolTip(tr("The next start opens the window where it was."));
  ui_saveWindowSize = new QCheckBox(tr("Save window si&ze"), exitGroup);
  ui_saveWindowSize->setObjectName("ui_saveWindowSize");
  ui_saveWindowSize->setToolTip(tr("The next start opens the window in the size it had."));
  exitLayout->addWidget(ui_alertUnsavedData);
  exitLayout->addWidget(ui_saveWindowPos);
  exitLayout->addWidget(ui_saveWindowSize);
  layout->addWidget(exitGroup);

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
  layout->addWidget(programs);
  layout->addStretch(1);
}

bool GeneralPrefs::alertUnsavedData() const
{
  return ui_alertUnsavedData->isChecked();
}

bool GeneralPrefs::saveWindowPosition() const
{
  return ui_saveWindowPos->isChecked();
}

bool GeneralPrefs::saveWindowSize() const
{
  return ui_saveWindowSize->isChecked();
}

QString GeneralPrefs::sigrokExecutable() const
{
  const QString exe = ui_sigrokExe->text().trimmed();
  return exe.isEmpty() ? QStringLiteral("sigrok-cli") : exe;
}

void GeneralPrefs::defaultsSLOT()
{
  ui_alertUnsavedData->setChecked(m_cfg->getBool("Alert/unsaved-file", true));
  ui_saveWindowPos->setChecked(m_cfg->getBool("Save/window-pos", true));
  ui_saveWindowSize->setChecked(m_cfg->getBool("Save/window-size", true));
  ui_sigrokExe->setText(m_cfg->getString("Port settings/sigrok_exe", "sigrok-cli"));
}

void GeneralPrefs::factoryDefaultsSLOT()
{
  ui_alertUnsavedData->setChecked(true);
  ui_saveWindowPos->setChecked(true);
  ui_saveWindowSize->setChecked(true);
  ui_sigrokExe->setText("sigrok-cli");
}

void GeneralPrefs::applySLOT()
{
  m_cfg->setBool("Alert/unsaved-file", alertUnsavedData());
  m_cfg->setBool("Save/window-pos", saveWindowPosition());
  m_cfg->setBool("Save/window-size", saveWindowSize());
  m_cfg->setString("Port settings/sigrok_exe", sigrokExecutable());
}
