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
  action.level = hold(*level);
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
  if (level == m_wanted) {
    m_wanted.reset();
  }
  // levels this tracker asked for are the server's brightness being put back
  if (isRequested(level, now)) {
    return action;
  }
  // the system or the user moved the brightness since, so it isn't asked for again
  m_wanted.reset();
  if (!m_onClient || !m_held || !previous || level == *previous || level == *m_held) {
    return action;
  }

  if (systemChange(now)) {
    m_stepLevel = level;
    action.level = hold(level);
    return request(std::move(action), now);
  }

  if (m_steps.empty()) {
    m_firstStep = now;
  }
  // the step goes from the last level reported, which is the held one unless putting it back failed
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
  // a display that's off or dimmed has the brightness the system gave it
  if (!on) {
    m_wanted.reset();
  }
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
    hold(m_stepLevel);
    action.level = m_held;
  }
  m_steps.clear();
  return request(std::move(action), now);
}

std::optional<uint8_t> BrightnessKeyTracker::retry(double now)
{
  if (!m_wanted || m_retries == kMaxRetries) {
    m_wanted.reset();
    return std::nullopt;
  }
  ++m_retries;
  // the level counts as asked for from now on
  std::erase_if(m_requested, [this](const auto &r) { return r.level == *m_wanted; });
  m_requested.push_back({*m_wanted, now});
  return m_wanted;
}

std::optional<uint8_t> BrightnessKeyTracker::hold(uint8_t level)
{
  m_held = level;
  // on the brightest level a step up changes nothing, so it can't be seen; the same goes down
  std::optional<uint8_t> room;
  if (level >= m_levels.back()) {
    room = m_levels[m_levels.size() - 2];
  } else if (level <= m_levels.front()) {
    room = m_levels[1];
  }
  // a big jump would dim or light up the display while the cursor is away, and a display
  // that's off or dimmed has the brightness the system gave it
  if (room && std::abs(*room - level) <= kMaxMove && m_displayOn) {
    m_held = room;
    m_before = level;
    m_moved = room;
    return room;
  }
  return std::nullopt;
}

BrightnessKeyTracker::Action BrightnessKeyTracker::request(Action action, double now)
{
  if (action.level) {
    m_requested.push_back({*action.level, now});
    m_wanted = action.level;
    m_retries = 0;
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
