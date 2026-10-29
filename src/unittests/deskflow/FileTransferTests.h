/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"

#include <QTemporaryDir>
#include <QTest>

class FileTransferTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void pathsRoundTrip();
  void entryRoundTrip();
  void entryRejectsMalformed();
  void sanitizeRejectsEscapes();
  void sanitizeKeepsNormalNames();
  void clipboardPaths();
  void removeKeepsOtherFormats();
  void transferFilesAndFolders();
  void refusesPathsNotOffered();
  void newFetchCancelsOld();

private:
  QTemporaryDir m_settingsDir;
  QTemporaryDir m_sourceDir;
  Log m_log;
};
