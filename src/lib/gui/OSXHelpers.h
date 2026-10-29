/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

#include <functional>

void requestOSXNotificationPermission();
bool isOSXDevelopmentBuild();
bool showOSXNotification(const QString &title, const QString &body);
bool isOSXInterfaceStyleDark();
void forceAppActive();
void macOSNativeHide();
//! Add or remove Deskflow as a login item, a login item turned off in System Settings is left off
void setLaunchAtLogin(bool enabled);
void installQuitHandler(std::function<bool()> shouldQuit);

/**
 * @return The name of the current system default audio input or output device.
 */
QString macDefaultAudioDevice(bool input);

/**
 * @brief Makes the audio device called @p name the system default input or output.
 */
bool macSetDefaultAudioDevice(bool input, const QString &name);

/**
 * @return Whether an audio device with @p uid exists, hidden or not.
 */
bool macAudioDeviceExists(const QString &uid);

/**
 * @return Whether the audio device with @p uid exists and is hidden from apps' device lists.
 */
bool macAudioDeviceHidden(const QString &uid);

/**
 * @brief Gives this process a device with id @p uid and name @p name that plays or records through the hidden
 * device @p hiddenUid. No other app sees it, and it goes away when this process quits.
 */
bool macCreatePrivateAudioDevice(const QString &uid, const QString &name, const QString &hiddenUid);
