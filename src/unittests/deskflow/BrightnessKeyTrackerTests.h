/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class BrightnessKeyTrackerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void stepsBecomeKeys();
  void quickStepsAllCount();
  void brightestLevelMakesRoom();
  void quickReturnAtBrightestLevel();
  void returnBeforeTheMoveIsReported();
  void bigMoveIsSkipped();
  void serverKeepsItsKeysOnItself();
  void noLevelsNoKeys();
  void dimmedDisplayIsNotAKey();
  void powerEventBeforeIsNotAKey();
  void powerEventAfterGivesTheLevelBack();
  void systemChangeWhileSettlingWins();
  void returningRightAfterAStep();
};
