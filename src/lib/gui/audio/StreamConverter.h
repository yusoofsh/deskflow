/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "AudioProtocol.h"

#include <QAudioFormat>
#include <QByteArray>

#include <array>

namespace deskflow::gui::audio {

/**
 * @brief Converts PCM from a device's own format to streamFormat(), keeping resampling state across reads.
 * Extra channels are dropped and mono is copied to both channels.
 */
class StreamConverter
{
public:
  explicit StreamConverter(const QAudioFormat &from);

  QByteArray convert(const QByteArray &pcm);

private:
  using Sample = std::array<float, kChannels>;

  Sample frameAt(qsizetype index) const;

  QAudioFormat m_from;
  QByteArray m_pending;
  qint64 m_inputTicks = 0;
};

} // namespace deskflow::gui::audio
