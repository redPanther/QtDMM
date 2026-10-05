// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QToolButton>

/// A big button with its symbol over its text, both together in the middle.
/// QToolButton puts the symbol at the top edge and centres only the text in
/// the rest, which looks unintended on a tall tile (Add device, the pages of
/// the assistant).
class TileButton : public QToolButton
{
  Q_OBJECT
public:
  explicit TileButton(QWidget *parent = nullptr);

protected:
  void paintEvent(QPaintEvent *event) override;
};
