/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "TouchpadGestureTrackerTests.h"

#include "deskflow/TouchpadGestureTracker.h"

using Contacts = std::vector<TouchpadGestureTracker::Contact>;
using enum TouchpadGesture::Phase;

namespace {

// \p count fingers side by side, all moved by x, y
Contacts fingers(int count, double x, double y, uint32_t firstId = 1)
{
  Contacts contacts;
  for (int i = 0; i < count; ++i) {
    contacts.push_back({firstId + static_cast<uint32_t>(i), 0.3 + 0.1 * i + x, 0.3 + y});
  }
  return contacts;
}

} // namespace

void TouchpadGestureTrackerTests::swipeBeginsAfterThreshold()
{
  TouchpadGestureTracker tracker;
  QVERIFY(!tracker.update(fingers(3, 0.0, 0.0), true));
  QVERIFY(!tracker.update(fingers(3, 0.0, -0.02), true));

  const auto begin = tracker.update(fingers(3, 0.0, -0.04), true);
  QVERIFY(begin);
  QVERIFY(begin->phase == Begin);
  QCOMPARE(begin->fingers, uint8_t{3});
  QCOMPARE(begin->dx, int16_t{0});
  QCOMPARE(begin->dy, int16_t{-400});

  const auto update = tracker.update(fingers(3, 0.01, -0.05), true);
  QVERIFY(update);
  QVERIFY(update->phase == Update);
  QCOMPARE(update->dx, int16_t{100});
  QCOMPARE(update->dy, int16_t{-100});

  // no movement, nothing to send
  QVERIFY(!tracker.update(fingers(3, 0.01, -0.05), true));
}

void TouchpadGestureTrackerTests::swipeEndsWhenFingersLift()
{
  TouchpadGestureTracker tracker;
  tracker.update(fingers(3, 0.0, 0.0), true);
  QVERIFY(tracker.update(fingers(3, 0.05, 0.0), true));

  // the first finger to lift ends the swipe, and the rest don't start another
  const auto end = tracker.update(fingers(2, 0.05, 0.0), true);
  QVERIFY(end);
  QVERIFY(end->phase == End);
  QCOMPARE(end->fingers, uint8_t{3});
  QVERIFY(!tracker.update(fingers(2, 0.2, 0.0), true));
  QVERIFY(!tracker.update({}, true));
}

void TouchpadGestureTrackerTests::fourFingerSwipe()
{
  TouchpadGestureTracker tracker;
  tracker.update(fingers(4, 0.0, 0.0), true);
  const auto begin = tracker.update(fingers(4, -0.05, 0.0), true);
  QVERIFY(begin);
  QVERIFY(begin->phase == Begin);
  QCOMPARE(begin->fingers, uint8_t{4});
  QCOMPARE(begin->dx, int16_t{-500});

  // a swipe keeps its finger count when one of four fingers lifts
  const auto update = tracker.update(fingers(3, -0.06, 0.0), true);
  QVERIFY(update);
  QVERIFY(update->phase == Update);
  QCOMPARE(update->fingers, uint8_t{4});
  QCOMPARE(update->dx, int16_t{-100});
}

void TouchpadGestureTrackerTests::twoOrFiveFingersNeverBegin()
{
  for (const int count : {1, 2, 5}) {
    TouchpadGestureTracker tracker;
    tracker.update(fingers(count, 0.0, 0.0), true);
    QVERIFY(!tracker.update(fingers(count, 0.0, 0.2), true));
    QVERIFY(!tracker.update(fingers(count, 0.0, 0.4), true));
  }
}

void TouchpadGestureTrackerTests::beginsOnlyWhenAllowed()
{
  TouchpadGestureTracker tracker;
  tracker.update(fingers(3, 0.0, 0.0), false);
  QVERIFY(!tracker.update(fingers(3, 0.0, 0.1), false));

  // movement while it wasn't allowed doesn't count
  QVERIFY(!tracker.update(fingers(3, 0.0, 0.12), true));
  QVERIFY(tracker.update(fingers(3, 0.0, 0.16), true));

  // a swipe that began goes on until the fingers lift
  const auto update = tracker.update(fingers(3, 0.0, 0.2), false);
  QVERIFY(update);
  QVERIFY(update->phase == Update);
  QCOMPARE(update->dy, int16_t{400});
  const auto end = tracker.update({}, false);
  QVERIFY(end);
  QVERIFY(end->phase == End);
}

void TouchpadGestureTrackerTests::changedFingersAreNotMovement()
{
  TouchpadGestureTracker tracker;
  tracker.update(fingers(3, 0.0, 0.0), true);
  QVERIFY(!tracker.update(fingers(3, 0.0, 0.02), true));

  // different fingers far away in one frame, then small moves from there
  QVERIFY(!tracker.update(fingers(3, 0.0, 0.5, 10), true));
  QVERIFY(!tracker.update(fingers(3, 0.0, 0.52, 10), true));
  const auto begin = tracker.update(fingers(3, 0.0, 0.54, 10), true);
  QVERIFY(begin);
  QCOMPARE(begin->dy, int16_t{400});
}

void TouchpadGestureTrackerTests::smallMovesAddUp()
{
  TouchpadGestureTracker tracker;
  tracker.update(fingers(3, 0.0, 0.0), true);
  QVERIFY(tracker.update(fingers(3, 0.04, 0.0), true));

  // moves under one unit each are carried until they make whole units
  int total = 0;
  for (int i = 1; i <= 10; ++i) {
    if (const auto update = tracker.update(fingers(3, 0.04 + 0.00004 * i, 0.0), true)) {
      total += update->dx;
    }
  }
  QCOMPARE(total, 4);
}

QTEST_MAIN(TouchpadGestureTrackerTests)
