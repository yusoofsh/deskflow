/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "TouchpadSwipeSettings.h"

#include "common/Settings.h"

#ifdef Q_OS_WIN
#include <QStringList>

#include <array>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>
#include <servprov.h>
#endif

namespace deskflow::gui::touchpadSwipeSettings {

#ifdef Q_OS_WIN

namespace {

const wchar_t *const kKeyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\PrecisionTouchPad";

struct Swipe
{
  const wchar_t *valueName;
  int shellSettingId;
};
const std::array<Swipe, 2> kSwipes = {{{L"ThreeFingerSlideEnabled", 0x11}, {L"FourFingerSlideEnabled", 0x12}}};

// Undocumented: the shell caches touchpad settings and only reloads one when told that it changed.
struct __declspec(uuid("4214f6fa-eb36-4e2f-9ca2-23fdc1832df7")) IImmersiveSettingsCache : public IUnknown
{
  virtual HRESULT STDMETHODCALLTYPE OnSettingChanged(int settingId) = 0;
};
const CLSID kImmersiveShell = {0xc2f03a33, 0x21f5, 0x47fa, {0xb4, 0xbb, 0x15, 0x63, 0x62, 0xa2, 0xf2, 0x39}};
const GUID kSettingsCacheService = {0x53660488, 0x8855, 0x460b, {0xa9, 0xab, 0x5c, 0xfc, 0x6b, 0x50, 0x12, 0xca}};

void notifyShell()
{
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IServiceProvider *services = nullptr;
  IImmersiveSettingsCache *cache = nullptr;
  if (FAILED(CoCreateInstance(kImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&services))) ||
      FAILED(services->QueryService(kSettingsCacheService, IID_PPV_ARGS(&cache)))) {
    qWarning("could not tell the shell about the touchpad swipe settings, they apply at the next sign in");
  } else {
    for (const auto &swipe : kSwipes) {
      cache->OnSettingChanged(swipe.shellSettingId);
    }
  }
  if (cache != nullptr) {
    cache->Release();
  }
  if (services != nullptr) {
    services->Release();
  }
  if (SUCCEEDED(init)) {
    CoUninitialize();
  }
}

} // namespace

void turnOff()
{
  if (Settings::value(Settings::Gui::TouchpadGestureBackup).isValid()) {
    return;
  }

  HKEY key = nullptr;
  if (RegCreateKeyExW(
          HKEY_CURRENT_USER, kKeyPath, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr
      ) != ERROR_SUCCESS) {
    qWarning("could not open the touchpad settings, this computer keeps acting on swipes");
    return;
  }

  QStringList backup;
  for (const auto &swipe : kSwipes) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    const auto result = RegGetValueW(key, nullptr, swipe.valueName, RRF_RT_REG_DWORD, nullptr, &value, &size);
    backup.append(result == ERROR_SUCCESS ? QString::number(value) : QStringLiteral("none"));
  }
  // saved before anything changes, so a crash can't lose the user's settings
  Settings::setValue(Settings::Gui::TouchpadGestureBackup, backup.join(' '));

  const DWORD off = 0;
  for (const auto &swipe : kSwipes) {
    RegSetValueExW(key, swipe.valueName, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&off), sizeof(off));
  }
  RegCloseKey(key);
  notifyShell();
  qInfo("touchpad swipes turned off on this computer while they go to a client");
}

void restore()
{
  const auto backup = Settings::value(Settings::Gui::TouchpadGestureBackup).toString().split(' ', Qt::SkipEmptyParts);
  if (backup.isEmpty()) {
    return;
  }

  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kKeyPath, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
    qWarning("could not open the touchpad settings to turn swipes back on");
    return;
  }
  for (size_t i = 0; i < kSwipes.size() && i < static_cast<size_t>(backup.size()); ++i) {
    bool ok = false;
    const DWORD value = backup.at(static_cast<qsizetype>(i)).toULong(&ok);
    if (ok) {
      RegSetValueExW(key, kSwipes[i].valueName, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
    } else {
      RegDeleteValueW(key, kSwipes[i].valueName);
    }
  }
  RegCloseKey(key);
  Settings::setValue(Settings::Gui::TouchpadGestureBackup, QVariant());
  notifyShell();
  qInfo("touchpad swipes turned back on for this computer");
}

#else

void turnOff()
{
}

void restore()
{
}

#endif

} // namespace deskflow::gui::touchpadSwipeSettings
