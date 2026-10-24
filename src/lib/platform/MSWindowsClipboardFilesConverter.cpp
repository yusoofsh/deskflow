/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardFilesConverter.h"

#include "deskflow/FileClipboard.h"

#include <QDir>

#include <ShlObj.h>

#include <cstring>
#include <cwchar>

//
// MSWindowsClipboardFilesConverter
//

IClipboard::Format MSWindowsClipboardFilesConverter::getFormat() const
{
  return IClipboard::Format::Files;
}

UINT MSWindowsClipboardFilesConverter::getWin32Format() const
{
  return CF_HDROP;
}

HANDLE MSWindowsClipboardFilesConverter::fromIClipboard(const std::string &data) const
{
  // DROPFILES is followed by NUL terminated UTF-16 paths and one more NUL
  std::wstring paths;
  for (const auto &path : deskflow::fileclipboard::decodePaths(data)) {
    paths += QDir::toNativeSeparators(path).toStdWString();
    paths += L'\0';
  }
  if (paths.empty())
    return nullptr;
  paths += L'\0';

  const size_t pathBytes = paths.size() * sizeof(wchar_t);
  HGLOBAL gData = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + pathBytes);
  if (gData == nullptr)
    return nullptr;

  auto *dropFiles = static_cast<DROPFILES *>(GlobalLock(gData));
  if (dropFiles == nullptr) {
    GlobalFree(gData);
    return nullptr;
  }
  dropFiles->pFiles = sizeof(DROPFILES);
  dropFiles->fWide = TRUE;
  std::memcpy(reinterpret_cast<char *>(dropFiles) + sizeof(DROPFILES), paths.data(), pathBytes);
  GlobalUnlock(gData);
  return gData;
}

std::string MSWindowsClipboardFilesConverter::toIClipboard(HANDLE data) const
{
  const auto *dropFiles = static_cast<const DROPFILES *>(GlobalLock(data));
  if (dropFiles == nullptr)
    return {};

  // the paths may be UTF-16 or ANSI, and the list ends with an empty path
  const SIZE_T size = GlobalSize(data);
  QStringList paths;
  if (dropFiles->pFiles < size) {
    const char *begin = reinterpret_cast<const char *>(dropFiles) + dropFiles->pFiles;
    const char *end = reinterpret_cast<const char *>(dropFiles) + size;
    if (dropFiles->fWide) {
      const auto *path = reinterpret_cast<const wchar_t *>(begin);
      const auto *last = reinterpret_cast<const wchar_t *>(end);
      while (path < last && *path != L'\0') {
        const size_t length = wcsnlen(path, static_cast<size_t>(last - path));
        paths.append(QString::fromWCharArray(path, static_cast<qsizetype>(length)));
        path += length + 1;
      }
    } else {
      const char *path = begin;
      while (path < end && *path != '\0') {
        const size_t length = strnlen(path, static_cast<size_t>(end - path));
        paths.append(QString::fromLocal8Bit(path, static_cast<qsizetype>(length)));
        path += length + 1;
      }
    }
  }
  GlobalUnlock(data);
  return deskflow::fileclipboard::encodePaths(paths);
}
