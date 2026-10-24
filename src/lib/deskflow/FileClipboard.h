/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <string>

class IClipboard;

//! Helpers for IClipboard::Format::Files, copied files that move between computers
namespace deskflow::fileclipboard {

//! One file or folder to send, a folder's contents follow as separate entries
struct Entry
{
  //! Index of the copied item this entry belongs to
  int top = 0;
  bool isDir = false;
  uint64_t size = 0;
  //! Seconds since the epoch, 0 when unknown
  int64_t modified = 0;
  //! Starts with the copied item's name, folders separated by '/'
  QString relativePath;
  //! Where the entry is on this computer, only set by the sender
  QString sourcePath;
};

//! Encodes absolute local paths as UTF-8, one per line
std::string encodePaths(const QStringList &paths);

//! Decodes paths written by encodePaths, skipping empty lines
QStringList decodePaths(const std::string &data);

//! Encodes an entry header without its source path
std::string encodeEntry(const Entry &entry);

//! Decodes an entry header, false if it is malformed
bool decodeEntry(const std::string &data, Entry &entry);

//! Lists the copied paths and everything inside copied folders, symbolic links inside folders are skipped
//! \return false with \p error set when a copied path can't be read
bool listEntries(const QStringList &paths, QList<Entry> &entries, QString &error);

//! Makes a relative path from the other computer safe to create here
//! \return an empty string when the path can't be made safe, like one with ".." in it
QString sanitizeRelativePath(const QString &path);

//! Folder for files received from the other computer, inside the settings folder
QString receiveFolder();

//! The copied paths on a clipboard, empty when it holds no files
QStringList copiedPaths(const IClipboard &clipboard);

//! Replaces everything on \p clipboard with \p paths
void setCopiedPaths(IClipboard &clipboard, const QStringList &paths);

//! Removes the files from \p clipboard and keeps its other formats
void removeCopiedPaths(IClipboard &clipboard);

} // namespace deskflow::fileclipboard
