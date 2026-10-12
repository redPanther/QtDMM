// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/settings/keyboardprefs.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
QString keyText(const QList<QKeySequence> &keys)
{
  return QKeySequence::listToString(keys, QKeySequence::NativeText);
}
}

KeyboardPrefs::KeyboardPrefs(QWidget *parent) : SettingsPage(parent)
{
  m_label = tr("Keyboard");
  m_description = tr("The keys of the main window's commands, the same in all instances.");
  m_iconName = "input-keyboard";

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_search = new QLineEdit(this);
  m_search->setObjectName("ui_shortcutSearch");
  m_search->setPlaceholderText(tr("Search command or key"));
  m_search->setClearButtonEnabled(true);
  layout->addWidget(m_search);

  m_tree = new QTreeWidget(this);
  m_tree->setObjectName("ui_shortcuts");
  m_tree->setColumnCount(3);
  m_tree->setHeaderLabels({ tr("Command"), tr("Key"), tr("Default") });
  m_tree->setRootIsDecorated(false);
  m_tree->setUniformRowHeights(true);
  m_tree->setAllColumnsShowFocus(true);
  m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_tree->header()->setStretchLastSection(false);
  m_tree->setMinimumHeight(18 * fontMetrics().height());
  layout->addWidget(m_tree, 1);

  auto *row = new QHBoxLayout;
  auto *keyLabel = new QLabel(tr("&Key:"), this);
  m_key = new QKeySequenceEdit(this);
  m_key->setObjectName("ui_shortcutKey");
  m_key->setToolTip(tr("Click and press the new key for the selected command."));
  keyLabel->setBuddy(m_key);
  m_clear = new QPushButton(tr("&None"), this);
  m_clear->setToolTip(tr("The command gets no key."));
  m_default = new QPushButton(tr("&Default"), this);
  m_default->setToolTip(tr("The command gets its key back."));
  m_allDefault = new QPushButton(tr("&All to default"), this);
  m_allDefault->setObjectName("ui_shortcutsAllDefault");
  row->addWidget(keyLabel);
  row->addWidget(m_key, 1);
  row->addWidget(m_clear);
  row->addWidget(m_default);
  row->addStretch();
  row->addWidget(m_allDefault);
  layout->addLayout(row);

  m_hint = new QLabel(this);
  m_hint->setWordWrap(true);
  layout->addWidget(m_hint);

  connect(m_search, &QLineEdit::textChanged, this, &KeyboardPrefs::filter);
  connect(m_tree, &QTreeWidget::currentItemChanged, this, &KeyboardPrefs::updateEditor);
  connect(m_key, &QKeySequenceEdit::editingFinished, this, &KeyboardPrefs::keyEdited);
  connect(m_clear, &QPushButton::clicked, this, [this]
  {
    const QString id = selectedId();
    if (!id.isEmpty())
      setKey(id, QKeySequence());
  });
  connect(m_default, &QPushButton::clicked, this, [this]
  {
    const QString id = selectedId();
    if (id.isEmpty())
      return;
    m_work.remove(id);
    fill();
  });
  connect(m_allDefault, &QPushButton::clicked, this, &KeyboardPrefs::factoryDefaultsSLOT);
  updateEditor();
}

void KeyboardPrefs::setRegistry(ShortcutRegistry *registry)
{
  m_registry = registry;
  defaultsSLOT();
}

void KeyboardPrefs::defaultsSLOT()
{
  m_work = m_registry ? m_registry->overrides() : ShortcutRegistry::Overrides();
  fill();
}

void KeyboardPrefs::factoryDefaultsSLOT()
{
  m_work.clear();
  fill();
}

void KeyboardPrefs::applySLOT()
{
  if (m_registry)
    m_registry->setOverrides(m_work);
}

