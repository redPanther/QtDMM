// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/settings/settingspage.h"
#include "ui/shortcuts.h"

class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

/// Settings page "Keyboard": the keys of the main window's actions. A list
/// by group with the key and the default, a search field, and below it the
/// key of the selected action to change, clear or set back. The keys are
/// one set for all instances (ShortcutRegistry, shortcuts.conf), not keys
/// of this instance's settings file.
class KeyboardPrefs : public SettingsPage
{
  Q_OBJECT
public:
  explicit KeyboardPrefs(QWidget *parent = nullptr);

  /// The actions and their keys; without it the page lists nothing.
  void setRegistry(ShortcutRegistry *registry);
  /// The keys as on the page, not applied yet.
  const ShortcutRegistry::Overrides &edited() const { return m_work; }
  /// Gives the selected action @p key as a user would (tests): false when
  /// it was refused (reserved, or the conflict question answered No).
  bool setKey(const QString &id, const QKeySequence &key);
  /// Answer for the conflict question instead of asking (tests): Yes or No.
  void setConflictAnswer(int button) { m_conflictAnswer = button; }

public Q_SLOTS:
  void defaultsSLOT() override;
  void factoryDefaultsSLOT() override;
  void applySLOT() override;

private:
  void fill();
  void updateRow(QTreeWidgetItem *item);
  void updateEditor();
  void filter(const QString &text);
  QString selectedId() const;
  void keyEdited();

  ShortcutRegistry *m_registry = nullptr;
  ShortcutRegistry::Overrides m_work;
  QLineEdit *m_search;
  QTreeWidget *m_tree;
  QKeySequenceEdit *m_key;
  QPushButton *m_clear, *m_default, *m_allDefault;
  QLabel *m_hint;
  bool m_updating = false;
  bool m_asking = false;   ///< the conflict question is open
  int m_conflictAnswer = 0;
};
