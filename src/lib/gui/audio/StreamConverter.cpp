/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "StreamConverter.h"

#include <algorithm>
#include <cmath>

namespace deskflow::gui::audio {

StreamConverter::StreamConverter(const QAudioFormat &from) : m_from{from}
{
}

StreamConverter::Sample StreamConverter::frameAt(qsizetype index) const
{
  if (index == 0)
    return m_previous;
  const auto *frame = m_pending.constData() + (index - 1) * m_from.bytesPerFrame();
  Sample sample;
  for (int c = 0; c < kChannels; ++c)
    sample[c] = m_from.normalizedSampleValue(frame + std::min(c, m_from.channelCount() - 1) * m_from.bytesPerSample());
  return sample;
}

QByteArray StreamConverter::convert(const QByteArray &pcm)
{
  if (m_from == streamFormat())
    return pcm;

  m_pending.append(pcm);
  const qsizetype frames = m_pending.size() / m_from.bytesPerFrame();
  QByteArray out;
  for (; m_position < frames * kSampleRate; m_position += m_from.sampleRate()) {
    const auto index = m_position / kSampleRate;
    const auto t = float(m_position % kSampleRate) / kSampleRate;
    const auto a = frameAt(index);
    const auto b = frameAt(index + 1);
    for (int c = 0; c < kChannels; ++c) {
      const auto value = std::clamp(a[c] + (b[c] - a[c]) * t, -1.0f, 1.0f);
      const auto sample = static_cast<qint16>(std::lround(value * 32767.0f));
      out.append(reinterpret_cast<const char *>(&sample), sizeof(sample));
    }
  }
  if (frames > 0) {
    m_previous = frameAt(frames);
    m_position -= frames * kSampleRate;
    m_pending.remove(0, frames * m_from.bytesPerFrame());
  }
  return out;
}

} // namespace deskflow::gui::audio
