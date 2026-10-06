// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/settings/settingspage.h"

class QCheckBox;
class QLineEdit;

/// Settings page "General": what QtDMM does at program exit (warn about
/// unsaved data, keep the window's position and size), the toolbars and the
/// programs it runs (sigrok-cli). Keys Alert/unsaved-file, Save/window-pos,
/// Save/window-size, Icons/text-label, Toolbar/dmm, Toolbar/graph,
/// Toolbar/file, Port settings/sigrok_exe.
class GeneralPrefs : public SettingsPage
{
  Q_OBJECT
public:
  explicit GeneralPrefs(QWidget *parent = nullptr);

  bool alertUnsavedData() const;
  bool saveWindowPosition() const;
  bool saveWindowSize() const;
  bool useTextLabel() const;
  bool showDmmToolbar() const;
  bool showGraphToolbar() const;
  bool showFileToolbar() const;
  void setToolbarVisibility(bool dmm, bool graph, bool file);
  /// The program for sigrok meters (Port settings/sigrok_exe, the same for all instances).
  QString sigrokExecutable() const;

public Q_SLOTS:
  void defaultsSLOT() override;
  void factoryDefaultsSLOT() override;
  void applySLOT() override;

private:
  QCheckBox *ui_alertUnsavedData, *ui_saveWindowPos, *ui_saveWindowSize;
  QCheckBox *ui_textLabel, *ui_dmmToolBar, *ui_graphToolBar, *ui_fileToolBar;
  QLineEdit *ui_sigrokExe;
};
