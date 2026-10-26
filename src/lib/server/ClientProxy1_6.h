/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardChunk.h"
#include "server/ClientProxy1_5.h"

#include <memory>
#include <string>

class FileTransfer;
class PacedStreamWriter;
class Server;
class IEventQueue;

//! Proxy for client implementing protocol version 1.6
class ClientProxy1_6 : public ClientProxy1_5
{
public:
  ClientProxy1_6(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);
  ~ClientProxy1_6() override;

  void setClipboard(ClipboardID id, const IClipboard *clipboard) override;
  bool recvClipboard() override;
  void touchpadGesture(const TouchpadGesture &gesture) override;

  bool acceptsTouchpadGestures() const override
  {
    return m_acceptsTouchpadGestures;
  }

protected:
  bool parseMessage(const uint8_t *code) override;

private:
  void notifyClipboardChanged(ClipboardID id, uint32_t sequence);

  IEventQueue *m_events;
  std::string m_clipboardDataCached;
  ClipboardChunkAssemblyState m_clipboardChunkState;
  bool m_acceptsTouchpadGestures = false;
  std::unique_ptr<PacedStreamWriter> m_writer;
  // declared after m_writer, which it uses, so it is destroyed first
  std::unique_ptr<FileTransfer> m_files;
};
