/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsTouchpad.h"

#include "base/Log.h"

#include <hidsdi.h>

#include <algorithm>

namespace {

constexpr USAGE kGenericDesktopPage = 0x01;
constexpr USAGE kXUsage = 0x30;
constexpr USAGE kYUsage = 0x31;
constexpr USAGE kDigitizerPage = 0x0D;
constexpr USAGE kTouchpadUsage = 0x05;
constexpr USAGE kTipSwitchUsage = 0x42;
constexpr USAGE kContactIdUsage = 0x51;
constexpr USAGE kContactCountUsage = 0x54;

} // namespace

MSWindowsTouchpad::MSWindowsTouchpad(HWND window)
{
  RAWINPUTDEVICE device{};
  device.usUsagePage = kDigitizerPage;
  device.usUsage = kTouchpadUsage;
  // our window is never in the foreground
  device.dwFlags = RIDEV_INPUTSINK;
  device.hwndTarget = window;
  m_registered = RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
  if (!m_registered) {
    LOG_WARN("touchpad swipes can't be read, raw input registration failed: %d", GetLastError());
  }
}

MSWindowsTouchpad::~MSWindowsTouchpad()
{
  if (m_registered) {
    RAWINPUTDEVICE device{};
    device.usUsagePage = kDigitizerPage;
    device.usUsage = kTouchpadUsage;
    device.dwFlags = RIDEV_REMOVE;
    RegisterRawInputDevices(&device, 1, sizeof(device));
  }
}

