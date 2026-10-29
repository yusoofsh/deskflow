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
  tracker.leave(0.0);
  return tracker;
}

} // namespace

void BrightnessKeyTrackerTests::stepsBecomeKeys()
{
  auto tracker = onClient(60);

  // the key goes out and the server's brightness goes back at once
  auto down = tracker.changed(50, 10.0);
  QCOMPARE(down.keys, Keys{kKeyBrightnessDown});
  QCOMPARE(down.level, Level{60});
  QVERIFY(tracker.settling());

  // putting the brightness back is not a step
  auto back = tracker.changed(60, 10.05);
  QVERIFY(back.keys.empty());
  QVERIFY(!back.level);

  // without a power event nothing is taken back
  auto settled = tracker.settle(10.2);
  QVERIFY(settled.keys.empty());
  QVERIFY(!settled.level);
  QVERIFY(!tracker.settling());

  auto up = tracker.changed(70, 11.0);
  QCOMPARE(up.keys, Keys{kKeyBrightnessUp});
  QCOMPARE(up.level, Level{60});
}

void BrightnessKeyTrackerTests::quickStepsAllCount()
{
  auto tracker = onClient(60);

  // a second press lands before the brightness was put back
  QCOMPARE(tracker.changed(50, 10.0).keys, Keys{kKeyBrightnessDown});
  QCOMPARE(tracker.changed(40, 10.05).keys, Keys{kKeyBrightnessDown});
  QVERIFY(tracker.changed(60, 10.1).keys.empty());
  QVERIFY(tracker.settle(10.2).keys.empty());
}

void BrightnessKeyTrackerTests::brightestLevelMakesRoom()
{
  BrightnessKeyTracker tracker;
  tracker.setLevels(percentLevels());
  tracker.changed(100, 0.0);

  // a step up from the brightest level changes nothing, so the server goes one level down
  QCOMPARE(tracker.leave(10.0).level, Level{99});
  QVERIFY(!tracker.changed(99, 10.05).level);

  auto up = tracker.changed(100, 11.0);
  QCOMPARE(up.keys, Keys{kKeyBrightnessUp});
  QCOMPARE(up.level, Level{99});
  tracker.changed(99, 11.05);

  // and goes back up when the cursor comes back
  QCOMPARE(tracker.enter(12.0).level, Level{100});

  // the darkest level makes room the other way
  tracker.changed(0, 20.0);
  QCOMPARE(tracker.leave(20.0).level, Level{1});
  tracker.changed(1, 20.1);
  QCOMPARE(tracker.enter(21.0).level, Level{0});
}

void BrightnessKeyTrackerTests::quickReturnAtBrightestLevel()
{
  BrightnessKeyTracker tracker;
  tracker.setLevels(percentLevels());
  tracker.changed(100, 0.0);
  tracker.leave(10.0);
  tracker.changed(99, 10.05);

  // the cursor comes back and leaves again before the server reports 100 again
  QCOMPARE(tracker.enter(10.1).level, Level{100});
  QCOMPARE(tracker.leave(10.12).level, Level{99});

  // the late 100 and the 99 after it are the server's own brightness, not steps
  QVERIFY(!tracker.changed(100, 10.15).level);
  QVERIFY(!tracker.changed(99, 10.2).level);
  QVERIFY(!tracker.settling());

  QCOMPARE(tracker.enter(11.0).level, Level{100});
}

void BrightnessKeyTrackerTests::returnBeforeTheMoveIsReported()
{
  BrightnessKeyTracker tracker;
  tracker.setLevels(percentLevels());
  tracker.changed(100, 0.0);
  QCOMPARE(tracker.leave(10.0).level, Level{99});

  // the 99 may still be on its way, so 100 goes after it
  QCOMPARE(tracker.enter(10.02).level, Level{100});
}

void BrightnessKeyTrackerTests::bigMoveIsSkipped()
{
  BrightnessKeyTracker tracker;
  tracker.setLevels({0, 50, 100});
  tracker.changed(100, 0.0);
  QVERIFY(!tracker.leave(10.0).level);
  QVERIFY(!tracker.enter(11.0).level);

  // steps down still count
  tracker.leave(12.0);
  auto down = tracker.changed(50, 13.0);
  QCOMPARE(down.keys, Keys{kKeyBrightnessDown});
  QCOMPARE(down.level, Level{100});
}

void BrightnessKeyTrackerTests::serverKeepsItsKeysOnItself()
{
  BrightnessKeyTracker tracker;
  tracker.setLevels(percentLevels());
  tracker.changed(60, 0.0);
  QVERIFY(!tracker.changed(50, 10.0).level);
  QVERIFY(!tracker.settling());

  // a step after the cursor came back stays on the server
  tracker.leave(10.5);
  tracker.enter(10.6);
  QVERIFY(!tracker.changed(40, 11.0).level);
  QVERIFY(!tracker.settling());
}

void BrightnessKeyTrackerTests::noLevelsNoKeys()
{
  BrightnessKeyTracker tracker;
  tracker.changed(60, 0.0);
  tracker.leave(0.0);
  QVERIFY(!tracker.changed(50, 10.0).level);
  QVERIFY(!tracker.settling());

  // nor without knowing where the brightness was
  BrightnessKeyTracker unknown;
  unknown.setLevels(percentLevels());
  unknown.leave(0.0);
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
  auto down = tracker.changed(50, 203.0);
  QCOMPARE(down.keys, Keys{kKeyBrightnessDown});
  QCOMPARE(down.level, Level{60});
}

