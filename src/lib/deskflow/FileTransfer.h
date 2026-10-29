/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/FileClipboard.h"

#include <QSet>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>

class PacedStreamWriter;

namespace deskflow {
class IStream;
}

//! Copies the files behind a shared Files clipboard over one connection
/*!
The clipboard only carries paths. The computer that receives a Files clipboard
fetches the files with kMsgDFileRequest, and the computer that copied them
answers with kMsgDFileData messages written through a PacedStreamWriter. A
computer only sends paths it offered on its own clipboard.
*/
class FileTransfer
{
public:
  //! Called with the local copies of the fetched paths, or an empty list when fetching failed
  using Fetched = std::function<void(const QStringList &localPaths)>;

  //! Largest total size fetched for one clipboard
  static constexpr uint64_t kMaxBytes = 1024ULL * 1024 * 1024;

  //! \p writer is shared with the connection's clipboard data so both are paced together
  FileTransfer(deskflow::IStream *stream, PacedStreamWriter &writer);
  FileTransfer(const FileTransfer &) = delete;
  FileTransfer &operator=(const FileTransfer &) = delete;
  ~FileTransfer();

  //! Lets the other computer fetch these local paths, replacing the paths offered before
  void offer(const QStringList &paths);

  //! Fetches paths from the other computer into a new folder, cancelling an earlier fetch
  void fetch(const QStringList &remotePaths, Fetched fetched);

  //! Stops the current fetch without calling its callback
  void cancelFetch();

  //! Reads a file transfer message from the stream
  //! \return false if \p code isn't a file transfer message
  bool handleMessage(const uint8_t *code);

private:
  struct Sending;
  struct Receiving;

  void recvRequest();
  void recvData();
  void recvCancel();
  bool sendNext();
  void stopSending();
  void sendData(uint32_t id, uint8_t mark, const std::string &payload);
  void closeReceivedFile();
  bool receiveEntry(const std::string &payload);
  bool receiveData(const std::string &payload);
  void finishFetch(bool ok, const QString &reason);

  deskflow::IStream *m_stream;
  PacedStreamWriter &m_writer;
  QSet<QString> m_offered;
  std::unique_ptr<Sending> m_sending;
  std::unique_ptr<Receiving> m_receiving;
  uint32_t m_lastFetchId = 0;
};
