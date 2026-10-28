/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/BrightnessKeyTracker.h"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

//! Sends the brightness keys of a laptop's built-in display to clients
/*!
Windows doesn't see these keys as key presses, only the brightness they set,
see BrightnessKeyTracker. The brightness comes from power setting messages to
the window. Putting it back goes through WMI on a thread of its own, since a
WMI call can take a while.
*/
class MSWindowsBrightness
{
public:
  //! Starts sending power setting messages to \p window
  explicit MSWindowsBrightness(HWND window);
  ~MSWindowsBrightness();

  MSWindowsBrightness(const MSWindowsBrightness &) = delete;
  MSWindowsBrightness &operator=(const MSWindowsBrightness &) = delete;

  //! Handle a WM_POWERBROADCAST PBT_POWERSETTINGCHANGE message
  void onPowerSetting(const POWERBROADCAST_SETTING &setting);

  //! Handle a WM_TIMER message and get the brightness keys for the client
  std::vector<KeyID> onTimer(UINT_PTR timer);

  //! The cursor left for a client
  void leave();

  //! The cursor came back to this computer
  void enter();

private:
  void setLevel(const std::optional<uint8_t> &level);
  void stopSettling();
  void run();

  HWND m_window;
  std::vector<HPOWERNOTIFY> m_notifications;
  BrightnessKeyTracker m_tracker;
  bool m_settling = false;

  // shared with the WMI thread
  std::mutex m_mutex;
  std::condition_variable m_wake;
  std::optional<uint8_t> m_request;
  std::vector<uint8_t> m_levels;
  bool m_stop = false;

  std::thread m_thread;
};
