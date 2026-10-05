// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class DeviceLibrary;

/// "Connect a meter", at the first start: My devices first when there are
/// some, then Find device (the assistant Add device), Try without a device (the virtual meter) and,
/// small, Set up by hand.
class WelcomeDlg : public QDialog
{
  Q_OBJECT
public:
  enum Choice
  {
    None,          ///< closed
    Known,         ///< device() of My devices
    Find,
    TryVirtual,
    ByHand
  };

  explicit WelcomeDlg(DeviceLibrary *library, QWidget *parent = nullptr);
  Choice      choice() const { return m_choice; }
  QString     device() const { return m_device; }

private:
  void        choose(Choice choice, const QString &device = QString());

  Choice      m_choice = None;
  QString     m_device;
};
