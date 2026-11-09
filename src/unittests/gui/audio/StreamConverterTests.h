/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

class StreamConverterTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void passesStreamFormatThrough();
  void convertsMonoFloatMicToStreamFormat();
  void splitReadsGiveTheSameOutput();
};
