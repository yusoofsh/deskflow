/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>

class EventQueueTimer;
class IEventQueue;

namespace deskflow {
class IStream;
}

//! Writes bulk messages a little at a time so input messages never wait behind them
/*!
Messages written straight to the stream, like mouse moves, go out at once. Bulk
messages queued here are written about 64 KiB at a time, and the next batch waits
until the stream has handed the previous one to the network.
*/
class PacedStreamWriter
{
public:
  using Write = std::function<void()>;
  //! Queues more messages when there is room, returns false when it has nothing left
  using Refill = std::function<bool()>;

  PacedStreamWriter(deskflow::IStream *stream, IEventQueue *events);
  PacedStreamWriter(const PacedStreamWriter &) = delete;
  PacedStreamWriter &operator=(const PacedStreamWriter &) = delete;
  ~PacedStreamWriter();

  //! Queues a message of about \p bytes, \p write writes it to the stream
  void enqueue(size_t bytes, Write write);

  //! Sets what is asked for more messages when the queue runs empty
  void setRefill(Refill refill);

  //! Writes more if the stream is ready, call after a refill source gains data
  void pump();

private:
  void onFlushed();
  void onTimer();
  void startTimer();
  void stopTimer();

  deskflow::IStream *m_stream;
  IEventQueue *m_events;
  std::deque<std::pair<size_t, Write>> m_queue;
  Refill m_refill;
  bool m_waiting = false;
  // a refill can enqueue, which must not start a second pump inside the first
  bool m_pumping = false;
  std::chrono::steady_clock::time_point m_waitStarted;
  EventQueueTimer *m_timer = nullptr;
};
