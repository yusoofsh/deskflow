/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/KeyTypes.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

//! Turns changes of the server's display brightness into brightness keys
/*!
Laptop brightness keys often never reach the keyboard hook because the firmware
changes the built-in display's brightness by itself. While the cursor is on a
client, each brightness step on the server becomes a brightness key for the
client, and the server's own brightness is put back.

The system changes the brightness too, for example when it dims an idle display
or switches between battery and mains. Those changes come with other power
events, so a step only counts when no power event came kSystemWindow before it
or comes in the kSettleTime after it. Steps wait for kSettleTime before they
become keys.

Brightness levels are in percent, the way Windows reports them.
*/
class BrightnessKeyTracker
{
public:
  //! How long a step waits for a power event that shows the system made it
  static constexpr double kSettleTime = 0.2;
  //! A step this soon after a power event was made by the system
  static constexpr double kSystemWindow = 2.0;
  //! How far leave() may move the brightness to make room for steps
  static constexpr int kMaxMove = 5;

  struct Action
  {
    //! brightness keys to send to the client, in order
    std::vector<KeyID> keys;
    //! brightness to give the server's display
    std::optional<uint8_t> level;
  };

  //! Set the levels the display supports; nothing is tracked without them
  void setLevels(std::vector<uint8_t> levels);

  //! The cursor left for a client
  Action leave();

  //! The cursor came back to the server
  Action enter();

  //! The server's display brightness is now \p level
  Action changed(uint8_t level, double now);

  //! A power event that can change the brightness, like switching to battery
  void powerChanged(double now);

  //! The display turned on, or turned off or dimmed
  void displayChanged(bool on, double now);

  //! True while steps wait for settle()
  bool settling() const;

  //! Turn the steps that waited kSettleTime into keys
  Action settle();

private:
  bool systemChange(double now) const;

  std::vector<uint8_t> m_levels;
  std::optional<uint8_t> m_level;
  bool m_onClient = false;
  bool m_displayOn = true;
  double m_lastPowerEvent = -std::numeric_limits<double>::infinity();

  // brightness the server keeps while the cursor is on a client, unset when not tracking
  std::optional<uint8_t> m_held;
  // brightness before leave() moved it off the brightest or darkest level, and where it moved to
  std::optional<uint8_t> m_before;
  std::optional<uint8_t> m_moved;

  std::vector<KeyID> m_steps;
  double m_firstStep = 0.0;
  uint8_t m_stepLevel = 0;
};
