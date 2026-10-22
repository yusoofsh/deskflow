/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

class ISecondaryComputerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void init();
  void applyScrollModifierInSteps_wholeSteps_unchanged();
  void applyScrollModifierInSteps_subStepDeltas_addUpToStep();
  void applyScrollModifierInSteps_halfScale_stepEverySecondClick();
  void applyScrollModifierInSteps_inverted_negativeSteps();

private:
  inline static const QString m_settingsFile = QStringLiteral("tmp/ISecondaryComputerTests/Deskflow.conf");
};
