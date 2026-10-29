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
or comes in the kSettleTime after it. A step's key goes out at once; when a
power event follows within kSettleTime, settle() takes the keys back with
opposite keys and gives the server the system's brightness.

Windows sometimes drops a brightness asked for right after a change, so a level
that isn't reported is asked for again with retry().

Brightness levels are in percent, the way Windows reports them.
*/
class BrightnessKeyTracker
{
public:
  //! How long after a step a power event still shows the system made it
  static constexpr double kSettleTime = 0.2;
  //! A step this soon after a power event was made by the system
  static constexpr double kSystemWindow = 2.0;
  //! How far leave() may move the brightness to make room for steps
  static constexpr int kMaxMove = 5;
  //! How long a level this tracker asked for may take to be reported
  static constexpr double kRequestTime = 1.0;
  //! How long to wait for a level asked for before retry()
  static constexpr double kRetryTime = 0.3;
  //! How often retry() asks for the same level
  static constexpr int kMaxRetries = 3;

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
  Action leave(double now);

  //! The cursor came back to the server
  Action enter(double now);

  //! The server's display brightness is now \p level
  Action changed(uint8_t level, double now);

  //! A power event that can change the brightness, like switching to battery
  void powerChanged(double now);

  //! The display turned on, or turned off or dimmed
  void displayChanged(bool on, double now);

  //! True while steps wait for settle()
  bool settling() const;

  //! Take back the steps of the last kSettleTime if a power event shows the system made them
  Action settle(double now);

  //! The level to ask for again when the last one asked for wasn't reported in kRetryTime
  std::optional<uint8_t> retry(double now);

private:
  // keeps \p level on the server, or the level next to it when \p level is the brightest or
  // darkest one; gives the level to set when it's the one next to it
  std::optional<uint8_t> hold(uint8_t level);

  struct Request
  {
    uint8_t level = 0;
    double time = 0.0;
  };

  // notes the level of \p action as asked for
  Action request(Action action, double now);
  // true when \p level is one this tracker asked for, which then counts as reported
  bool isRequested(uint8_t level, double now);
  void forgetOldRequests(double now);
  bool systemChange(double now) const;

  std::vector<uint8_t> m_levels;
  // the server's brightness, as last reported
  std::optional<uint8_t> m_level;
  bool m_onClient = false;
  bool m_displayOn = true;
  double m_lastPowerEvent = -std::numeric_limits<double>::infinity();

  // brightness the server keeps while the cursor is on a client, unset when not tracking
  std::optional<uint8_t> m_held;
  // brightness before leave() moved it off the brightest or darkest level, and where it moved to
  std::optional<uint8_t> m_before;
  std::optional<uint8_t> m_moved;

  // levels asked for and not reported yet, oldest first
  std::vector<Request> m_requested;
  // the last level asked for until it's reported, and how often retry() asked for it
  std::optional<uint8_t> m_wanted;
  int m_retries = 0;

  std::vector<KeyID> m_steps;
  double m_firstStep = 0.0;
  uint8_t m_stepLevel = 0;
};
