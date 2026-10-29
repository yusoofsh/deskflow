/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "AudioProtocol.h"

#include <QAudioDevice>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>

#include <memory>

class QAudioSink;
class QAudioSource;
class QIODevice;

namespace deskflow::gui::audio {

/**
 * @brief Local audio I/O for one role: captures from one input device, plays each remote source to one output device.
 */
class AudioEngine : public QObject
{
  Q_OBJECT

public:
  explicit AudioEngine(QObject *parent = nullptr);
  ~AudioEngine() override;

  void start(Role role, const QAudioDevice &capture, const QAudioDevice &playback);
  void stop();
  void play(SourceId source, const QByteArray &pcm);

Q_SIGNALS:
  void captured(deskflow::gui::audio::FrameKind kind, const QByteArray &pcm);
  void error(const QString &message);

private:
  struct Stream
  {
    std::unique_ptr<QAudioSink> sink;
    QIODevice *io = nullptr;
    QByteArray pending;
    QElapsedTimer lastFrame;
  };

  void readCapture();
  void drain();
  Stream *stream(SourceId source);

  std::optional<FrameKind> m_sendKind;
  QAudioDevice m_playback;
  std::unique_ptr<QAudioSource> m_source;
  QIODevice *m_sourceIo = nullptr;
  QByteArray m_captureBuffer;
  QHash<SourceId, std::shared_ptr<Stream>> m_streams;
  QTimer m_drainTimer;
};

} // namespace deskflow::gui::audio