std::vector<TouchpadGesture> MSWindowsTouchpad::onRawInput(HRAWINPUT input, bool canBegin)
{
  std::vector<TouchpadGesture> gestures;

  UINT size = 0;
  if (GetRawInputData(input, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0) {
    return gestures;
  }
  m_buffer.resize(size);
  if (GetRawInputData(input, RID_INPUT, m_buffer.data(), &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
    return gestures;
  }

  auto *raw = reinterpret_cast<RAWINPUT *>(m_buffer.data());
  if (raw->header.dwType != RIM_TYPEHID) {
    return gestures;
  }

  auto *touchpad = device(raw->header.hDevice);
  if (touchpad == nullptr) {
    return gestures;
  }

  auto &hid = raw->data.hid;
  for (DWORD i = 0; i < hid.dwCount; ++i) {
    auto *report = reinterpret_cast<PCHAR>(hid.bRawData + i * hid.dwSizeHid);
    readReport(*touchpad, report, hid.dwSizeHid, canBegin, gestures);
  }
  return gestures;
}

std::optional<MSWindowsTouchpad::Device> MSWindowsTouchpad::parseDevice(HANDLE handle)
{
  UINT size = 0;
  if (GetRawInputDeviceInfoW(handle, RIDI_PREPARSEDDATA, nullptr, &size) != 0 || size == 0) {
    return std::nullopt;
  }

  Device device;
  device.preparsed.resize(size);
  if (GetRawInputDeviceInfoW(handle, RIDI_PREPARSEDDATA, device.preparsed.data(), &size) == static_cast<UINT>(-1)) {
    return std::nullopt;
  }
  auto data = reinterpret_cast<PHIDP_PREPARSED_DATA>(device.preparsed.data());

  HIDP_CAPS caps{};
  if (HidP_GetCaps(data, &caps) != HIDP_STATUS_SUCCESS || caps.NumberInputValueCaps == 0) {
    return std::nullopt;
  }
  USHORT count = caps.NumberInputValueCaps;
  std::vector<HIDP_VALUE_CAPS> values(count);
  if (HidP_GetValueCaps(HidP_Input, values.data(), &count, data) != HIDP_STATUS_SUCCESS) {
    return std::nullopt;
  }

  const auto axis = [](const HIDP_VALUE_CAPS &value) {
    Axis result{value.LogicalMin, value.LogicalMax, value.PhysicalMin, value.PhysicalMax};
    // a logical maximum with its top bit set is read back as negative
    if (result.max <= result.min && value.BitSize > 0 && value.BitSize < 32) {
      result.max = static_cast<LONG>((1UL << value.BitSize) - 1);
    }
    return result;
  };

  bool hasContactCount = false;
  std::map<USHORT, Axis> xs;
  std::map<USHORT, Axis> ys;
  for (USHORT i = 0; i < count; ++i) {
    const auto &value = values[i];
    const USAGE usage = value.IsRange ? value.Range.UsageMin : value.NotRange.Usage;
    if (value.UsagePage == kDigitizerPage && usage == kContactCountUsage) {
      device.contactCountLink = value.LinkCollection;
      hasContactCount = true;
    } else if (value.UsagePage == kGenericDesktopPage && usage == kXUsage) {
      xs[value.LinkCollection] = axis(value);
    } else if (value.UsagePage == kGenericDesktopPage && usage == kYUsage) {
      ys[value.LinkCollection] = axis(value);
    }
  }

  for (const auto &[link, x] : xs) {
    if (const auto y = ys.find(link); y != ys.end() && x.max > x.min && y->second.max > y->second.min) {
      device.fingers.push_back({link, x, y->second});
    }
  }
  if (!hasContactCount || device.fingers.empty()) {
    return std::nullopt;
  }

  // y uses the width scale, so a diagonal swipe keeps its angle
  const auto &first = device.fingers.front();
  const double physicalWidth = first.x.physicalMax - first.x.physicalMin;
  const double physicalHeight = first.y.physicalMax - first.y.physicalMin;
  if (physicalWidth > 0 && physicalHeight > 0) {
    device.height = physicalHeight / physicalWidth;
  } else {
    device.height = static_cast<double>(first.y.max - first.y.min) / (first.x.max - first.x.min);
  }

  device.usages.resize(HidP_MaxUsageListLength(HidP_Input, kDigitizerPage, data));
  LOG_DEBUG("precision touchpad found, %d contacts per report", static_cast<int>(device.fingers.size()));
  return device;
}

MSWindowsTouchpad::Device *MSWindowsTouchpad::device(HANDLE handle)
{
  auto it = m_devices.find(handle);
  if (it == m_devices.end()) {
    it = m_devices.emplace(handle, parseDevice(handle)).first;
  }
  return it->second.has_value() ? &it->second.value() : nullptr;
}

void MSWindowsTouchpad::readReport(
    Device &device, PCHAR report, ULONG length, bool canBegin, std::vector<TouchpadGesture> &gestures
)
{
  auto data = reinterpret_cast<PHIDP_PREPARSED_DATA>(device.preparsed.data());
  const auto value = [&](USAGE page, USHORT link, USAGE usage, ULONG &result) {
    return HidP_GetUsageValue(HidP_Input, page, link, usage, &result, data, report, length) == HIDP_STATUS_SUCCESS;
  };

  if (ULONG count = 0; value(kDigitizerPage, device.contactCountLink, kContactCountUsage, count) && count > 0) {
    m_frame.clear();
    m_frameContacts = count;
    m_frameRead = 0;
  }

  for (const auto &finger : device.fingers) {
    if (m_frameRead >= m_frameContacts) {
      break;
    }
    ++m_frameRead;

    auto pressed = static_cast<ULONG>(device.usages.size());
    if (pressed == 0 ||
        HidP_GetUsages(HidP_Input, kDigitizerPage, finger.link, device.usages.data(), &pressed, data, report, length) !=
            HIDP_STATUS_SUCCESS) {
      continue;
    }
    const auto end = device.usages.begin() + pressed;
    if (std::find(device.usages.begin(), end, kTipSwitchUsage) == end) {
      continue;
    }

    ULONG id = 0;
    ULONG x = 0;
    ULONG y = 0;
    if (value(kDigitizerPage, finger.link, kContactIdUsage, id) &&
        value(kGenericDesktopPage, finger.link, kXUsage, x) && value(kGenericDesktopPage, finger.link, kYUsage, y)) {
      m_frame.push_back(
          {id, (static_cast<double>(x) - finger.x.min) / (finger.x.max - finger.x.min),
           (static_cast<double>(y) - finger.y.min) / (finger.y.max - finger.y.min) * device.height}
      );
    }
  }

  if (m_frameContacts > 0 && m_frameRead >= m_frameContacts) {
    m_frameContacts = 0;
    if (const auto gesture = m_tracker.update(m_frame, canBegin)) {
      gestures.push_back(*gesture);
    }
  }
}
