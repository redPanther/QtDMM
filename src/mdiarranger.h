// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>

class QMdiArea;
class QMdiSubWindow;

/// Places the sub-windows of a QMdiArea.
///
/// In the automatic mode ("Displays on top") the instruments share a strip
/// at the top, the graph takes the rest and a table gets a narrower column
/// on the right; the layout is redone whenever the area is resized or a
/// window is shown or hidden. "Free" is the classic MDI; there, windows
/// without title bar are moved with Ctrl+drag.
///
/// Knows nothing about QtDMM's windows or settings: it only sees
/// sub-windows and their roles, so it can move on unchanged.
class MdiArranger : public QObject
{
  Q_OBJECT
public:
  enum Mode { DisplaysOnTop, Free };
  enum Role { Instrument, Graph, Table };

  explicit MdiArranger(QMdiArea *area, QObject *parent = nullptr);

  /// Takes part in the arrangement, in the order added.
  void addWindow(QMdiSubWindow *window, Role role);

  Mode mode() const { return m_mode; }
  /// Switches the mode; the automatic mode also hides the title bars, Free
  /// shows them again (both can be changed afterwards).
  void setMode(Mode mode);

  /// Title bars of all windows (true = hidden).
  bool titleBarsHidden() const { return m_titleBarsHidden; }
  void setTitleBarsHidden(bool hidden);
  /// Title bar of one window.
  void setTitleBarHidden(QMdiSubWindow *window, bool hidden);
  static bool titleBarHidden(const QMdiSubWindow *window);


  /// The rects the automatic mode gives the visible windows inside @p area
  /// (in viewport coordinates); a table column is at least @p tableMinWidth
  /// wide. Public for the tests.
  static QList<QRect> layout(const QRect &area, const QList<Role> &roles, int headerHeight,
                             int tableMinWidth = 260);

public Q_SLOTS:
  /// Lays the windows out again (deferred, once per event loop pass).
  void arrange();
  /// Lays the windows out by the automatic rule now, in any mode - a start
  /// in Free mode seeds the positions with it.
  void arrangeNow();

Q_SIGNALS:
  /// The mode or the title bars changed.
  void changed();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void doArrange();
  QMdiSubWindow *subWindowOf(QObject *object) const;

  QMdiArea *m_area;
  Mode m_mode = DisplaysOnTop;
  bool m_titleBarsHidden = true;
  QList<QMdiSubWindow *> m_order;
  QList<Role> m_roles;             ///< parallel to m_order
  bool m_pending = false;
  QMdiSubWindow *m_drag = nullptr; ///< a window being moved with Ctrl+drag
  QPoint m_dragOffset;             ///< cursor - window position
};
