// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

/// The files used last, newest first, in an ini file of their own that all
/// instances share (workspaces.conf next to devices.conf). Each read and
/// write goes to the file, so another instance's change shows the next time.
/// A window shows as many as it likes of them (Sidebar/recent-workspaces).
class RecentFiles
{
public:
  /// @p file the ini file, empty keeps the list in memory only (tests);
  /// @p key the list's key in it; at most @p max are kept.
  explicit RecentFiles(const QString &file, const QString &key = QStringLiteral("Workspaces/recent"),
                       int max = 30);

  /// The files, newest first.
  QStringList list() const;
  /// @p path to the front, once (absolute and clean, as the list keeps them).
  void        add(const QString &path);
  /// @p path off the list.
  void        remove(const QString &path);
  /// The list emptied.
  void        clear();

  /// @p list with @p path in front and no second time, at most @p max long.
  static QStringList added(QStringList list, const QString &path, int max);

private:
  void        store(const QStringList &list);

  QString     m_file;
  QString     m_key;
  int         m_max;
  QStringList m_memory;   ///< without a file
};
