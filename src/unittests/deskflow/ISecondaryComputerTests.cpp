/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ISecondaryComputerTests.h"

#include "deskflow/ISecondaryComputer.h"

#include <QFile>

namespace {

// one wheel click, the step size the platforms use
const int32_t s_step = 120;

class TestComputer : public ISecondaryComputer
{
public:
  void fakeMouseButton(ButtonID, bool) override
  {
  }
  void fakeMouseMove(int32_t, int32_t) override
  {
  }
  void fakeMouseRelativeMove(int32_t, int32_t) const override
  {
  }
  void fakeMouseWheel(ScrollDelta) const override
  {
  }
};

} // namespace

void ISecondaryComputerTests::initTestCase()
{
  QFile::remove(m_settingsFile);
  Settings::setSettingsFile(m_settingsFile);
}

void ISecondaryComputerTests::init()
{
  Settings::setValue(Settings::Client::YScrollScale, 1.0);
  Settings::setValue(Settings::Client::XScrollScale, 1.0);
  Settings::setValue(Settings::Client::InvertYScroll, false);
  Settings::setValue(Settings::Client::InvertXScroll, false);
}

void ISecondaryComputerTests::applyScrollModifierInSteps_wholeSteps_unchanged()
{
  TestComputer computer;
  const auto steps = computer.applyScrollModifierInSteps({2 * s_step, 3 * s_step}, s_step);
  QCOMPARE(steps.x, 2);
  QCOMPARE(steps.y, 3);
}

void ISecondaryComputerTests::applyScrollModifierInSteps_subStepDeltas_addUpToStep()
{
  TestComputer computer;
  for (int i = 0; i < 3; ++i) {
    QCOMPARE(computer.applyScrollModifierInSteps({0, s_step / 4}, s_step).y, 0);
  }
  QCOMPARE(computer.applyScrollModifierInSteps({0, s_step / 4}, s_step).y, 1);
}

void ISecondaryComputerTests::applyScrollModifierInSteps_halfScale_stepEverySecondClick()
{
  Settings::setValue(Settings::Client::YScrollScale, 0.5);
  TestComputer computer;
  QCOMPARE(computer.applyScrollModifierInSteps({0, s_step}, s_step).y, 0);
  QCOMPARE(computer.applyScrollModifierInSteps({0, s_step}, s_step).y, 1);
}

void ISecondaryComputerTests::applyScrollModifierInSteps_inverted_negativeSteps()
{
  Settings::setValue(Settings::Client::InvertYScroll, true);
  TestComputer computer;
  QCOMPARE(computer.applyScrollModifierInSteps({0, 2 * s_step}, s_step).y, -2);
}

QTEST_MAIN(ISecondaryComputerTests)
