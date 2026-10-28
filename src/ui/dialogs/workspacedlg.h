// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;

/// Asked before a workspace is saved: its name and description, with the
/// picture that goes into the file.
class WorkspaceDlg : public QDialog
{
  Q_OBJECT
public:
  WorkspaceDlg(const QString &name, const QString &description, const QPixmap &picture, QWidget *parent = nullptr);
  QString name() const;
  QString description() const;

private:
  QLineEdit      *m_name;
  QPlainTextEdit *m_description;
};
