/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioProtocol.h"

#include <QtEndian>

#include <algorithm>

namespace deskflow::gui::audio {

namespace {
constexpr qsizetype kLengthBytes = 4;
constexpr qsizetype kHeaderBytes = 3;
constexpr qsizetype kMaxQueuedBytes = kChunkBytes * 6;
constexpr qsizetype kQueuedAfterDropBytes = kChunkBytes * 3;
} // namespace

QAudioFormat streamFormat()
{
  QAudioFormat format;
  format.setSampleRate(kSampleRate);
  format.setChannelCount(kChannels);
  format.setSampleFormat(QAudioFormat::Int16);
  return format;
}

quint16 audioPort(quint16 corePort)
{
  return corePort + 1;
}

std::optional<FrameKind> sentKind(Role role)
{
  switch (role) {
  case Role::Headset:
    return FrameKind::Mic;
  case Role::Remote:
    return FrameKind::Speaker;
  case Role::Off:
    break;
  }
  return std::nullopt;
}

qsizetype staleBytes(qsizetype pending, qsizetype playing)
{
  const auto queued = pending + playing;
  if (queued <= kMaxQueuedBytes)
    return 0;
  return std::min(pending, queued - kQueuedAfterDropBytes) / kBytesPerFrame * kBytesPerFrame;
}

bool shouldDeliver(FrameKind kind, Role from, Role to)
{
  switch (kind) {
  case FrameKind::Speaker:
    return from == Role::Remote && to == Role::Headset;
  case FrameKind::Mic:
    return from == Role::Headset && to == Role::Remote;
  case FrameKind::Hello:
    break;
  }
  return false;
}

QByteArray encodeHello(Role role)
{
  return QByteArray(1, static_cast<char>(role));
}

std::optional<Role> decodeHello(const QByteArray &payload)
{
  if (payload.size() != 1)
    return std::nullopt;
  switch (static_cast<Role>(payload.at(0))) {
  case Role::Off:
    return Role::Off;
  case Role::Headset:
    return Role::Headset;
  case Role::Remote:
    return Role::Remote;
  }
  return std::nullopt;
}

QByteArray encode(const Frame &frame)
{
  QByteArray out(kLengthBytes + kHeaderBytes, Qt::Uninitialized);
  qToBigEndian<quint32>(static_cast<quint32>(kHeaderBytes + frame.payload.size()), out.data());
  out[kLengthBytes] = static_cast<char>(frame.kind);
  qToBigEndian<quint16>(frame.source, out.data() + kLengthBytes + 1);
  out.append(frame.payload);
  return out;
}

void FrameReader::append(const QByteArray &bytes)
{
  if (!m_failed)
    m_buffer.append(bytes);
}

std::optional<Frame> FrameReader::next()
{
  if (m_failed || m_buffer.size() < kLengthBytes)
    return std::nullopt;

  const auto length = qFromBigEndian<quint32>(m_buffer.constData());
  if (length < kHeaderBytes || length > kHeaderBytes + kMaxPayload) {
    m_failed = true;
    m_buffer.clear();
    return std::nullopt;
  }
  if (m_buffer.size() < kLengthBytes + qsizetype(length))
    return std::nullopt;

  const auto kind = static_cast<FrameKind>(m_buffer.at(kLengthBytes));
  if (kind != FrameKind::Hello && kind != FrameKind::Speaker && kind != FrameKind::Mic) {
    m_failed = true;
    m_buffer.clear();
    return std::nullopt;
  }

  Frame frame;
  frame.kind = kind;
  frame.source = qFromBigEndian<quint16>(m_buffer.constData() + kLengthBytes + 1);
  frame.payload = m_buffer.mid(kLengthBytes + kHeaderBytes, length - kHeaderBytes);
  m_buffer.remove(0, kLengthBytes + length);
  return frame;
}

} // namespace deskflow::gui::audio
