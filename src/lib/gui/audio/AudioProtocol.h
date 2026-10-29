/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QAudioFormat>
#include <QByteArray>

#include <optional>

namespace deskflow::gui::audio {

/**
 * @brief What this computer contributes to the shared audio link.
 * Headset: owns the real speaker and mic, plays every Remote's audio and sends its mic to them.
 * Remote: sends its system audio to the Headset and exposes the Headset mic locally.
 */
enum class Role : quint8
{
  Off = 0,
  Headset = 1,
  Remote = 2
};

enum class FrameKind : quint8
{
  Hello = 1,
  Speaker = 2,
  Mic = 3
};

using SourceId = quint16;
inline constexpr SourceId kLocalSource = 0;

struct Frame
{
  FrameKind kind = FrameKind::Hello;
  SourceId source = kLocalSource;
  QByteArray payload;

  bool operator==(const Frame &) const = default;
};

inline constexpr int kSampleRate = 48000;
inline constexpr int kChannels = 2;
inline constexpr int kBytesPerFrame = kChannels * 2;
inline constexpr int kChunkBytes = kSampleRate / 100 * kBytesPerFrame; // 10 ms
inline constexpr qsizetype kMaxPayload = 64 * 1024;

QAudioFormat streamFormat();

quint16 audioPort(quint16 corePort);

/**
 * @return The kind of frame a computer with @p role captures and sends, if any.
 */
std::optional<FrameKind> sentKind(Role role);

/**
 * @return How many of the oldest @p pending bytes to drop, given @p playing bytes already queued in the device.
 * Audio that arrives late or in a burst would otherwise stay that far behind for as long as the stream plays.
 */
qsizetype staleBytes(qsizetype pending, qsizetype playing);

/**
 * @brief The routing rule of the link: Remote speaker audio reaches Headsets, Headset mic audio reaches Remotes.
 */
bool shouldDeliver(FrameKind kind, Role from, Role to);

QByteArray encodeHello(Role role);
std::optional<Role> decodeHello(const QByteArray &payload);

/**
 * @brief Wire layout: u32 length (of everything after it), u8 kind, u16 source, payload. Big endian.
 */
QByteArray encode(const Frame &frame);

class FrameReader
{
public:
  void append(const QByteArray &bytes);

  /**
   * @return The next complete frame, or nullopt when more bytes are needed or the stream is corrupt.
   */
  std::optional<Frame> next();

  bool failed() const
  {
    return m_failed;
  }

private:
  QByteArray m_buffer;
  bool m_failed = false;
};

} // namespace deskflow::gui::audio
