/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileClipboard.h"

#include "common/Settings.h"
#include "deskflow/IClipboard.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace deskflow::fileclipboard {

namespace {

const auto kFieldSeparator = QLatin1Char('\t');

#ifdef Q_OS_WIN
// names Windows won't create, whatever the extension
bool isReservedWindowsName(const QString &name)
{
  static const QStringList reserved = {QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),
                                       QStringLiteral("NUL"),  QStringLiteral("COM1"), QStringLiteral("COM2"),
                                       QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("COM5"),
                                       QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
                                       QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"),
                                       QStringLiteral("LPT3"), QStringLiteral("LPT4"), QStringLiteral("LPT5"),
                                       QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"),
                                       QStringLiteral("LPT9")};
  return reserved.contains(name.section(QLatin1Char('.'), 0, 0).toUpper());
}
#endif

QString sanitizeName(QString name)
{
  if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") || name.contains(QChar(0)))
    return {};

#ifdef Q_OS_WIN
  static const QString invalid = QStringLiteral("<>:\"\\|?*");
  for (auto &c : name) {
    if (c.unicode() < 0x20 || invalid.contains(c))
      c = QLatin1Char('_');
  }
  // Windows drops trailing dots and spaces, which could turn a name into ".."
  while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
    name.chop(1);
  if (name.isEmpty())
    return {};
  if (isReservedWindowsName(name))
    name.prepend(QLatin1Char('_'));
#endif

  return name;
}

Entry entryFor(int top, const QFileInfo &info, const QString &relativePath)
{
  Entry entry;
  entry.top = top;
  entry.isDir = info.isDir();
  entry.size = entry.isDir ? 0 : static_cast<uint64_t>(info.size());
  entry.modified = info.lastModified().toSecsSinceEpoch();
  entry.relativePath = relativePath;
  entry.sourcePath = info.absoluteFilePath();
  return entry;
}

} // namespace

std::string encodeEntry(const Entry &entry)
{
  const QStringList fields = {
      QString::number(entry.top), entry.isDir ? QStringLiteral("d") : QStringLiteral("f"), QString::number(entry.size),
      QString::number(entry.modified), entry.relativePath
  };
  return fields.join(kFieldSeparator).toStdString();
}

bool decodeEntry(const std::string &data, Entry &entry)
{
  const QString text = QString::fromStdString(data);
  const QStringList fields = text.split(kFieldSeparator);
  if (fields.size() < 5)
    return false;

  bool topOk = false;
  bool sizeOk = false;
  bool modifiedOk = false;
  entry.top = fields[0].toInt(&topOk);
  entry.isDir = fields[1] == QStringLiteral("d");
  entry.size = fields[2].toULongLong(&sizeOk);
  entry.modified = fields[3].toLongLong(&modifiedOk);
  // the path is last and keeps any separators in it, file names may contain tabs
  entry.relativePath = text.section(kFieldSeparator, 4);
  entry.sourcePath.clear();

  const bool typeOk = entry.isDir || fields[1] == QStringLiteral("f");
  return topOk && entry.top >= 0 && typeOk && sizeOk && modifiedOk && !entry.relativePath.isEmpty();
}

bool listEntries(const QStringList &paths, QList<Entry> &entries, QString &error)
{
  entries.clear();
  for (int top = 0; top < paths.size(); ++top) {
    // a copied link is followed, the user picked it
    const QFileInfo item(paths[top]);
    if (!item.exists() || !item.isReadable()) {
      error = QStringLiteral("can't read %1").arg(paths[top]);
      return false;
    }

    entries.append(entryFor(top, item, item.fileName()));
    if (!item.isDir())
      continue;

    const QDir root(item.absoluteFilePath());
    QDirIterator it(
        root.absolutePath(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories
    );
    while (it.hasNext()) {
      const QFileInfo info = it.nextFileInfo();
      // links inside a folder could point anywhere on this computer
      if (info.isSymLink() || info.fileName() == QStringLiteral(".DS_Store"))
        continue;
      entries.append(entryFor(top, info, item.fileName() + QLatin1Char('/') + root.relativeFilePath(info.filePath())));
    }
  }
  return true;
}

QString sanitizeRelativePath(const QString &path)
{
  // a leading separator would make the path absolute
  if (path.startsWith(QLatin1Char('/')))
    return {};

  QStringList safe;
  for (const auto &name : path.split(QLatin1Char('/'))) {
    const QString cleaned = sanitizeName(name);
    if (cleaned.isEmpty())
      return {};
    safe.append(cleaned);
  }
  return safe.join(QLatin1Char('/'));
}

QString receiveFolder()
{
  return QDir(Settings::settingsPath()).filePath(QStringLiteral("received-files"));
}

QStringList copiedPaths(const IClipboard &clipboard)
{
  QStringList paths;
  if (clipboard.open(0)) {
    if (clipboard.has(IClipboard::Format::Files))
      paths = decodePaths(clipboard.get(IClipboard::Format::Files));
    clipboard.close();
  }
  return paths;
}

void setCopiedPaths(IClipboard &clipboard, const QStringList &paths)
{
  if (clipboard.open(0)) {
    clipboard.empty();
    clipboard.add(IClipboard::Format::Files, encodePaths(paths));
    clipboard.close();
  }
}

void removeCopiedPaths(IClipboard &clipboard)
{
  if (!clipboard.open(0))
    return;

  if (clipboard.has(IClipboard::Format::Files)) {
    QList<std::pair<IClipboard::Format, std::string>> kept;
    for (int format = 0; format < static_cast<int>(IClipboard::Format::TotalFormats); ++format) {
      const auto f = static_cast<IClipboard::Format>(format);
      if (f != IClipboard::Format::Files && clipboard.has(f))
        kept.append({f, clipboard.get(f)});
    }
    clipboard.empty();
    for (const auto &[format, data] : kept)
      clipboard.add(format, data);
  }
  clipboard.close();
}

} // namespace deskflow::fileclipboard
