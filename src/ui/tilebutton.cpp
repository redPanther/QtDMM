// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/tilebutton.h"

#include <QStyleOptionToolButton>
#include <QStylePainter>

TileButton::TileButton(QWidget *parent)
  : QToolButton(parent)
{
  setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
}

void TileButton::paintEvent(QPaintEvent *)
{
  QStylePainter painter(this);
  QStyleOptionToolButton option;
  initStyleOption(&option);

  // the frame of the whole tile, without its content
  QStyleOptionToolButton panel = option;
  panel.text.clear();
  panel.icon = QIcon();
  painter.drawComplexControl(QStyle::CC_ToolButton, panel);

  // the content as one block in the middle: the style gives the symbol its
  // height + 6 at the top of the rectangle and centres the text below
  const int textHeight = fontMetrics().boundingRect(rect(), Qt::AlignCenter | Qt::TextShowMnemonic, option.text).height();
  const int height = option.iconSize.height() + 6 + textHeight;
  QStyleOptionToolButton label = option;
  label.rect = QRect(rect().left(), rect().top() + (rect().height() - height) / 2, rect().width(), height);
  painter.drawControl(QStyle::CE_ToolButtonLabel, label);
}
