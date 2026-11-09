/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioOutput.h"

#include "AudioProtocol.h"

#include <QAudioSink>

#include <algorithm>

namespace deskflow::gui::audio {

namespace {

class QtAudioOutput : public AudioOutput
{
public:
  explicit QtAudioOutput(const QAudioDevice &device) : m_sink(device, streamFormat())
  {
  }

  ~QtAudioOutput() override
  {
    m_sink.stop();
  }

  bool open(qsizetype capacity)
  {
    m_sink.setBufferSize(capacity);
    m_io = m_sink.start();
    return m_io != nullptr;
  }

  qsizetype queuedBytes() const override
  {
    return m_sink.bufferSize() - m_sink.bytesFree();
  }

  qsizetype write(QByteArrayView pcm) override
  {
    const auto n = std::min(m_sink.bytesFree(), pcm.size()) / kBytesPerFrame * kBytesPerFrame;
    if (n <= 0)
      return 0;
    return std::max<qint64>(m_io->write(pcm.data(), n), 0);
  }

private:
  QAudioSink m_sink;
  QIODevice *m_io = nullptr;
};

} // namespace

std::unique_ptr<AudioOutput> openAudioOutput(const QAudioDevice &device, qsizetype capacity)
{
  auto output = std::make_unique<QtAudioOutput>(device);
  if (!output->open(capacity))
    return nullptr;
  return output;
}

} // namespace deskflow::gui::audio
