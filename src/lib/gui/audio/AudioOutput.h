/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QAudioDevice>
#include <QByteArrayView>

#include <memory>

namespace deskflow::gui::audio {

/**
 * @brief An open playback device that plays streamFormat() PCM in the order it is written.
 */
class AudioOutput
{
public:
  virtual ~AudioOutput() = default;

  /**
   * @return Bytes written that the device has not played yet.
   */
  virtual qsizetype queuedBytes() const = 0;

  /**
   * @return How many leading bytes of @p pcm the device took, always whole frames.
   */
  virtual qsizetype write(QByteArrayView pcm) = 0;
};

/**
 * @return An output on @p device holding at most about @p capacity bytes, or nullptr when it can't open.
 */
std::unique_ptr<AudioOutput> openAudioOutput(const QAudioDevice &device, qsizetype capacity);

} // namespace deskflow::gui::audio
