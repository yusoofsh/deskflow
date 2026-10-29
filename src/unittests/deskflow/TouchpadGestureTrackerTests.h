/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class TouchpadGestureTrackerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void swipeBeginsAfterThreshold();
  void swipeEndsWhenFingersLift();
  void fourFingerSwipe();
  void twoOrFiveFingersNeverBegin();
  void beginsOnlyWhenAllowed();
  void changedFingersAreNotMovement();
  void smallMovesAddUp();
};
