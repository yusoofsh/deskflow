/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioEngine.h"

#include <QAudioSource>
#include <QDebug>

namespace deskflow::gui::audio {

namespace {
constexpr qsizetype kDeviceBufferBytes = kChunkBytes * 8;
constexpr qint64 kIdleStreamMs = 2000;
} // namespace

AudioEngine::AudioEngine(QObject *parent) : QObject(parent)
{
  m_drainTimer.setInterval(5);
  connect(&m_drainTimer, &QTimer::timeout, this, &AudioEngine::drain);
}

AudioEngine::~AudioEngine()
{
  stop();
}

void AudioEngine::start(Role role, const QAudioDevice &capture, const QAudioDevice &playback)
{
  stop();
  m_sendKind = sentKind(role);
  m_playback = playback;
  if (!m_sendKind)
    return;

  if (capture.isNull()) {
    Q_EMIT error(tr("No audio input device selected"));
  } else {
    const auto format = capture.isFormatSupported(streamFormat()) ? streamFormat() : capture.preferredFormat();
    if (format != streamFormat())
      qInfo().noquote() << "audio: converting" << capture.description() << "from" << format.sampleRate() << "Hz,"
                        << format.channelCount() << "channel(s)";
    m_converter = StreamConverter(format);
    m_source = std::make_unique<QAudioSource>(capture, format);
    m_source->setBufferSize(format.bytesForDuration(40'000));
    m_sourceIo = m_source->start();
    if (!m_sourceIo || m_source->error() != QAudio::NoError) {
      Q_EMIT error(tr("Could not open %1").arg(capture.description()));
      m_source.reset();
      m_sourceIo = nullptr;
    } else {
      connect(m_sourceIo, &QIODevice::readyRead, this, &AudioEngine::readCapture);
    }
  }
  m_drainTimer.start();
}

void AudioEngine::stop()
{
  m_drainTimer.stop();
  if (m_source) {
    m_source->stop();
    m_source.reset();
  }
  m_sourceIo = nullptr;
  m_captureBuffer.clear();
  m_streams.clear();
  m_sendKind.reset();
}

void AudioEngine::readCapture()
{
  if (!m_sourceIo || !m_sendKind)
    return;
  m_captureBuffer.append(m_converter.convert(m_sourceIo->readAll()));
  while (m_captureBuffer.size() >= kChunkBytes) {
    Q_EMIT captured(*m_sendKind, m_captureBuffer.left(kChunkBytes));
    m_captureBuffer.remove(0, kChunkBytes);
  }
}

AudioEngine::Stream *AudioEngine::stream(SourceId source)
{
  if (auto it = m_streams.find(source); it != m_streams.end())
    return it->get();
  if (m_playback.isNull())
    return nullptr;

  auto s = std::make_shared<Stream>();
  s->output = openAudioOutput(m_playback, kDeviceBufferBytes);
  if (!s->output) {
    Q_EMIT error(tr("Could not open %1").arg(m_playback.description()));
    return nullptr;
  }
  m_streams.insert(source, s);
  return s.get();
}

void AudioEngine::play(SourceId source, const QByteArray &pcm)
{
  if (!m_sendKind || pcm.size() % kBytesPerFrame != 0)
    return;
  auto s = stream(source);
  if (!s)
    return;
  s->pending.append(pcm);
  s->lastFrame.start();
  s->pending.remove(0, staleBytes(s->pending.size(), s->output->queuedBytes()));
  drain();
}

void AudioEngine::drain()
{
  for (auto it = m_streams.begin(); it != m_streams.end();) {
    auto &s = *it.value();
    if (s.pending.isEmpty() && s.lastFrame.isValid() && s.lastFrame.elapsed() > kIdleStreamMs) {
      it = m_streams.erase(it);
      continue;
    }
    if (const auto written = s.output->write(s.pending); written > 0)
      s.pending.remove(0, written);
    ++it;
  }
}

} // namespace deskflow::gui::audio
