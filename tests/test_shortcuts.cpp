// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The keys of the main window's commands (ShortcutRegistry) and the page
// Keyboard: only the keys that differ are stored, one file for all
// instances, conflicts and reserved keys, the tooltips name the key.
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>

#include "ui/settings/keyboardprefs.h"
#include "ui/shortcuts.h"

static int failed = 0;

static void check(bool cond, const QString &what)
{
  if (!cond)
  {
    qWarning() << "FAILED:" << what;
    failed++;
  }
}

static QAction *action(QWidget &w, const char *id, const QString &text, const QKeySequence &key)
{
  auto *a = new QAction(text, &w);
  a->setObjectName(id);
  a->setShortcut(key);
  return a;
}

int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir dir;
  const QString file = dir.filePath("shortcuts.conf");

  QWidget window;
  QAction *connect = action(window, "action_Connect", "&Connect", QKeySequence("F3"));
  QAction *record = action(window, "action_Record", "&Record", QKeySequence("Space"));
  record->setToolTip("Start recording");
  QAction *live = action(window, "action_Live", "&Live", QKeySequence());

  // --- 1. registered: the keys it came with are the defaults, the tooltip names the key ---
  {
    ShortcutRegistry reg(&window, file);
    reg.add(connect, "Meter");
    reg.add(record, "Recording", "Record / Stop");
    reg.add(live, "Recording");
    reg.load();
    check(reg.overrides().isEmpty(), "no file: no overrides");
    check(connect->shortcut() == QKeySequence("F3"), "default key stays");
    check(connect->toolTip() == ShortcutRegistry::withKey("Connect", QKeySequence("F3")), "tooltip from text with key: " + connect->toolTip());
    check(record->toolTip().startsWith("Start recording ("), "own tooltip with key: " + record->toolTip());
    check(live->toolTip() == "Live", "no key: tooltip without brackets: " + live->toolTip());
    check(window.actions().contains(live), "added to the window");
    check(reg.find("action_Record")->name == "Record / Stop", "own name");
    check(reg.find("action_Live")->name == "Live", "name from the text");

    // --- 2. a changed key is stored, an equal one is not ---
    int changes = 0;
    QObject::connect(&reg, &ShortcutRegistry::changed, [&changes] { ++changes; });
    reg.setOverrides({ { "action_Connect", { QKeySequence("F3") } }, { "action_Live", { QKeySequence("Ctrl+L") } } });
    check(changes == 1, "changed once");
    check(reg.overrides().size() == 1 && reg.overrides().contains("action_Live"), "default dropped");
    check(live->shortcut() == QKeySequence("Ctrl+L"), "applied");
    check(live->toolTip() == ShortcutRegistry::withKey("Live", QKeySequence("Ctrl+L")), "tooltip follows");
    reg.setOverrides(reg.overrides());
    check(changes == 1, "same again: no change");
    QSettings s(file, QSettings::IniFormat);
    check(s.childGroups() == QStringList{ "Shortcuts" }, "group Shortcuts");
    check(s.value("Shortcuts/action_Live").toString() == "Ctrl+L", "portable text in the file");
    check(!s.contains("Shortcuts/action_Connect"), "default not in the file");

    // --- 3. no key at all is kept as such ---
    reg.setOverrides({ { "action_Connect", {} } });
    check(connect->shortcut().isEmpty(), "key taken away");
    check(live->shortcut().isEmpty(), "Live back to its default (none)");
  }

  // --- 4. another instance reads the same file ---
  {
    QWidget other;
    QAction *c2 = action(other, "action_Connect", "&Connect", QKeySequence("F3"));
    QAction *unknown = action(other, "action_Gone", "&Gone", QKeySequence("F7"));
    {
      QSettings s(file, QSettings::IniFormat);
      s.setValue("Shortcuts/action_Removed", "F6");   // a command that no longer exists
    }
    ShortcutRegistry reg(&other, file);
    reg.add(c2, "Meter");
    reg.add(unknown, "Meter");
    reg.load();
    check(c2->shortcut().isEmpty(), "other instance: no key for Connect");
    check(reg.overrides().size() == 1, "unknown ids ignored");
    check(unknown->shortcut() == QKeySequence("F7"), "untouched default");
  }

  // --- 5. reserved keys, conflicts ---
  check(ShortcutRegistry::isReserved(QKeySequence("Esc")), "Esc reserved");
  check(ShortcutRegistry::isReserved(QKeySequence("Alt+C")), "Alt+letter reserved");
  check(ShortcutRegistry::isReserved(QKeySequence("Tab")), "Tab reserved");
  check(!ShortcutRegistry::isReserved(QKeySequence("Alt+F4")), "Alt+F4 not reserved");
  check(!ShortcutRegistry::isReserved(QKeySequence("Ctrl+R")), "Ctrl+R not reserved");
  check(!ShortcutRegistry::isReserved(QKeySequence("Space")), "Space not reserved");

  {
    QWidget w;
    QAction *a = action(w, "action_A", "&A", QKeySequence("F3"));
    QAction *b = action(w, "action_B", "&B", QKeySequence("F4"));
    ShortcutRegistry reg(&w, QString());   // memory only
    reg.add(a, "G");
    reg.add(b, "G");
    reg.load();
    check(reg.conflict("action_B", QKeySequence("F3"), {}) == "action_A", "F3 belongs to A");
    check(reg.conflict("action_A", QKeySequence("F3"), {}).isEmpty(), "own key: no conflict");

    KeyboardPrefs page;
    page.setRegistry(&reg);
    page.setConflictAnswer(QMessageBox::No);
    check(!page.setKey("action_B", QKeySequence("F3")), "No: refused");
    check(page.edited().isEmpty(), "No: nothing changed");
    page.setConflictAnswer(QMessageBox::Yes);
    check(page.setKey("action_B", QKeySequence("F3")), "Yes: taken");
    check(page.edited().value("action_B") == QList<QKeySequence>{ QKeySequence("F3") }, "B has F3");
    check(page.edited().contains("action_A") && page.edited().value("action_A").isEmpty(), "A lost it");
    check(!page.setKey("action_A", QKeySequence("Esc")), "reserved refused");
    check(a->shortcut() == QKeySequence("F3"), "not applied before OK");
    page.applySLOT();
    check(a->shortcut().isEmpty() && b->shortcut() == QKeySequence("F3"), "applied on OK");
    // back by hand to the default: not an override any more
    page.setKey("action_A", QKeySequence("F3"));   // takes it from B again
    page.setKey("action_B", QKeySequence("F4"));
    check(page.edited().isEmpty(), "set back by hand = defaults");
    page.factoryDefaultsSLOT();
    page.defaultsSLOT();
    check(page.edited() == reg.overrides(), "Cancel: the stored keys again");
    page.factoryDefaultsSLOT();
    page.applySLOT();
    check(a->shortcut() == QKeySequence("F3") && b->shortcut() == QKeySequence("F4"), "all to default");
  }

  if (failed)
    qWarning() << failed << "check(s) failed";
  else
    qInfo() << "test_shortcuts: all checks passed";
  return failed ? 1 : 0;
}
