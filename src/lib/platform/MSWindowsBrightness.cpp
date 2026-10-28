/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsBrightness.h"

#include "base/Log.h"

#include <comutil.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace {

// power settings from winnt.h: GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS, GUID_CONSOLE_DISPLAY_STATE,
// GUID_ACDC_POWER_SOURCE and GUID_POWER_SAVING_STATUS
const GUID kBrightness = {0x8ffee2c6, 0x2d01, 0x46be, {0xad, 0xb9, 0x39, 0x8a, 0xdd, 0xc5, 0xb4, 0xff}};
const GUID kDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};
const GUID kPowerSource = {0x5d3e9a59, 0xe9d5, 0x4b00, {0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48}};
const GUID kPowerSaving = {0xe00958c0, 0xc213, 0x4ace, {0xac, 0x77, 0xfe, 0xcc, 0xed, 0x2e, 0xee, 0xa5}};
constexpr DWORD kDisplayOn = 1;

constexpr UINT_PTR kSettleTimer = 0xB1;

double secondsNow()
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

//! The built-in display's brightness through WMI, for a thread in the multithreaded apartment
class WmiBrightness
{
public:
  WmiBrightness()
  {
    ComPtr<IWbemLocator> locator;
    ComPtr<IWbemClassObject> methods;
    ComPtr<IWbemClassObject> methodClass;
    ComPtr<IWbemClassObject> inputClass;
    _variant_t path;
    if (FAILED(CoCreateInstance(__uuidof(WbemLocator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator))) ||
        FAILED(locator->ConnectServer(_bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &m_wmi)) ||
        FAILED(allowImpersonation(m_wmi.Get())) || !(methods = first(L"WmiMonitorBrightnessMethods")) ||
        FAILED(methods->Get(L"__PATH", 0, &path, nullptr, nullptr)) || path.vt != VT_BSTR ||
        FAILED(m_wmi->GetObject(_bstr_t(L"WmiMonitorBrightnessMethods"), 0, nullptr, &methodClass, nullptr)) ||
        FAILED(methodClass->GetMethod(L"WmiSetBrightness", 0, &inputClass, nullptr)) ||
        FAILED(inputClass->SpawnInstance(0, &m_input))) {
      m_input.Reset();
      return;
    }
    m_path = path.bstrVal;
  }

  //! The brightness levels the display supports, none when it can't be set
  std::vector<uint8_t> levels() const
  {
    std::vector<uint8_t> levels;
    ComPtr<IWbemClassObject> monitor;
    _variant_t value;
    if (!m_input || !(monitor = first(L"WmiMonitorBrightness")) ||
        FAILED(monitor->Get(L"Level", 0, &value, nullptr, nullptr)) || value.vt != (VT_ARRAY | VT_UI1)) {
      return levels;
    }
    void *data = nullptr;
    if (SUCCEEDED(SafeArrayAccessData(value.parray, &data))) {
      const auto *bytes = static_cast<const uint8_t *>(data);
      levels.assign(bytes, bytes + value.parray->rgsabound[0].cElements);
      SafeArrayUnaccessData(value.parray);
    }
    return levels;
  }

  bool set(uint8_t level) const
  {
    // Timeout is a uint32, which WMI takes as VT_I4
    _variant_t timeout(0L);
    _variant_t brightness(static_cast<BYTE>(level));
    return m_input && SUCCEEDED(m_input->Put(L"Timeout", 0, &timeout, 0)) &&
           SUCCEEDED(m_input->Put(L"Brightness", 0, &brightness, 0)) &&
           SUCCEEDED(
               m_wmi->ExecMethod(m_path, _bstr_t(L"WmiSetBrightness"), 0, nullptr, m_input.Get(), nullptr, nullptr)
           );
  }

private:
  // without CoInitializeSecurity, which is for the whole process, each proxy needs this
  static HRESULT allowImpersonation(IUnknown *proxy)
  {
    return CoSetProxyBlanket(
        proxy, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE
    );
  }

