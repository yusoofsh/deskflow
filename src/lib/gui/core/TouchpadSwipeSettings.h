/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

//! This computer's own three and four finger swipe settings
/*!
While the server sends touchpad swipes to a client, this computer must not act on
them too. Only Windows needs this, so elsewhere these do nothing.
*/
namespace deskflow::gui::touchpadSwipeSettings {

//! Turns this computer's swipes off, saving the user's settings first
void turnOff();

//! Puts back the settings saved by turnOff(), if any
void restore();

} // namespace deskflow::gui::touchpadSwipeSettings
