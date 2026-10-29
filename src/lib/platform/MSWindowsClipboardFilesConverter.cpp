/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardFilesConverter.h"

#include "base/Log.h"
#include "deskflow/FileClipboard.h"

#include <QDir>

#include <ShlObj.h>
#include <UserEnv.h>
#include <Wtsapi32.h>

#include <cstring>
#include <cwchar>
#include <vector>

namespace {

const DWORD kPrintTimeoutMs = 2000;

bool isLocalSystem()
{
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    return false;

  alignas(TOKEN_USER) BYTE buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
  DWORD size = 0;
  const BOOL ok = GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &size);
  CloseHandle(token);
  return ok && IsWellKnownSid(reinterpret_cast<TOKEN_USER *>(buffer)->User.Sid, WinLocalSystemSid);
}

//! Reads what a process writes to \p pipe until it exits, false if it doesn't exit in time
bool readUntilExit(HANDLE process, HANDLE pipe, std::string &output)
{
  char buffer[4096]; // NOSONAR -- Idiomatic Win32
  DWORD read = 0;
  const ULONGLONG deadline = GetTickCount64() + kPrintTimeoutMs;
  while (WaitForSingleObject(process, 10) != WAIT_OBJECT_0) {
    if (GetTickCount64() > deadline)
      return false;

    // a full pipe would block the helper, so read what it has written so far
    DWORD available = 0;
    while (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
      const auto wanted = static_cast<DWORD>(available < sizeof(buffer) ? available : sizeof(buffer));
      if (!ReadFile(pipe, buffer, wanted, &read, nullptr))
        break;
      output.append(buffer, read);
    }
  }

  // the helper had the only other end of the pipe, so this reads the rest and then fails
  while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
    output.append(buffer, read);
  return true;
}

//! Runs this program with kPrintArg as the signed in user and returns what it prints
std::string printAsSessionUser()
{
  DWORD session = 0;
  HANDLE token = nullptr;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) || !WTSQueryUserToken(session, &token)) {
    LOG_DEBUG("can't read copied files as the signed in user, no user token, error=%d", GetLastError());
    return {};
  }

  SECURITY_ATTRIBUTES pipeAttributes = {sizeof(pipeAttributes), nullptr, TRUE};
  HANDLE readPipe = nullptr;
  HANDLE writePipe = nullptr;
  LPVOID environment = nullptr;
  if (!CreatePipe(&readPipe, &writePipe, &pipeAttributes, 0) || !CreateEnvironmentBlock(&environment, token, FALSE)) {
    LOG_WARN("can't read copied files as the signed in user, error=%d", GetLastError());
    if (readPipe != nullptr) {
      CloseHandle(readPipe);
      CloseHandle(writePipe);
    }
    CloseHandle(token);
    return {};
  }
  SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

  // only the pipe is inherited, the helper runs as a less trusted user than this process
  SIZE_T listSize = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &listSize);
  std::vector<char> listBuffer(listSize);
  auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(listBuffer.data());
  InitializeProcThreadAttributeList(attributes, 1, 0, &listSize);
  UpdateProcThreadAttribute(
      attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &writePipe, sizeof(writePipe), nullptr, nullptr
  );

  wchar_t desktop[] = L"winsta0\\Default"; // NOSONAR -- Idiomatic Win32
  STARTUPINFOEXW startup = {};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.lpDesktop = desktop;
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdOutput = writePipe;
  startup.lpAttributeList = attributes;

  wchar_t program[MAX_PATH] = {}; // NOSONAR -- Idiomatic Win32
  GetModuleFileNameW(nullptr, program, MAX_PATH);
  const auto printArg = QString::fromLatin1(MSWindowsClipboardFilesConverter::kPrintArg).toStdWString();
  std::wstring command = L"\"" + std::wstring(program) + L"\" " + printArg;

  PROCESS_INFORMATION process = {};
  const DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
  const BOOL started = CreateProcessAsUserW(
      token, program, command.data(), nullptr, nullptr, TRUE, flags, environment, nullptr, &startup.StartupInfo,
      &process
  );
  const DWORD startError = GetLastError();

  // the helper holds the only write end now, so the pipe ends when it exits
  CloseHandle(writePipe);
  DeleteProcThreadAttributeList(attributes);
  DestroyEnvironmentBlock(environment);
  CloseHandle(token);

  if (!started) {
    LOG_WARN("can't read copied files as the signed in user, failed to start helper, error=%d", startError);
    CloseHandle(readPipe);
    return {};
  }

  std::string output;
  DWORD exitCode = 1;
  if (readUntilExit(process.hProcess, readPipe, output)) {
    GetExitCodeProcess(process.hProcess, &exitCode);
  } else {
    LOG_WARN("can't read copied files as the signed in user, helper didn't finish in time");
    TerminateProcess(process.hProcess, 1);
  }
  CloseHandle(readPipe);
  CloseHandle(process.hProcess);
  CloseHandle(process.hThread);

  if (exitCode != 0)
    return {};
  return output;
}

} // namespace

