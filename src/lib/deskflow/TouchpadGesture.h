/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>

//! A three or four finger swipe on the primary computer's touchpad
/*!
A swipe is sent as one Begin, any number of Updates and one End or Cancel. The
computer that replays it decides what the swipe does, so the action follows the
computer the cursor is on, like scrolling does.
*/
struct TouchpadGesture
{
  enum class Phase : uint8_t
  {
    Begin = 1,
    Update = 2,
    End = 3,
    Cancel = 4
  };

  //! Movement units in one touchpad width, used for both axes
  static constexpr int kUnitsPerWidth = 10000;

  Phase phase = Phase::Update;
  uint8_t fingers = 0;
  //! Finger movement since the previous message, positive to the right
  int16_t dx = 0;
  //! Finger movement since the previous message, positive towards the user
  int16_t dy = 0;
};
