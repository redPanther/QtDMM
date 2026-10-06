// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/settings/generalprefs.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QToolButton>

#include "core/settings.h"

GeneralPrefs::GeneralPrefs(QWidget *parent) : SettingsPage(parent)
{
  m_label = tr("General");
  m_description = tr("Program exit and the programs QtDMM runs.");
  m_iconName = "configure";

  QFormLayout *form = createForm();
  auto check = [this](const QString &text, const char *name, const QString &tip)
  {
    auto *box = new QCheckBox(text, this);
    box->setObjectName(name);
    box->setToolTip(tip);
    return box;
  };

  addSection(form, tr("At program exit"));
  ui_alertUnsavedData = check(tr("&Alert unsaved data"), "ui_alertUnsavedData",
                              tr("Asks before unsaved recordings are lost: at exit, and before an "
                                 "import replaces them."));
  ui_saveWindowPos = check(tr("Save window &position"), "ui_saveWindowPos",
                           tr("The next start opens the window where it was."));
  ui_saveWindowSize = check(tr("Save window si&ze"), "ui_saveWindowSize",
                            tr("The next start opens the window in the size it had."));
  form->addRow(QString(), ui_alertUnsavedData);
  form->addRow(QString(), ui_saveWindowPos);
  form->addRow(QString(), ui_saveWindowSize);

  // the programs QtDMM runs: sigrok-cli for the meters it reads through sigrok
  addSection(form, tr("Programs"));
  ui_sigrokExe = new QLineEdit(this);
  ui_sigrokExe->setObjectName("ui_sigrokExe");
  ui_sigrokExe->setPlaceholderText("sigrok-cli");
  ui_sigrokExe->setMinimumWidth(fontMetrics().horizontalAdvance('x') * 32);
  ui_sigrokExe->setToolTip(tr("The program QtDMM runs for the meters it reads through sigrok; "
                              "a name is searched in the PATH."));
  auto *browse = new QToolButton(this);
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
  form->addRow(tr("&sigrok-cli:"), row({ ui_sigrokExe, browse }, false));
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
