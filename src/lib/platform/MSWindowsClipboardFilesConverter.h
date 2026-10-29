/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsClipboard.h"

//! Convert to/from files copied in Explorer (CF_HDROP)
class MSWindowsClipboardFilesConverter : public IMSWindowsClipboardConverter
{
public:
  //! Makes deskflow-core print the copied files and exit, see readAsSessionUser()
  static constexpr auto kPrintArg = "--print-copied-files";

  MSWindowsClipboardFilesConverter() = default;
  ~MSWindowsClipboardFilesConverter() override = default;

  //! Writes the files on the clipboard to stdout in the Files format
  //! \return the process exit code
  static int printCopiedFiles();

  //! Files copied in Explorer that a process running as SYSTEM can't see
  /*!
  Explorer's copied files are hidden from SYSTEM, which is how the core runs when it's
  elevated, so this runs deskflow-core with kPrintArg as the signed in user to read them.
  Returns an empty string when there are none or this process isn't SYSTEM.
  Must be called while the clipboard is closed.
  */
  static std::string readAsSessionUser();

  // IMSWindowsClipboardConverter overrides
  IClipboard::Format getFormat() const override;
  UINT getWin32Format() const override;
  HANDLE fromIClipboard(const std::string &) const override;
  std::string toIClipboard(HANDLE) const override;
};
