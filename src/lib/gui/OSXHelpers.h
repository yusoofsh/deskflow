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
