/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/PacedStreamWriter.h"

#include "base/IEventQueue.h"
#include "io/IStream.h"

namespace {
// about 10 ms of Wi-Fi, so input never waits longer than that
constexpr size_t kBatchBytes = 64 * 1024;
// resume if a flushed event never arrives, so a transfer can't stall
constexpr auto kFlushTimeout = std::chrono::seconds(1);
constexpr double kTimerInterval = 0.5;
} // namespace

PacedStreamWriter::PacedStreamWriter(deskflow::IStream *stream, IEventQueue *events)
    : m_stream(stream),
      m_events(events)
{
  // sent by the socket each time its output buffer becomes empty
  m_events->addHandler(EventTypes::StreamOutputFlushed, m_stream->getEventTarget(), [this](const auto &) {
    onFlushed();
  });
}

PacedStreamWriter::~PacedStreamWriter()
{
  stopTimer();
  m_events->removeHandler(EventTypes::StreamOutputFlushed, m_stream->getEventTarget());
}

void PacedStreamWriter::enqueue(size_t bytes, Write write)
{
  m_queue.emplace_back(bytes, std::move(write));
  pump();
}

void PacedStreamWriter::setRefill(Refill refill)
{
  m_refill = std::move(refill);
}

void PacedStreamWriter::pump()
{
  if (m_waiting || m_pumping)
    return;
  m_pumping = true;

  size_t written = 0;
  while (written < kBatchBytes) {
    if (m_queue.empty() && !(m_refill && m_refill()))
      break;
    if (m_queue.empty())
      break;

    auto [bytes, write] = std::move(m_queue.front());
    m_queue.pop_front();
    write();
    written += bytes;
  }
  m_pumping = false;

  if (written > 0) {
    m_waiting = true;
    m_waitStarted = std::chrono::steady_clock::now();
    startTimer();
  } else {
    stopTimer();
  }
}

void PacedStreamWriter::onFlushed()
{
  m_waiting = false;
  pump();
}

void PacedStreamWriter::onTimer()
{
  if (m_waiting && std::chrono::steady_clock::now() - m_waitStarted > kFlushTimeout)
    onFlushed();
}

void PacedStreamWriter::startTimer()
{
  if (m_timer != nullptr)
    return;
  m_timer = m_events->newTimer(kTimerInterval, nullptr);
  m_events->addHandler(EventTypes::Timer, m_timer, [this](const auto &) { onTimer(); });
}

void PacedStreamWriter::stopTimer()
{
  if (m_timer == nullptr)
    return;
  m_events->removeHandler(EventTypes::Timer, m_timer);
  m_events->deleteTimer(m_timer);
  m_timer = nullptr;
}
