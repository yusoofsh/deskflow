/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_6.h"

#include "base/Log.h"
#include "deskflow/ClipboardChunk.h"
#include "deskflow/FileClipboard.h"
#include "deskflow/FileTransfer.h"
#include "deskflow/PacedStreamWriter.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/StreamChunker.h"
#include "io/IStream.h"
#include "server/Server.h"

#include <cstring>

//
// ClientProxy1_6
//

ClientProxy1_6::ClientProxy1_6(const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events)
    : ClientProxy1_5(name, stream, server, events),
      m_events(events),
      m_writer(std::make_unique<PacedStreamWriter>(stream, events)),
      m_files(std::make_unique<FileTransfer>(stream, *m_writer))
{
  m_events->addHandler(EventTypes::ClipboardSending, this, [this](const auto &e) {
    ClipboardChunk::sendPaced(*m_writer, getStream(), e.getDataObject());
  });
}

ClientProxy1_6::~ClientProxy1_6()
{
  m_events->removeHandler(EventTypes::ClipboardSending, this);
}

void ClientProxy1_6::setClipboard(ClipboardID id, const IClipboard *clipboard)
{
  // ignore if this clipboard is already clean
  if (m_clipboard[id].m_dirty) {
    // this clipboard is now clean
    m_clipboard[id].m_dirty = false;
    Clipboard::copy(&m_clipboard[id].m_clipboard, clipboard);

    std::string data = m_clipboard[id].m_clipboard.marshall();

    // the client can fetch the files on this clipboard, and nothing else
    if (id == kClipboardClipboard)
      m_files->offer(deskflow::fileclipboard::copiedPaths(m_clipboard[id].m_clipboard));

    size_t size = data.size();
    LOG_DEBUG("sending clipboard %d to \"%s\"", id, getName().c_str());

    StreamChunker::sendClipboard(data, size, id, 0, m_events, this);
  }
}

bool ClientProxy1_6::recvClipboard()
{
  // parse message
  ClipboardID id;
  uint32_t seq;

  auto r = ClipboardChunk::assemble(
      getStream(), m_clipboardDataCached, id, seq, m_clipboardChunkState, m_server->getMaximumClipboardSizeBytes()
  );

  if (r == TransferState::Started) {
    size_t size = ClipboardChunk::getExpectedSize(m_clipboardChunkState);
    LOG_DEBUG("receiving clipboard %d size=%zu", id, size);
  } else if (r == TransferState::Finished) {
    LOG(
        (CLOG_DEBUG "received client \"%s\" clipboard %d seqnum=%d, size=%zu", getName().c_str(), id, seq,
         m_clipboardDataCached.size())
    );
    // save clipboard
    auto &clipboard = m_clipboard[id].m_clipboard;
    clipboard.unmarshall(m_clipboardDataCached, 0);
    m_clipboard[id].m_sequenceNumber = seq;
    m_clipboardDataCached.clear();
    m_clipboardDataCached.shrink_to_fit();

    // copied files stay on the client until they are fetched, the paths mean nothing here
    const QStringList copied = deskflow::fileclipboard::copiedPaths(clipboard);
    deskflow::fileclipboard::removeCopiedPaths(clipboard);
    if (id != kClipboardClipboard || copied.isEmpty()) {
      if (id == kClipboardClipboard)
        m_files->cancelFetch();
      notifyClipboardChanged(id, seq);
      return true;
    }

    m_files->fetch(copied, [this, id, seq](const QStringList &localPaths) {
      if (!localPaths.isEmpty())
        deskflow::fileclipboard::setCopiedPaths(m_clipboard[id].m_clipboard, localPaths);
      notifyClipboardChanged(id, seq);
    });
  } else if (r == TransferState::Error) {
    return false;
  }

  return true;
}

void ClientProxy1_6::touchpadGesture(const TouchpadGesture &gesture)
{
  LOG_VERBOSE(
      "send touchpad gesture to \"%s\" phase=%d fingers=%d %+d,%+d", getName().c_str(), static_cast<int>(gesture.phase),
      gesture.fingers, gesture.dx, gesture.dy
  );
  ProtocolUtil::writef(
      getStream(), kMsgDTouchpadGesture, static_cast<int32_t>(gesture.phase), static_cast<int32_t>(gesture.fingers),
      static_cast<int32_t>(gesture.dx), static_cast<int32_t>(gesture.dy)
  );
}

bool ClientProxy1_6::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgCTouchpadGestures, 4) == 0) {
    LOG_DEBUG("client \"%s\" replays touchpad gestures", getName().c_str());
    m_acceptsTouchpadGestures = true;
    return true;
  }
  return m_files->handleMessage(code) || ClientProxy1_5::parseMessage(code);
}

void ClientProxy1_6::notifyClipboardChanged(ClipboardID id, uint32_t sequence)
{
  auto *info = new ClipboardInfo;
  info->m_id = id;
  info->m_sequenceNumber = sequence;
  m_events->addEvent(Event(EventTypes::ClipboardChanged, getEventTarget(), info));
}
