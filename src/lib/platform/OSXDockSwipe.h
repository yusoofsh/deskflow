/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/TouchpadGesture.h"

#include <cstdint>
#include <deque>

//! Replays touchpad swipes as the macOS swipes for Mission Control and Spaces
/*!
macOS has no public way to post these, so this builds the HID dock swipe event a
trackpad sends and posts it through SkyLight, which needs Accessibility like the
rest of the input Deskflow posts.
*/
class OSXDockSwipe
{
public:
  OSXDockSwipe();

  //! Test if the private calls needed to post swipes were found
  bool isAvailable() const;

  void handle(const TouchpadGesture &gesture);

  //! End a swipe in progress without completing it
  void cancel();

private:
  struct Sample
  {
    uint64_t time;
    double progress;
  };

  void begin(const TouchpadGesture &gesture);
  void move(const TouchpadGesture &gesture);
  void end();
  // progress per second over the last moments of the swipe
  double velocity() const;
  void post(uint32_t phase, double velocity) const;

  void *m_setEvent = nullptr;
  void *m_eventClass = nullptr;
  bool m_active = false;
  bool m_horizontal = false;
  double m_progress = 0.0;
  std::deque<Sample> m_samples;
};