bool KeyboardPrefs::setKey(const QString &id, const QKeySequence &key)
{
  if (!m_registry || !m_registry->find(id))
    return false;
  if (ShortcutRegistry::isReserved(key))
  {
    m_hint->setText(tr("%1 stays with the window (buttons, menus, dialogs). Choose another key.")
                      .arg(key.toString(QKeySequence::NativeText)));
    fill();
    return false;
  }
  m_hint->clear();
  const QString other = m_registry->conflict(id, key, m_work);
  if (!other.isEmpty())
  {
    int answer = m_conflictAnswer;
    if (answer == 0)
      answer = QMessageBox::question(this, tr("Key in use"),
                                     tr("%1 is the key of \"%2\". Take it from there?")
                                       .arg(key.toString(QKeySequence::NativeText), m_registry->find(other)->name));
    if (answer != QMessageBox::Yes)
    {
      fill();
      return false;
    }
    QList<QKeySequence> rest = m_registry->keys(other, m_work);
    rest.removeAll(key);
    m_work.insert(other, rest);
  }
  m_work.insert(id, key.isEmpty() ? QList<QKeySequence>() : QList<QKeySequence>{ key });
  // a key set back to the default by hand counts as default
  if (m_work.value(id) == m_registry->find(id)->defaults)
    m_work.remove(id);
  if (!other.isEmpty() && m_work.value(other) == m_registry->find(other)->defaults)
    m_work.remove(other);
  fill();
  return true;
}

void KeyboardPrefs::fill()
{
  const QString current = selectedId();
  m_updating = true;
  m_tree->clear();
  QTreeWidgetItem *select = nullptr;
  if (m_registry)
  {
    QMap<QString, QTreeWidgetItem *> groups;
    for (const ShortcutRegistry::Entry &e : m_registry->entries())
    {
      QTreeWidgetItem *group = groups.value(e.group);
      if (!group)
      {
        group = new QTreeWidgetItem(m_tree, { e.group });
        QFont bold = group->font(0);
        bold.setBold(true);
        group->setFont(0, bold);
        group->setFlags(Qt::ItemIsEnabled);
        group->setFirstColumnSpanned(true);
        groups.insert(e.group, group);
      }
      auto *item = new QTreeWidgetItem(group);
      item->setData(0, Qt::UserRole, e.id);
      item->setText(0, e.name);
      updateRow(item);
      if (e.id == current)
        select = item;
    }
    m_tree->expandAll();
  }
  m_updating = false;
  if (select)
    m_tree->setCurrentItem(select);
  filter(m_search->text());
  updateEditor();
}

void KeyboardPrefs::updateRow(QTreeWidgetItem *item)
{
  const QString id = item->data(0, Qt::UserRole).toString();
  const ShortcutRegistry::Entry *e = m_registry->find(id);
  item->setText(1, keyText(m_registry->keys(id, m_work)));
  item->setText(2, keyText(e->defaults));
  // what the user changed stands out
  QFont f = item->font(0);
  f.setBold(m_work.contains(id));
  for (int c = 0; c < 2; ++c)
    item->setFont(c, f);
}

void KeyboardPrefs::updateEditor()
{
  if (m_updating)
    return;
  const QString id = selectedId();
  m_key->setEnabled(!id.isEmpty());
  m_clear->setEnabled(!id.isEmpty() && m_registry && !m_registry->keys(id, m_work).isEmpty());
  m_default->setEnabled(!id.isEmpty() && m_work.contains(id));
  m_allDefault->setEnabled(!m_work.isEmpty());
  m_updating = true;
  m_key->setKeySequence(id.isEmpty() || !m_registry ? QKeySequence() : m_registry->keys(id, m_work).value(0));
  m_updating = false;
}

void KeyboardPrefs::filter(const QString &text)
{
  const QString t = text.trimmed();
  for (int g = 0; g < m_tree->topLevelItemCount(); ++g)
  {
    QTreeWidgetItem *group = m_tree->topLevelItem(g);
    bool any = false;
    for (int i = 0; i < group->childCount(); ++i)
    {
      QTreeWidgetItem *item = group->child(i);
      const bool match = t.isEmpty() || item->text(0).contains(t, Qt::CaseInsensitive)
                         || item->text(1).contains(t, Qt::CaseInsensitive);
      item->setHidden(!match);
      any = any || match;
    }
    group->setHidden(!any);
  }
}

QString KeyboardPrefs::selectedId() const
{
  const QTreeWidgetItem *item = m_tree->currentItem();
  return item ? item->data(0, Qt::UserRole).toString() : QString();
}

// one key, not a sequence of up to four (Qt 6.4 has no maximum length);
// the edit reports the end again while the conflict question is open
void KeyboardPrefs::keyEdited()
{
  if (m_updating || m_asking)
    return;
  const QString id = selectedId();
  const QKeySequence seq = m_key->keySequence();
  if (id.isEmpty() || seq.isEmpty())
    return;
  const QKeySequence key(seq[0]);
  if (m_registry->keys(id, m_work).value(0) != key)
  {
    m_asking = true;
    setKey(id, key);
    m_asking = false;
  }
  else
    updateEditor();
}