  ComPtr<IWbemClassObject> first(const wchar_t *className) const
  {
    ComPtr<IEnumWbemClassObject> instances;
    ComPtr<IWbemClassObject> instance;
    ULONG count = 0;
    if (FAILED(m_wmi->CreateInstanceEnum(_bstr_t(className), WBEM_FLAG_FORWARD_ONLY, nullptr, &instances)) ||
        FAILED(allowImpersonation(instances.Get())) ||
        instances->Next(static_cast<long>(WBEM_INFINITE), 1, &instance, &count) != WBEM_S_NO_ERROR) {
      return nullptr;
    }
    return instance;
  }

  ComPtr<IWbemServices> m_wmi;
  ComPtr<IWbemClassObject> m_input;
  _bstr_t m_path;
};

} // namespace

MSWindowsBrightness::MSWindowsBrightness(HWND window) : m_window(window)
{
  for (const auto *setting : {&kBrightness, &kDisplayState, &kPowerSource, &kPowerSaving}) {
    if (const auto notification = RegisterPowerSettingNotification(window, setting, DEVICE_NOTIFY_WINDOW_HANDLE)) {
      m_notifications.push_back(notification);
    } else {
      LOG_DEBUG("power setting notification failed: %d", GetLastError());
    }
  }
  m_thread = std::thread([this] { run(); });
}

MSWindowsBrightness::~MSWindowsBrightness()
{
  {
    std::scoped_lock lock(m_mutex);
    m_stop = true;
  }
  m_wake.notify_one();
  m_thread.join();

  stopSettling();
  for (const auto notification : m_notifications) {
    UnregisterPowerSettingNotification(notification);
  }
}

void MSWindowsBrightness::onPowerSetting(const POWERBROADCAST_SETTING &setting)
{
  DWORD value = 0;
  if (setting.DataLength < sizeof(value)) {
    return;
  }
  std::memcpy(&value, setting.Data, sizeof(value));

  const double now = secondsNow();
  if (setting.PowerSetting == kBrightness) {
    setLevel(m_tracker.changed(static_cast<uint8_t>(std::min<DWORD>(value, 100)), now).level);
    if (m_tracker.settling() && !m_settling) {
      const auto delay = static_cast<UINT>(BrightnessKeyTracker::kSettleTime * 1000);
      m_settling = SetTimer(m_window, kSettleTimer, delay, nullptr) != 0;
    }
  } else if (setting.PowerSetting == kDisplayState) {
    m_tracker.displayChanged(value == kDisplayOn, now);
  } else if (setting.PowerSetting == kPowerSource || setting.PowerSetting == kPowerSaving) {
    m_tracker.powerChanged(now);
  }
}

std::vector<KeyID> MSWindowsBrightness::onTimer(UINT_PTR timer)
{
  if (timer != kSettleTimer) {
    return {};
  }
  stopSettling();
  auto settled = m_tracker.settle(secondsNow());
  setLevel(settled.level);
  return settled.keys;
}

void MSWindowsBrightness::leave()
{
  {
    std::scoped_lock lock(m_mutex);
    m_tracker.setLevels(m_levels);
  }
  setLevel(m_tracker.leave(secondsNow()).level);
}

void MSWindowsBrightness::enter()
{
  stopSettling();
  setLevel(m_tracker.enter(secondsNow()).level);
}

void MSWindowsBrightness::setLevel(const std::optional<uint8_t> &level)
{
  if (!level) {
    return;
  }
  {
    std::scoped_lock lock(m_mutex);
    m_request = level;
  }
  m_wake.notify_one();
}

void MSWindowsBrightness::stopSettling()
{
  if (m_settling) {
    KillTimer(m_window, kSettleTimer);
    m_settling = false;
  }
}

void MSWindowsBrightness::run()
{
  const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  {
    const WmiBrightness wmi;
    auto levels = wmi.levels();
    if (levels.empty()) {
      LOG_DEBUG("the display brightness can't be set, brightness keys stay on this computer");
    }

    std::unique_lock lock(m_mutex);
    m_levels = std::move(levels);
    while (true) {
      m_wake.wait(lock, [this] { return m_stop || m_request; });
      // a brightness to put back still goes out when stopping
      if (!m_request) {
        break;
      }
      const uint8_t level = *m_request;
      m_request.reset();
      lock.unlock();
      if (!wmi.set(level)) {
        LOG_WARN("could not set the display brightness to %d", level);
      }
      lock.lock();
    }
  }
  if (SUCCEEDED(init)) {
    CoUninitialize();
  }
}
