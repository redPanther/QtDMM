// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/finddevicedlg.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "core/devicelibrary.h"
#include "core/settings.h"

FindDeviceDlg::FindDeviceDlg(Settings *settings, DeviceLibrary *library, QWidget *parent) :
  QDialog(parent),
  m_settings(settings),
  m_library(library)
{
  setWindowTitle(tr("Find device"));
  auto *layout = new QVBoxLayout(this);

  // where to look: only what is ticked is searched, so nobody waits for a
  // Bluetooth scan without a Bluetooth meter
  auto *where = new QGroupBox(tr("Where should QtDMM look?"), this);
  auto *whereLayout = new QVBoxLayout(where);
  m_usb = new QCheckBox(tr("&USB cable of the meter (UNI-T UT-D04 and UT-D09, Brymen BU-86X, TFA ...)"), where);
  m_ble = new QCheckBox(tr("&Bluetooth (UNI-T UT60BT and UT161, Victron ...)"), where);
  m_serial = new QCheckBox(tr("&Serial cable or USB-serial adapter (RS-232, FTDI, CH340, PL2303 ...)"), where);
  m_network = new QCheckBox(tr("In the &network (qtdmm-bridge)"), where);
  const QString noBle = BleDiscoverer::unavailable();
  if (!noBle.isEmpty())
  {
    m_ble->setEnabled(false);
    m_ble->setText(m_ble->text() + " - " + noBle);
  }
  const QStringList remembered = m_settings->getString("Find/where").split(',', Qt::SkipEmptyParts);
  const std::pair<QCheckBox *, const char *> keysOf[] = { { m_usb, "usb" }, { m_ble, "ble" }, { m_serial, "serial" },
                                                          { m_network, "network" } };
  for (const auto &[box, key] : keysOf)
  {
    box->setChecked(box->isEnabled() && remembered.contains(key));
    box->setProperty("key", key);
    whereLayout->addWidget(box);
    connect(box, &QCheckBox::toggled, this, &FindDeviceDlg::updateButtons);
  }
  auto *searchRow = new QHBoxLayout;
  m_search = new QPushButton(tr("Sea&rch"), where);
  m_progress = new QProgressBar(where);
  m_progress->setRange(0, 0);
  m_progress->setTextVisible(false);
  m_progress->hide();
  searchRow->addWidget(m_search);
  searchRow->addWidget(m_progress, 1);
  whereLayout->addLayout(searchRow);
  layout->addWidget(where);
  connect(m_search, &QPushButton::clicked, this, &FindDeviceDlg::search);

  // what was found, as it comes
  m_cards = new QListWidget(this);
  m_cards->setIconSize(QSize(32, 32));
  m_cards->setSpacing(2);
  m_cards->setMinimumHeight(160);
  layout->addWidget(m_cards, 1);
  connect(m_cards, &QListWidget::currentRowChanged, this, &FindDeviceDlg::select);

  // the chosen one
  m_hint = new QLabel(this);
  m_hint->setWordWrap(true);
  m_fix = new QPushButton(tr("How to &fix..."), this);
  m_fix->hide();
  auto *hintRow = new QHBoxLayout;
  hintRow->addWidget(m_hint, 1);
  hintRow->addWidget(m_fix);
  layout->addLayout(hintRow);
  connect(m_fix, &QPushButton::clicked, this, [this]
  {
    const int row = m_cards->currentRow();
    if (row < 0 || row >= m_found.size())
      return;
    QMessageBox box(QMessageBox::Information, tr("How to fix"), m_found[row].problem, QMessageBox::Close, this);
    box.setInformativeText(m_found[row].fix);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    box.exec();
  });

  auto *form = new QFormLayout;
  auto *modelRow = new QHBoxLayout;
  m_model = new QComboBox(this);
  m_model->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_model->setMinimumContentsLength(24);
  m_model->setPlaceholderText(tr("Choose the model"));
  m_allModels = new QCheckBox(tr("Not listed? &All models"), this);
  modelRow->addWidget(m_model, 1);
  modelRow->addWidget(m_allModels);
  auto *modelLabel = new QLabel(tr("&Model:"), this);
  modelLabel->setBuddy(m_model);
  form->addRow(modelLabel, modelRow);
  connect(m_allModels, &QCheckBox::toggled, this, &FindDeviceDlg::showAllModels);
  connect(m_model, &QComboBox::currentTextChanged, this, [this](const QString &model)
  {
    if (!m_name->isModified())
      m_name->setText(model.isEmpty() || !m_library ? model : m_library->uniqueName(QString(model).remove(" *")));
    // another model than the known one at this place: another meter, a new entry
    const bool known = !knownDevice().isEmpty();
    m_keep->setVisible(!known);
    m_name->setVisible(!known);
    updateButtons();
  });
  m_key = new QLineEdit(this);
  m_key->setPlaceholderText(tr("32 hex digits, from VictronConnect"));
  m_keyLabel = new QLabel(tr("Device &key:"), this);
  m_keyLabel->setBuddy(m_key);
  form->addRow(m_keyLabel, m_key);
  connect(m_key, &QLineEdit::textChanged, this, &FindDeviceDlg::updateButtons);
  m_keep = new QCheckBox(tr("Keep in My de&vices as"), this);
  m_keep->setChecked(true);
  m_name = new QLineEdit(this);
  form->addRow(m_keep, m_name);
  connect(m_keep, &QCheckBox::toggled, m_name, &QLineEdit::setEnabled);
  layout->addLayout(form);

  auto *buttons = new QDialogButtonBox(this);
  m_connect = buttons->addButton(tr("&Connect"), QDialogButtonBox::AcceptRole);
  buttons->addButton(QDialogButtonBox::Close);
  connect(buttons, &QDialogButtonBox::accepted, this, [this]
  {
    stopSearch();
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  select();
  resize(640, 560);
}

FindDeviceDlg::~FindDeviceDlg()
{
  stopSearch();
}

QList<QCheckBox *> FindDeviceDlg::places() const
{
  return { m_usb, m_ble, m_serial, m_network };
}

void FindDeviceDlg::search()
{
  stopSearch();
  m_found.clear();
  m_known.clear();
  m_cards->clear();
  // the choice is remembered for the next search
  QStringList where;
  for (QCheckBox *box : places())
    if (box->isChecked())
      where << box->property("key").toString();
  m_settings->setString("Find/where", where.join(','));
  m_settings->save();

  auto run = [this](Discoverer *d)
  {
    connect(d, &Discoverer::found, this, &FindDeviceDlg::add);
    connect(d, &Discoverer::finished, this, [this, d]
    {
      m_running.removeAll(d);
      d->deleteLater();
      if (m_running.isEmpty())
      {
        m_progress->hide();
        m_search->setEnabled(true);
        if (m_found.isEmpty())
          m_hint->setText(tr("Nothing found. Is the meter switched on and its cable plugged in? "
                             "Or set it up by hand on the Multimeter page."));
      }
    });
    m_running << d;
  };
  if (m_usb->isChecked())
    run(new UsbDiscoverer(this));
  if (m_ble->isChecked())
    run(new BleDiscoverer(this));
  if (m_serial->isChecked())
    run(new SerialDiscoverer(this));
  if (m_network->isChecked())
    run(new BridgeDiscoverer(this));
  m_search->setEnabled(false);
  m_progress->show();
  m_hint->setText(tr("Searching ..."));
  for (Discoverer *d : QList<Discoverer *>(m_running))
    d->start();
}

void FindDeviceDlg::stopSearch()
{
  for (Discoverer *d : QList<Discoverer *>(m_running))
    d->stop();
}

void FindDeviceDlg::add(const Candidate &c)
{
  for (const Candidate &f : m_found)
    if (f.key == c.key && f.kind == c.kind)
      return;
  // a device of My devices (by its place: a by-id name and its ttyUSB, an
  // HID cable with another hidraw number, a Bluetooth address): say so
  const QString known = m_library ? m_library->findByPlace(c.keys) : QString();
  m_found << c;
  m_known << known;
  // symbols of the sets QtDMM brings (assets/icons/sets)
  static const char *icons[] = { "qtdmm-dmm", "network-wired", "qtdmm-dmm", "network-server" };
  QString text = c.title + '\n' + c.detail;
  if (const std::optional<MyDevice> d = m_library ? m_library->find(known) : std::nullopt)
    text += '\n' + tr("In My devices: %1").arg(d->name);
  const QString inUse = m_inUse.value(DeviceLibrary::place(c.keys));
  if (!inUse.isEmpty())
    text += '\n' + inUse;
  if (!c.problem.isEmpty())
    text += '\n' + c.problem;
  auto *item = new QListWidgetItem(QIcon::fromTheme(icons[c.kind]), text, m_cards);
  if (!c.problem.isEmpty())
    item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
  if (m_cards->currentRow() < 0)
    m_cards->setCurrentRow(0);
}

void FindDeviceDlg::select()
{
  const int row = m_cards->currentRow();
  const bool any = row >= 0 && row < m_found.size();
  m_model->clear();
  m_fix->setVisible(any && !m_found[row].problem.isEmpty());
  m_key->clear();
  const std::optional<MyDevice> known = any && m_library ? m_library->find(m_known[row]) : std::nullopt;
  // a Victron device asks for its key - unless it is one of My devices with a key
  static const QRegularExpression hex32("^[0-9a-fA-F]{32}$");
  m_needsKey = any && !m_found[row].models.isEmpty() && m_found[row].models.first().startsWith("Victron")
               && !(known && hex32.match(known->keys.value("Port settings/ble-key").toString()).hasMatch());
  m_keyLabel->setVisible(m_needsKey);
  m_key->setVisible(m_needsKey);
  if (!any)
  {
    if (m_running.isEmpty())
      m_hint->setText(tr("Tick where to look and press Search."));
    updateButtons();
    return;
  }
  const Candidate &c = m_found[row];
  m_hint->setText(!c.problem.isEmpty() ? c.problem
                  : known              ? tr("Connect uses \"%1\" of My devices.").arg(known->name)
                                       : c.hint);
  // a port without a known family: the model is chosen from all of them,
  // none is chosen beforehand
  m_allModels->setVisible(!c.models.isEmpty());
  m_allModels->setChecked(false);
  showAllModels(false);
  if (known)
  {
    const int index = m_model->findText(known->model());
    if (index < 0)
      m_model->insertItem(0, known->model());
    m_model->setCurrentIndex(qMax(0, index));
  }
}

void FindDeviceDlg::showAllModels(bool all)
{
  const int row = m_cards->currentRow();
  QStringList models = row >= 0 && row < m_found.size() ? m_found[row].models : QStringList();
  if (all || models.isEmpty())
    models = Families::models(FrameFormat::Invalid, true) + Families::models(FrameFormat::Invalid, false);
  m_model->clear();
  m_model->addItems(models);
  // all models for a port nothing is known of: nothing chosen yet
  if (row >= 0 && row < m_found.size() && m_found[row].models.isEmpty())
    m_model->setCurrentIndex(-1);
  updateButtons();
}

void FindDeviceDlg::updateButtons()
{
  bool any = false;
  for (QCheckBox *box : places())
    any = any || box->isChecked();
  m_search->setEnabled(any && m_running.isEmpty());
  const int row = m_cards->currentRow();
  static const QRegularExpression hex32("^[0-9a-fA-F]{32}$");
  const bool ok = row >= 0 && row < m_found.size() && m_found[row].problem.isEmpty() && !m_model->currentText().isEmpty()
                  && (!m_needsKey || hex32.match(m_key->text().simplified().remove(' ')).hasMatch());
  m_connect->setEnabled(ok);
}

QVariantMap FindDeviceDlg::keys() const
{
  QVariantMap keys;
  const int row = m_cards->currentRow();
  if (row < 0 || row >= m_found.size())
    return keys;
  keys = m_found[row].keys;
  keys.insert("DMM/model", m_model->currentText());
  // asked after accept(): the dialog and its fields are hidden by then
  if (m_needsKey)
    keys.insert("Port settings/ble-key", m_key->text().simplified().remove(' '));
  return keys;
}

bool FindDeviceDlg::addToLibrary() const
{
  return knownDevice().isEmpty() && m_keep->isChecked() && !m_name->text().trimmed().isEmpty();
}

QString FindDeviceDlg::knownDevice() const
{
  const int row = m_cards->currentRow();
  if (row < 0 || row >= m_known.size() || !m_library)
    return QString();
  // the known entry as long as its model is chosen - a known one is
  // connected as it is, without a second entry
  const std::optional<MyDevice> d = m_library->find(m_known[row]);
  return d && QString(d->model()).remove(" *") == m_model->currentText().remove(" *") ? d->id : QString();
}

QString FindDeviceDlg::name() const
{
  return m_name->text().trimmed();
}
