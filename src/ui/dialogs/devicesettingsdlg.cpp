// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/devicesettingsdlg.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "ui/devicesettings.h"
#include "ui/devicesummary.h"
#include "ui/devicesymbol.h"

DeviceSettingsDlg::DeviceSettingsDlg(const QString &name, QWidget *parent)
  : QDialog(parent),
    m_name(name)
{
  setWindowTitle(name);
  auto *layout = new QVBoxLayout(this);
  m_tabs = new QTabWidget(this);
  layout->addWidget(m_tabs, 1);

  // Info: the device at a glance, its state now, where to read more
  auto *info = new QWidget(m_tabs);
  auto *infoLayout = new QVBoxLayout(info);
  m_summary = new DeviceSummary(info);
  infoLayout->addWidget(m_summary);
  auto *line = new QFrame(info);
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);
  infoLayout->addWidget(line);
  m_status = new QLabel(info);
  m_status->setObjectName("ui_deviceStatus");
  m_status->setTextFormat(Qt::RichText);
  m_status->setWordWrap(true);
  m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
  infoLayout->addWidget(m_status);
  infoLayout->addStretch(1);
  m_links = new QLabel(info);
  m_links->setObjectName("ui_deviceLinks");
  m_links->setTextFormat(Qt::RichText);
  m_links->setTextInteractionFlags(Qt::TextBrowserInteraction);
  // the maker's website in the browser, the help page in QtDMM's help
  connect(m_links, &QLabel::linkActivated, this, [this](const QString &link)
  {
    if (link.startsWith(QLatin1String("help:")))
      Q_EMIT helpRequested(link.mid(5));
    else
      QDesktopServices::openUrl(QUrl(link));
  });
  infoLayout->addWidget(m_links);
  m_tabs->addTab(info, tr("&Info"));

  m_settings = new DeviceSettings(m_tabs);
  m_settings->setDescriptionFilesVisible(false);
  m_tabs->addTab(m_settings, tr("&Settings"));

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  m_ok = buttons->button(QDialogButtonBox::Ok);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_settings, &DeviceSettings::changed, this, [this]
  {
    m_ok->setEnabled(m_settings->isComplete());
    updateInfo();
  });
  layout->addWidget(buttons);

  m_tick.setInterval(1000);
  connect(&m_tick, &QTimer::timeout, this, &DeviceSettingsDlg::updateStatus);
  resize(620, 480);
}

void DeviceSettingsDlg::load(const QVariantMap &keys)
{
  m_settings->load(keys);
  m_ok->setEnabled(m_settings->isComplete());
  updateInfo();
}

QVariantMap DeviceSettingsDlg::keys() const
{
  return m_settings->keys();
}

void DeviceSettingsDlg::showTab(Tab tab)
{
  m_tabs->setCurrentIndex(int(tab));
}

DeviceSettingsDlg::Tab DeviceSettingsDlg::currentTab() const
{
  return Tab(m_tabs->currentIndex());
}

void DeviceSettingsDlg::setStatus(const std::function<QString()> &status)
{
  m_statusText = status;
  updateStatus();
  if (m_statusText)
    m_tick.start();
  else
    m_tick.stop();
}

QString DeviceSettingsDlg::statusText() const
{
  return m_status->text();
}

QString DeviceSettingsDlg::linksText() const
{
  return m_links->text();
}

void DeviceSettingsDlg::updateInfo()
{
  m_summary->show(m_settings, m_name);
  QStringList links;
  const QString vendor = m_settings->dmmInfo().vendor;
  const QString site = DeviceSymbol::website(vendor);
  if (!site.isEmpty())
    links << QString("<a href=\"%1\">%2</a>").arg(site.toHtmlEscaped(), tr("Website of %1").arg(vendor).toHtmlEscaped());
  links << QString("<a href=\"help:supported-devices.md\">%1</a>").arg(tr("Supported devices (help)").toHtmlEscaped());
  m_links->setText(links.join("&nbsp;&nbsp;·&nbsp;&nbsp;"));
}

void DeviceSettingsDlg::updateStatus()
{
  m_status->setText(m_statusText ? m_statusText() : QString());
}
