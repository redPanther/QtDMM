// Copyright (c) 2026 The QtDMM developers
// SPDX-License-Identifier: GPL-3.0-or-later
#include "device/transports/hidpath.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <hidapi.h>

namespace HidPath
{

Entry parse(const QString &entry)
{
  Entry e;
  const QString t = entry.trimmed();
  e.path = t.contains(' ') ? t.section(' ', -1) : t;
  static const QRegularExpression ids("0x([0-9a-fA-F]{4}):0x([0-9a-fA-F]{4})");
  const auto m = ids.match(t);
  if (m.hasMatch())
  {
    e.hasIds = true;
    e.vendorId = m.captured(1).toUShort(nullptr, 16);
    e.productId = m.captured(2).toUShort(nullptr, 16);
  }
  return e;
}

Choice choose(const Entry &entry, const QList<Candidate> &present, QString (*canonical)(const QString &))
{
  Choice c;
  c.path = entry.path;
  if (!entry.hasIds)
    return c;   // a bare path: open it as it is
  const QString wanted = canonical(entry.path);
  QList<const Candidate *> same;
  for (const Candidate &d : present)
  {
    const bool ids = d.vendorId == entry.vendorId && d.productId == entry.productId;
    if (ids && (d.path == entry.path || canonical(d.path) == wanted))
      return c;   // still there (also through a udev link)
    if (ids)
      same << &d;
  }
  if (same.size() == 1)
  {
    c.path = same.first()->path;
    c.moved = c.path;
  }
  else if (same.size() > 1)
    c.ambiguous = true;
  return c;
}

namespace
{
QString canonicalPath(const QString &path)
{
  const QString c = QFileInfo(path).canonicalFilePath();
  return c.isEmpty() ? path : c;
}
}

Choice resolve(const QString &entry)
{
  const Entry e = parse(entry);
  if (!e.hasIds)
    return choose(e, {}, canonicalPath);
  QList<Candidate> present;
  struct hid_device_info *devs = hid_enumerate(e.vendorId, e.productId);
  for (struct hid_device_info *d = devs; d; d = d->next)
    present << Candidate{ d->vendor_id, d->product_id, QString::fromLatin1(d->path) };
  hid_free_enumeration(devs);
  return choose(e, present, canonicalPath);
}

QString linkFor(const QString &path, const QString &linkDir)
{
#ifdef Q_OS_LINUX
  const QDir dir(linkDir);
  if (!dir.exists())
    return QString();
  const QString target = canonicalPath(path);
  for (const QFileInfo &fi : dir.entryInfoList(QDir::System | QDir::Files | QDir::NoDotAndDotDot, QDir::Name))
    if (fi.isSymLink() && canonicalPath(fi.absoluteFilePath()) == target)
      return fi.absoluteFilePath();
#else
  Q_UNUSED(path) Q_UNUSED(linkDir)
#endif
  return QString();
}

}
