// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/devicesettingsdlg.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/devicesettings.h"

DeviceSettingsDlg::DeviceSettingsDlg(const QString &name, QWidget *parent)
  : QDialog(parent)
{
  setWindowTitle(tr("Settings of %1").arg(name));
  auto *layout = new QVBoxLayout(this);
  m_settings = new DeviceSettings(this);
  m_settings->layout()->setContentsMargins(0, 0, 0, 0);
  m_settings->setDescriptionFilesVisible(false);
  layout->addWidget(m_settings, 1);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  m_ok = buttons->button(QDialogButtonBox::Ok);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_settings, &DeviceSettings::changed, this, [this] { m_ok->setEnabled(m_settings->isComplete()); });
  layout->addWidget(buttons);
  resize(620, 480);
}

void DeviceSettingsDlg::load(const QVariantMap &keys)
{
  m_settings->load(keys);
  m_ok->setEnabled(m_settings->isComplete());
}

QVariantMap DeviceSettingsDlg::keys() const
{
  return m_settings->keys();
}
