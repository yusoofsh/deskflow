/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "BrightnessKeyTrackerTests.h"

#include "deskflow/BrightnessKeyTracker.h"

#include <numeric>

using Keys = std::vector<KeyID>;
using Level = std::optional<uint8_t>;

namespace {

// levels 0 to 100, like most laptop displays on Windows
std::vector<uint8_t> percentLevels()
{
  std::vector<uint8_t> levels(101);
  std::iota(levels.begin(), levels.end(), uint8_t{0});
  return levels;
}

// a tracker with the server at \p level and the cursor on a client, long after any power event
BrightnessKeyTracker onClient(uint8_t level)
{
  BrightnessKeyTracker tracker;
  tracker.setLevels(percentLevels());
  tracker.changed(level, 0.0);
  tracker.leave();
  return tracker;
}

} // namespace

void BrightnessKeyTrackerTests::stepsBecomeKeys()
{
  auto tracker = onClient(60);

  // the server's brightness goes back at once, the key waits for kSettleTime
  auto down = tracker.changed(50, 10.0);
  QVERIFY(down.keys.empty());
  QCOMPARE(down.level, Level{60});
  QVERIFY(tracker.settling());

  // putting the brightness back is not a step
  QVERIFY(!tracker.changed(60, 10.05).level);

  auto settled = tracker.settle();
  QCOMPARE(settled.keys, Keys{kKeyBrightnessDown});
  QVERIFY(!settled.level);
  QVERIFY(!tracker.settling());

  QCOMPARE(tracker.changed(70, 11.0).level, Level{60});
  QCOMPARE(tracker.settle().keys, Keys{kKeyBrightnessUp});
}

void BrightnessKeyTrackerTests::quickStepsAllCount()
{
  auto tracker = onClient(60);

  // a second press lands before the brightness was put back
  tracker.changed(50, 10.0);
  tracker.changed(40, 10.05);
  tracker.changed(60, 10.1);
  QCOMPARE(tracker.settle().keys, (Keys{kKeyBrightnessDown, kKeyBrightnessDown}));
}

void BrightnessKeyTrackerTests::brightestLevelMakesRoom()
{
  BrightnessKeyTracker tracker;
  tracker.setLevels(percentLevels());
  tracker.changed(100, 0.0);

  // a step up from the brightest level changes nothing, so the server goes one level down
  QCOMPARE(tracker.leave().level, Level{99});
  QVERIFY(!tracker.changed(99, 10.0).level);

  QCOMPARE(tracker.changed(100, 11.0).level, Level{99});
  tracker.changed(99, 11.05);
  QCOMPARE(tracker.settle().keys, Keys{kKeyBrightnessUp});

  // and goes back up when the cursor comes back
  QCOMPARE(tracker.enter().level, Level{100});

  // the darkest level makes room the other way
  tracker.changed(0, 20.0);
  QCOMPARE(tracker.leave().level, Level{1});
  tracker.changed(1, 20.1);
  QCOMPARE(tracker.enter().level, Level{0});
}

void BrightnessKeyTrackerTests::bigMoveIsSkipped()
{
  BrightnessKeyTracker tracker;
  tracker.setLevels({0, 50, 100});
  tracker.changed(100, 0.0);
  QVERIFY(!tracker.leave().level);
  QVERIFY(!tracker.enter().level);

  // steps down still count
  tracker.leave();
  QCOMPARE(tracker.changed(50, 10.0).level, Level{100});
  QCOMPARE(tracker.settle().keys, Keys{kKeyBrightnessDown});
}

void BrightnessKeyTrackerTests::serverKeepsItsKeysOnItself()
{
  BrightnessKeyTracker tracker;
  tracker.setLevels(percentLevels());
  tracker.changed(60, 0.0);
  QVERIFY(!tracker.changed(50, 10.0).level);
  QVERIFY(!tracker.settling());

  // a step after the cursor came back stays on the server
  tracker.leave();
  tracker.enter();
  QVERIFY(!tracker.changed(40, 11.0).level);
  QVERIFY(!tracker.settling());
}

void BrightnessKeyTrackerTests::noLevelsNoKeys()
{
  BrightnessKeyTracker tracker;
  tracker.changed(60, 0.0);
  tracker.leave();
  QVERIFY(!tracker.changed(50, 10.0).level);
  QVERIFY(!tracker.settling());

  // nor without knowing where the brightness was
  BrightnessKeyTracker unknown;
  unknown.setLevels(percentLevels());
  unknown.leave();
  unknown.changed(50, 10.0);
  QVERIFY(!unknown.changed(40, 11.0).level);
}

void BrightnessKeyTrackerTests::dimmedDisplayIsNotAKey()
{
  auto tracker = onClient(60);

  tracker.displayChanged(false, 100.0);
  QVERIFY(!tracker.changed(30, 100.0).level);
  QVERIFY(!tracker.settling());

  tracker.displayChanged(true, 200.0);
  QVERIFY(!tracker.changed(60, 200.05).level);
  QVERIFY(!tracker.settling());

  // keys count again once the power events are kSystemWindow behind
  QCOMPARE(tracker.changed(50, 203.0).level, Level{60});
  QCOMPARE(tracker.settle().keys, Keys{kKeyBrightnessDown});
}

void BrightnessKeyTrackerTests::powerEventBeforeIsNotAKey()
{
  auto tracker = onClient(60);

  // switching to battery picks the battery brightness
  tracker.powerChanged(100.0);
  QVERIFY(!tracker.changed(40, 100.5).level);
  QVERIFY(!tracker.settling());

  // the battery brightness is the one to keep from now on
  QCOMPARE(tracker.changed(30, 110.0).level, Level{40});
  QCOMPARE(tracker.settle().keys, Keys{kKeyBrightnessDown});
}

void BrightnessKeyTrackerTests::powerEventAfterGivesTheLevelBack()
{
  auto tracker = onClient(60);

  // the display dims and says so only after its brightness changed
  QCOMPARE(tracker.changed(30, 100.0).level, Level{60});
  tracker.changed(60, 100.05);
  tracker.displayChanged(false, 100.1);

  auto settled = tracker.settle();
  QVERIFY(settled.keys.empty());
  QCOMPARE(settled.level, Level{30});
  QVERIFY(!tracker.changed(30, 100.25).level);
}

void BrightnessKeyTrackerTests::enterDropsWaitingSteps()
{
  auto tracker = onClient(60);
  tracker.changed(50, 10.0);
  tracker.enter();
  QVERIFY(!tracker.settling());
  QVERIFY(tracker.settle().keys.empty());
}

QTEST_MAIN(BrightnessKeyTrackerTests)