//
// MSWindowsClipboardFilesConverter
//

int MSWindowsClipboardFilesConverter::printCopiedFiles()
{
  // another program can have the clipboard open for a moment
  bool opened = OpenClipboard(nullptr);
  for (int attempt = 1; !opened && attempt < 5; ++attempt) {
    Sleep(10);
    opened = OpenClipboard(nullptr);
  }
  if (!opened)
    return 1;

  std::string files;
  if (HANDLE data = GetClipboardData(CF_HDROP); data != nullptr)
    files = MSWindowsClipboardFilesConverter().toIClipboard(data);
  CloseClipboard();

  DWORD written = 0;
  if (!files.empty() &&
      !WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), files.data(), static_cast<DWORD>(files.size()), &written, nullptr))
    return 1;
  return 0;
}

std::string MSWindowsClipboardFilesConverter::readAsSessionUser()
{
  // Explorer's files reach the clipboard through OLE, which also puts a DataObject format there
  static const bool s_isLocalSystem = isLocalSystem();
  static const UINT s_dataObjectFormat = RegisterClipboardFormat(TEXT("DataObject"));
  if (!s_isLocalSystem || IsClipboardFormatAvailable(CF_HDROP) || !IsClipboardFormatAvailable(s_dataObjectFormat))
    return {};

  // the server reads the clipboard every time the cursor leaves, so only start the helper when it changes
  static DWORD s_sequence = 0;
  static std::string s_files;
  if (const DWORD sequence = GetClipboardSequenceNumber(); sequence != s_sequence) {
    s_sequence = sequence;
    s_files = printAsSessionUser();
    LOG_DEBUG(
        "read %d copied item(s) as the signed in user",
        static_cast<int>(deskflow::fileclipboard::decodePaths(s_files).size())
    );
  }
  return s_files;
}

IClipboard::Format MSWindowsClipboardFilesConverter::getFormat() const
{
  return IClipboard::Format::Files;
}

UINT MSWindowsClipboardFilesConverter::getWin32Format() const
{
  return CF_HDROP;
}

HANDLE MSWindowsClipboardFilesConverter::fromIClipboard(const std::string &data) const
{
  // DROPFILES is followed by NUL terminated UTF-16 paths and one more NUL
  std::wstring paths;
  for (const auto &path : deskflow::fileclipboard::decodePaths(data)) {
    paths += QDir::toNativeSeparators(path).toStdWString();
    paths += L'\0';
  }
  if (paths.empty())
    return nullptr;
  paths += L'\0';

  const size_t pathBytes = paths.size() * sizeof(wchar_t);
  HGLOBAL gData = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + pathBytes);
  if (gData == nullptr)
    return nullptr;

  auto *dropFiles = static_cast<DROPFILES *>(GlobalLock(gData));
  if (dropFiles == nullptr) {
    GlobalFree(gData);
    return nullptr;
  }
  dropFiles->pFiles = sizeof(DROPFILES);
  dropFiles->fWide = TRUE;
  std::memcpy(reinterpret_cast<char *>(dropFiles) + sizeof(DROPFILES), paths.data(), pathBytes);
  GlobalUnlock(gData);
  return gData;
}

std::string MSWindowsClipboardFilesConverter::toIClipboard(HANDLE data) const
{
  const auto *dropFiles = static_cast<const DROPFILES *>(GlobalLock(data));
  if (dropFiles == nullptr)
    return {};

  // the paths may be UTF-16 or ANSI, and the list ends with an empty path
  const SIZE_T size = GlobalSize(data);
  QStringList paths;
  if (dropFiles->pFiles < size) {
    const char *begin = reinterpret_cast<const char *>(dropFiles) + dropFiles->pFiles;
    const char *end = reinterpret_cast<const char *>(dropFiles) + size;
    if (dropFiles->fWide) {
      const auto *path = reinterpret_cast<const wchar_t *>(begin);
      const auto *last = reinterpret_cast<const wchar_t *>(end);
      while (path < last && *path != L'\0') {
        const size_t length = wcsnlen(path, static_cast<size_t>(last - path));
        paths.append(QString::fromWCharArray(path, static_cast<qsizetype>(length)));
        path += length + 1;
      }
    } else {
      const char *path = begin;
      while (path < end && *path != '\0') {
        const size_t length = strnlen(path, static_cast<size_t>(end - path));
        paths.append(QString::fromLocal8Bit(path, static_cast<qsizetype>(length)));
        path += length + 1;
      }
    }
  }
  GlobalUnlock(data);
  return deskflow::fileclipboard::encodePaths(paths);
}
