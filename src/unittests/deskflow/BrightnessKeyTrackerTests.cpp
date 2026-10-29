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
  dimmed.changed(55, 100.05);
  dimmed.displayChanged(false, 100.1);
  settled = dimmed.settle(100.2);
  QCOMPARE(settled.keys, (Keys{kKeyBrightnessDown, kKeyBrightnessUp}));
  QCOMPARE(settled.level, Level{55});
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
  QVERIFY(settling.changed(99, 100.4).keys.empty());
  QCOMPARE(settling.changed(100, 110.0).keys, Keys{kKeyBrightnessUp});
}

void BrightnessKeyTrackerTests::lostLevelIsAskedForAgain()
{
  // Windows sometimes drops the level put back after a step up to the brightest level
  BrightnessKeyTracker tracker;
  tracker.setLevels(percentLevels());
  tracker.changed(100, 0.0);
  QCOMPARE(tracker.leave(10.0).level, Level{99});
  QVERIFY(!tracker.changed(99, 10.2).level);
  QVERIFY(!tracker.retry(10.3));

  auto up = tracker.changed(100, 11.0);
  QCOMPARE(up.keys, Keys{kKeyBrightnessUp});
  QCOMPARE(up.level, Level{99});
  QCOMPARE(tracker.retry(11.3), Level{99});
  QVERIFY(tracker.changed(99, 11.5).keys.empty());
  QVERIFY(!tracker.retry(11.6));

  // so the next step up counts
  QCOMPARE(tracker.changed(100, 12.0).keys, Keys{kKeyBrightnessUp});

  // and it stops asking after kMaxRetries
  for (int retry = 1; retry <= BrightnessKeyTracker::kMaxRetries; ++retry) {
    QCOMPARE(tracker.retry(12.0 + retry * 0.3), Level{99});
  }
  QVERIFY(!tracker.retry(13.5));
}

void BrightnessKeyTrackerTests::stepAfterALostLevel()
{
  // a step down to 60 isn't put back, so the next step up goes from 60
  auto tracker = onClient(99);
  QCOMPARE(tracker.changed(89, 10.0).keys, Keys{kKeyBrightnessDown});
  QCOMPARE(tracker.changed(79, 10.2).keys, Keys{kKeyBrightnessDown});
  QCOMPARE(tracker.changed(69, 10.4).keys, Keys{kKeyBrightnessDown});
  auto up = tracker.changed(79, 11.0);
  QCOMPARE(up.keys, Keys{kKeyBrightnessUp});
  QCOMPARE(up.level, Level{99});

  // a step down from the brightest level when 99 was lost
  BrightnessKeyTracker brightest;
  brightest.setLevels(percentLevels());
  brightest.changed(100, 0.0);
  brightest.leave(10.0);
  QCOMPARE(brightest.changed(90, 11.0).keys, Keys{kKeyBrightnessDown});
}

void BrightnessKeyTrackerTests::retryStopsWhenTheBrightnessMoves()
{
  // the cursor came back and the restore was lost, then the brightness was changed on the server
  BrightnessKeyTracker tracker;
  tracker.setLevels(percentLevels());
  tracker.changed(100, 0.0);
  tracker.leave(10.0);
  tracker.changed(99, 10.2);
  QCOMPARE(tracker.enter(11.0).level, Level{100});
  QVERIFY(!tracker.changed(89, 11.2).level);
  QVERIFY(!tracker.retry(11.3));

  // a level asked for before the last one being reported doesn't stop it
  BrightnessKeyTracker late;
  late.setLevels(percentLevels());
  late.changed(100, 0.0);
  QCOMPARE(late.leave(10.0).level, Level{99});
  QCOMPARE(late.enter(10.05).level, Level{100});
  QVERIFY(!late.changed(99, 10.1).level);
  QCOMPARE(late.retry(10.35), Level{100});
  QVERIFY(!late.changed(100, 10.5).level);
  QVERIFY(!late.retry(10.8));

  // nor while the display is off or dimmed
  auto dimmed = onClient(100);
  dimmed.displayChanged(false, 20.0);
  QVERIFY(!dimmed.retry(20.3));
}

QTEST_MAIN(BrightnessKeyTrackerTests)
