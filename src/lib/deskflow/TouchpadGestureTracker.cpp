/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/TouchpadGestureTracker.h"

#include <algorithm>
#include <cmath>

std::optional<TouchpadGesture> TouchpadGestureTracker::update(const std::vector<Contact> &contacts, bool canBegin)
{
  // average movement of the contacts that were down in the previous frame too,
  // so a finger landing or lifting doesn't count as movement
  double dx = 0.0;
  double dy = 0.0;
  size_t moved = 0;
  for (const auto &contact : contacts) {
    if (const auto last = m_last.find(contact.id); last != m_last.end()) {
      dx += contact.x - last->second.x;
      dy += contact.y - last->second.y;
      ++moved;
    }
  }
  const bool sameContacts = moved == contacts.size() && moved == m_last.size();
  if (moved > 0) {
    dx /= static_cast<double>(moved);
    dy /= static_cast<double>(moved);
  }

  m_last.clear();
  for (const auto &contact : contacts) {
    m_last[contact.id] = contact;
  }

  using enum TouchpadGesture::Phase;
  const auto count = contacts.size();

  if (m_fingers != 0) {
    if (count < 3) {
      const auto gesture = take(End);
      m_fingers = 0;
      m_pendingX = 0.0;
      m_pendingY = 0.0;
      return gesture;
    }
    m_pendingX += dx;
    m_pendingY += dy;
    const auto gesture = take(Update);
    if (gesture.dx == 0 && gesture.dy == 0) {
      return std::nullopt;
    }
    return gesture;
  }

  if (count < 3 || count > 4 || !sameContacts || !canBegin) {
    m_pendingX = 0.0;
    m_pendingY = 0.0;
    return std::nullopt;
  }

  m_pendingX += dx;
  m_pendingY += dy;
  if (std::hypot(m_pendingX, m_pendingY) < kBeginDistance) {
    return std::nullopt;
  }

  m_fingers = static_cast<uint8_t>(count);
  return take(Begin);
}

TouchpadGesture TouchpadGestureTracker::take(TouchpadGesture::Phase phase)
{
  const auto units = [](double &pending) {
    const double value = std::clamp(std::round(pending * TouchpadGesture::kUnitsPerWidth), -32767.0, 32767.0);
    pending -= value / TouchpadGesture::kUnitsPerWidth;
    return static_cast<int16_t>(value);
  };

  TouchpadGesture gesture;
  gesture.phase = phase;
  gesture.fingers = m_fingers;
  gesture.dx = units(m_pendingX);
  gesture.dy = units(m_pendingY);
  return gesture;
}