void BrightnessKeyTrackerTests::powerEventBeforeIsNotAKey()
{
  auto tracker = onClient(60);

  // switching to battery picks the battery brightness
  tracker.powerChanged(100.0);
  QVERIFY(!tracker.changed(40, 100.5).level);
  QVERIFY(!tracker.settling());

  // the battery brightness is the one to keep from now on
  auto down = tracker.changed(30, 110.0);
  QCOMPARE(down.keys, Keys{kKeyBrightnessDown});
  QCOMPARE(down.level, Level{40});
}

void BrightnessKeyTrackerTests::powerEventAfterGivesTheLevelBack()
{
  auto tracker = onClient(60);

  // the brightness changes and the power event that explains it comes just after
  QCOMPARE(tracker.changed(40, 100.0).keys, Keys{kKeyBrightnessDown});
  tracker.changed(60, 100.05);
  tracker.powerChanged(100.1);

  // the key that already went out is taken back
  auto settled = tracker.settle(100.2);
  QCOMPARE(settled.keys, Keys{kKeyBrightnessUp});
  QCOMPARE(settled.level, Level{40});
  QVERIFY(!tracker.changed(40, 100.25).level);

  // the same when the display dims, in reverse order for several steps
  auto dimmed = onClient(60);
  dimmed.changed(50, 100.0);
  dimmed.changed(60, 100.02);
  dimmed.changed(70, 100.05);
  dimmed.displayChanged(false, 100.1);
  settled = dimmed.settle(100.2);
  QCOMPARE(settled.keys, (Keys{kKeyBrightnessDown, kKeyBrightnessUp}));
  QCOMPARE(settled.level, Level{70});
}

void BrightnessKeyTrackerTests::systemChangeWhileSettlingWins()
{
  auto tracker = onClient(60);

  // a step, then the system moves the brightness before the step settled
  tracker.changed(50, 10.0);
  tracker.powerChanged(10.1);
  QVERIFY(!tracker.changed(80, 10.15).level);

  auto settled = tracker.settle(10.2);
  QCOMPARE(settled.keys, Keys{kKeyBrightnessUp});
  QCOMPARE(settled.level, Level{80});
}

void BrightnessKeyTrackerTests::returningRightAfterAStep()
{
  auto tracker = onClient(60);
  QCOMPARE(tracker.changed(50, 10.0).keys, Keys{kKeyBrightnessDown});

  // the key already went to the client, so the server keeps its own brightness
  QVERIFY(!tracker.enter(10.1).level);
  QVERIFY(!tracker.settling());
  QVERIFY(tracker.settle(10.2).keys.empty());
}

void BrightnessKeyTrackerTests::levelPutBackIsNotReported()
{
  // Windows may not report the brightness put back, so every step goes from the held level
  auto tracker = onClient(60);
  QCOMPARE(tracker.changed(50, 10.0).keys, Keys{kKeyBrightnessDown});
  QCOMPARE(tracker.changed(50, 11.0).keys, Keys{kKeyBrightnessDown});
  QCOMPARE(tracker.changed(70, 12.0).keys, Keys{kKeyBrightnessUp});
  QCOMPARE(tracker.changed(70, 13.0).keys, Keys{kKeyBrightnessUp});

  // on the brightest level each step up reports the brightest level again
  BrightnessKeyTracker brightest;
  brightest.setLevels(percentLevels());
  brightest.changed(100, 0.0);
  QCOMPARE(brightest.leave(10.0).level, Level{99});
  QCOMPARE(brightest.changed(100, 11.0).keys, Keys{kKeyBrightnessUp});
  QCOMPARE(brightest.changed(100, 12.0).keys, Keys{kKeyBrightnessUp});
  QCOMPARE(brightest.enter(13.0).level, Level{100});

  // the next leave starts from the level put back, not the last one reported
  brightest.changed(90, 20.0);
  brightest.changed(100, 21.0);
  QCOMPARE(brightest.leave(30.0).level, Level{99});
  QCOMPARE(brightest.changed(90, 31.0).keys, Keys{kKeyBrightnessDown});
  QCOMPARE(brightest.enter(32.0).level, Level{100});
  QCOMPARE(brightest.leave(40.0).level, Level{99});
}

void BrightnessKeyTrackerTests::systemSetsTheBrightestLevel()
{
  auto tracker = onClient(60);

  // plugging in sets the brightness for mains power, which here is the brightest level
  tracker.powerChanged(100.0);
  auto plugged = tracker.changed(100, 100.5);
  QVERIFY(plugged.keys.empty());
  QCOMPARE(plugged.level, Level{99});
  QVERIFY(tracker.changed(99, 100.6).keys.empty());

  // so steps up still count, and the cursor coming back gives the brightest level back
  QCOMPARE(tracker.changed(100, 110.0).keys, Keys{kKeyBrightnessUp});
  QCOMPARE(tracker.enter(111.0).level, Level{100});

  // the same when a step is taken back
  auto settling = onClient(60);
  settling.changed(100, 100.0);
  settling.powerChanged(100.1);
  auto settled = settling.settle(100.2);
  QCOMPARE(settled.keys, Keys{kKeyBrightnessDown});
  QCOMPARE(settled.level, Level{99});
  QCOMPARE(settling.changed(100, 110.0).keys, Keys{kKeyBrightnessUp});
}

QTEST_MAIN(BrightnessKeyTrackerTests)
