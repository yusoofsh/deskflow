/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/BrightnessKeyTracker.h"

#include <algorithm>
#include <cstdlib>

void BrightnessKeyTracker::setLevels(std::vector<uint8_t> levels)
{
  std::sort(levels.begin(), levels.end());
  levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
  m_levels = std::move(levels);
}

BrightnessKeyTracker::Action BrightnessKeyTracker::leave()
{
  Action action;
  m_onClient = true;
  m_steps.clear();
  m_held.reset();
  // without levels the server's brightness can't be put back, so the keys stay on the server
  if (!m_level || m_levels.size() < 2) {
    return action;
  }

  // on the brightest level a step up changes nothing, so it can't be seen; the same goes down
  m_held = m_level;
  if (*m_level >= m_levels.back()) {
    m_held = m_levels[m_levels.size() - 2];
  } else if (*m_level <= m_levels.front()) {
    m_held = m_levels[1];
  }
  if (std::abs(*m_held - *m_level) > kMaxMove) {
    // a big jump would dim or light up the display while the cursor is away
    m_held = m_level;
  }
  if (m_held != m_level) {
    m_before = m_level;
    m_moved = m_held;
    action.level = m_held;
  }
  return action;
}

BrightnessKeyTracker::Action BrightnessKeyTracker::enter()
{
  Action action;
  // undo leave() unless something else changed the brightness since
  if (m_before && m_level == m_moved) {
    action.level = m_before;
  }
  m_onClient = false;
  m_steps.clear();
  m_held.reset();
  m_before.reset();
  m_moved.reset();
  return action;
}

BrightnessKeyTracker::Action BrightnessKeyTracker::changed(uint8_t level, double now)
{
  Action action;
  const auto previous = m_level;
  m_level = level;
  // a level equal to the held one is the server's brightness being put back
  if (!m_onClient || !m_held || !previous || level == *previous || level == *m_held) {
    return action;
  }

  if (systemChange(now)) {
    m_held = level;
    return action;
  }

  if (m_steps.empty()) {
    m_firstStep = now;
  }
  m_steps.push_back(level > *previous ? kKeyBrightnessUp : kKeyBrightnessDown);
  m_stepLevel = level;
  action.level = m_held;
  return action;
}

void BrightnessKeyTracker::powerChanged(double now)
{
  m_lastPowerEvent = now;
}

void BrightnessKeyTracker::displayChanged(bool on, double now)
{
  m_displayOn = on;
  powerChanged(now);
}

bool BrightnessKeyTracker::settling() const
{
  return !m_steps.empty();
}

BrightnessKeyTracker::Action BrightnessKeyTracker::settle()
{
  Action action;
  if (m_steps.empty()) {
    return action;
  }

  if (systemChange(m_firstStep)) {
    // a power event came right after the steps, so the system made them: give its level back
    m_held = m_stepLevel;
    action.level = m_stepLevel;
  } else {
    action.keys = std::move(m_steps);
  }
  m_steps.clear();
  return action;
}

bool BrightnessKeyTracker::systemChange(double now) const
{
  // a power event after now counts too, the difference is negative then
  return !m_displayOn || now - m_lastPowerEvent < kSystemWindow;
}
