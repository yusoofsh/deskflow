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

BrightnessKeyTracker::Action BrightnessKeyTracker::leave(double now)
{
  Action action;
  m_onClient = true;
  m_steps.clear();
  m_held.reset();
  // a level asked for but not reported yet is where the brightness is going
  forgetOldRequests(now);
  const auto level = m_requested.empty() ? m_level : std::optional<uint8_t>(m_requested.back().level);
  // without levels the server's brightness can't be put back, so the keys stay on the server
  if (!level || m_levels.size() < 2) {
    return action;
  }

  // on the brightest level a step up changes nothing, so it can't be seen; the same goes down
  m_held = level;
  if (*level >= m_levels.back()) {
    m_held = m_levels[m_levels.size() - 2];
  } else if (*level <= m_levels.front()) {
    m_held = m_levels[1];
  }
  if (std::abs(*m_held - *level) > kMaxMove) {
    // a big jump would dim or light up the display while the cursor is away
    m_held = level;
  }
  if (m_held != level) {
    m_before = level;
    m_moved = m_held;
    action.level = m_held;
  }
  return request(std::move(action), now);
}

BrightnessKeyTracker::Action BrightnessKeyTracker::enter(double now)
{
  Action action;
  // undo leave() unless the system changed the brightness since
  if (m_before && m_held == m_moved) {
    action.level = m_before;
  }
  m_onClient = false;
  m_steps.clear();
  m_held.reset();
  m_before.reset();
  m_moved.reset();
  return request(std::move(action), now);
}

BrightnessKeyTracker::Action BrightnessKeyTracker::changed(uint8_t level, double now)
{
  Action action;
  const auto previous = m_level;
  m_level = level;
  // levels this tracker asked for, and the held one, are the server's brightness being put back
  if (isRequested(level, now) || !m_onClient || !m_held || !previous || level == *previous || level == *m_held) {
    return action;
  }

  if (systemChange(now)) {
    m_held = level;
    m_stepLevel = level;
    return action;
  }

  if (m_steps.empty()) {
    m_firstStep = now;
  }
  const KeyID key = level > *previous ? kKeyBrightnessUp : kKeyBrightnessDown;
  m_steps.push_back(key);
  m_stepLevel = level;
  action.keys = {key};
  action.level = m_held;
  return request(std::move(action), now);
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

BrightnessKeyTracker::Action BrightnessKeyTracker::settle(double now)
{
  Action action;
  if (m_steps.empty()) {
    return action;
  }

  if (systemChange(m_firstStep)) {
    // a power event came right after the steps, so the system made them: undo the
    // keys on the client and give the server the system's level back
    for (auto key = m_steps.rbegin(); key != m_steps.rend(); ++key) {
      action.keys.push_back(*key == kKeyBrightnessUp ? kKeyBrightnessDown : kKeyBrightnessUp);
    }
    m_held = m_stepLevel;
    action.level = m_stepLevel;
  }
  m_steps.clear();
  return request(std::move(action), now);
}

BrightnessKeyTracker::Action BrightnessKeyTracker::request(Action action, double now)
{
  if (action.level) {
    m_requested.push_back({*action.level, now});
  }
  return action;
}

bool BrightnessKeyTracker::isRequested(uint8_t level, double now)
{
  forgetOldRequests(now);
  const auto found =
      std::find_if(m_requested.begin(), m_requested.end(), [level](const auto &r) { return r.level == level; });
  if (found == m_requested.end()) {
    return false;
  }
  // levels asked for before this one were passed on the way to it
  m_requested.erase(m_requested.begin(), found + 1);
  return true;
}

void BrightnessKeyTracker::forgetOldRequests(double now)
{
  std::erase_if(m_requested, [now](const auto &r) { return now - r.time > kRequestTime; });
}

bool BrightnessKeyTracker::systemChange(double now) const
{
  // a power event after now counts too, the difference is negative then
  return !m_displayOn || now - m_lastPowerEvent < kSystemWindow;
}
