// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
// RecentFiles: the workspaces used last, newest first and each once, at
// most so many; in a file that two instances share.
#include <QDir>
#include <QTemporaryDir>

#include "core/recentfiles.h"
#include "testutil.h"

int main()
{
  // the order: newest first, a file used again comes to the front
  check(RecentFiles::added({}, "/a", 3) == QStringList({ "/a" }), "the first");
  check(RecentFiles::added({ "/a", "/b" }, "/c", 3) == QStringList({ "/c", "/a", "/b" }), "the new one in front");
  check(RecentFiles::added({ "/a", "/b", "/c" }, "/b", 3) == QStringList({ "/b", "/a", "/c" }), "again: to the front, once");
  check(RecentFiles::added({ "/a", "/b", "/c" }, "/d", 3) == QStringList({ "/d", "/a", "/b" }), "the oldest goes");
  check(RecentFiles::added({ "/a" }, "/b", 0).isEmpty(), "none kept");

  // in memory (no file)
  {
    RecentFiles r(QString(), "Workspaces/recent", 2);
    r.add("/x/one.qtdmm-workspace");
    r.add("/x/two.qtdmm-workspace");
    r.add("/x/three.qtdmm-workspace");
    // (made absolute: on Windows with the drive in front)
    const QStringList l = r.list();
    check(l.size() == 2 && l[0].endsWith("/x/three.qtdmm-workspace") && l[1].endsWith("/x/two.qtdmm-workspace"),
          "in memory, at most two: " + l.join(", "));
  }

  // in a file: what one instance adds, the other reads; paths kept clean
  QTemporaryDir dir;
  const QString file = QDir(dir.path()).filePath("workspaces.conf");
  RecentFiles first(file);
  RecentFiles second(file);
  const QString bench = QDir(dir.path()).filePath("bench.qtdmm-workspace");
  first.add(QDir(dir.path()).filePath("sub/../bench.qtdmm-workspace"));
  check(second.list() == QStringList({ bench }), "the other instance reads it, the path clean: " + second.list().join(", "));
  second.add(QDir(dir.path()).filePath("field.qtdmm-workspace"));
  check(first.list().size() == 2 && first.list().first().endsWith("field.qtdmm-workspace"), "and the other way round");
  first.remove(bench);
  check(second.list().size() == 1 && !second.list().contains(bench), "removed for both");
  second.clear();
  check(first.list().isEmpty(), "cleared");
  first.add("relative.qtdmm-workspace");
  check(QDir::isAbsolutePath(first.list().value(0)), "a relative path kept absolute");

  if (failed)
    qWarning() << failed << "check(s) failed";
  else
    qInfo() << "all checks passed";
  return failed ? 1 : 0;
}
