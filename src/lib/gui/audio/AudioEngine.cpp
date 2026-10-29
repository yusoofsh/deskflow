/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioEngine.h"

#include <QAudioSink>
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

  const auto format = streamFormat();
  if (capture.isNull()) {
    Q_EMIT error(tr("No audio input device selected"));
  } else {
    if (!capture.isFormatSupported(format))
      qWarning() << "audio: input" << capture.description() << "may not support 48 kHz stereo";
    m_source = std::make_unique<QAudioSource>(capture, format);
    m_source->setBufferSize(kChunkBytes * 4);
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
  for (auto &s : m_streams)
    s->sink->stop();
  m_streams.clear();
  m_sendKind.reset();
}

void AudioEngine::readCapture()
{
  if (!m_sourceIo || !m_sendKind)
    return;
  m_captureBuffer.append(m_sourceIo->readAll());
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
  s->sink = std::make_unique<QAudioSink>(m_playback, streamFormat());
  s->sink->setBufferSize(kDeviceBufferBytes);
  s->io = s->sink->start();
  if (!s->io) {
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
  s->pending.remove(0, staleBytes(s->pending.size(), s->sink->bufferSize() - s->sink->bytesFree()));
  drain();
}

void AudioEngine::drain()
{
  for (auto it = m_streams.begin(); it != m_streams.end();) {
    auto &s = *it.value();
    if (s.pending.isEmpty() && s.lastFrame.isValid() && s.lastFrame.elapsed() > kIdleStreamMs) {
      s.sink->stop();
      it = m_streams.erase(it);
      continue;
    }
    const auto free = s.sink->bytesFree() / kBytesPerFrame * kBytesPerFrame;
    const auto n = std::min<qsizetype>(free, s.pending.size());
    if (n > 0) {
      const auto written = s.io->write(s.pending.constData(), n);
      if (written > 0)
        s.pending.remove(0, written);
    }
    ++it;
  }
}

} // namespace deskflow::gui::audio
