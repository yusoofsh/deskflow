/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/TouchpadGesture.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

//! Turns touchpad contact frames into three and four finger swipes
/*!
Contact positions are in touchpad widths: a finger moving across the whole
touchpad moves 1 along x, and y uses the same scale.
*/
class TouchpadGestureTracker
{
public:
  struct Contact
  {
    uint32_t id = 0;
    double x = 0.0;
    double y = 0.0;
  };

  //! How far the fingers move, in touchpad widths, before a swipe begins
  static constexpr double kBeginDistance = 0.03;

  /*!
  Feed the contacts touching the touchpad in one frame and get the gesture
  message for it, if any. A swipe only begins while \p canBegin is true, but a
  swipe that began goes on until the fingers lift.
  */
  std::optional<TouchpadGesture> update(const std::vector<Contact> &contacts, bool canBegin);

private:
  // moves the whole units of pending movement into a message
  TouchpadGesture take(TouchpadGesture::Phase phase);

  std::map<uint32_t, Contact> m_last;
  // fingers of the swipe in progress, 0 when there is none
  uint8_t m_fingers = 0;
  double m_pendingX = 0.0;
  double m_pendingY = 0.0;
};
