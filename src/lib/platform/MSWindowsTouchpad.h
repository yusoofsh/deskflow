/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/TouchpadGesture.h"
#include "deskflow/TouchpadGestureTracker.h"

#include <map>
#include <optional>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

//! Reads finger contacts from precision touchpads and turns them into swipes
class MSWindowsTouchpad
{
public:
  //! Starts sending precision touchpad input to \p window as WM_INPUT
  explicit MSWindowsTouchpad(HWND window);
  ~MSWindowsTouchpad();

  MSWindowsTouchpad(const MSWindowsTouchpad &) = delete;
  MSWindowsTouchpad &operator=(const MSWindowsTouchpad &) = delete;

  //! Get the swipe messages for a WM_INPUT message
  /*!
  See TouchpadGestureTracker::update() for \p canBegin.
  */
  std::vector<TouchpadGesture> onRawInput(HRAWINPUT input, bool canBegin);

private:
  struct Axis
  {
    LONG min = 0;
    LONG max = 0;
    LONG physicalMin = 0;
    LONG physicalMax = 0;
  };

  struct Finger
  {
    USHORT link = 0;
    Axis x;
    Axis y;
  };

  struct Device
  {
    std::vector<BYTE> preparsed;
    USHORT contactCountLink = 0;
    std::vector<Finger> fingers;
    // touchpad height in touchpad widths
    double height = 1.0;
    std::vector<USAGE> usages;
  };

  static std::optional<Device> parseDevice(HANDLE handle);
  Device *device(HANDLE handle);
  void readReport(Device &device, PCHAR report, ULONG length, bool canBegin, std::vector<TouchpadGesture> &gestures);

  bool m_registered = false;
  std::map<HANDLE, std::optional<Device>> m_devices;
  std::vector<BYTE> m_buffer;
  TouchpadGestureTracker m_tracker;

  // a frame's contacts can be split over several reports; the first one has the contact count
  std::vector<TouchpadGestureTracker::Contact> m_frame;
  ULONG m_frameContacts = 0;
  ULONG m_frameRead = 0;
};
