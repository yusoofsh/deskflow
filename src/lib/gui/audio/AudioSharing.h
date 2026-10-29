/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "AudioEngine.h"
#include "AudioLink.h"

#include "common/Settings.h"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QObject>

#include <optional>

namespace deskflow::gui::audio {

/**
 * @brief Runs shared audio while the Deskflow core runs, using the role and devices from Settings.
 */
class AudioSharing : public QObject
{
  Q_OBJECT

public:
  explicit AudioSharing(QObject *parent = nullptr);
  ~AudioSharing() override;

  void setCoreRunning(bool running);

  static Role role();
  static QAudioDevice captureDevice(Role role);
  static QAudioDevice playbackDevice(Role role);
  static bool blackHoleInstalled();

  QString status() const
  {
    return m_status;
  }

Q_SIGNALS:
  void statusChanged(const QString &status);

private:
  struct EngineDevices
  {
    QByteArray capture;
    QByteArray playback;
    bool operator==(const EngineDevices &) const = default;
  };

  void restart();
  void followDevices();
  void setStatus(const QString &status);
  void switchSystemDevices(Role role, const QAudioDevice &capture, const QAudioDevice &playback);
  void restoreSystemDevices();

  AudioLink m_link;
  AudioEngine m_engine;
  QMediaDevices m_devices;
  std::optional<EngineDevices> m_engineDevices;
  bool m_coreRunning = false;
  bool m_active = false;
  QString m_status;
  QString m_savedDefaultOutput;
  QString m_savedDefaultInput;
};

} // namespace deskflow::gui::audio
