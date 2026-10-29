/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioSharing.h"

#include "gui/TlsUtility.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMediaDevices>

#ifdef Q_OS_MACOS
#include "gui/OSXHelpers.h"
#endif

namespace deskflow::gui::audio {

namespace {

// macOS sees one "BlackHole Loopback" output and one "BlackHole Loopback" input, from two drivers.
// Separate drivers keep the shared microphone out of the captured Mac sound.
// Deskflow uses each driver's mirror, which macOS keeps out of the Sound menu.
const auto kLoopbackName = QStringLiteral("BlackHole Loopback");
const auto kSoundCaptureId = QStringLiteral("DeskflowLoopbackOut_2_UID");
const auto kMicFeedId = QStringLiteral("DeskflowLoopbackIn_2_UID");

QAudioDevice pick(const QList<QAudioDevice> &devices, const QByteArray &savedId, const QStringList &preferred)
{
  for (const auto &device : devices) {
    if (!savedId.isEmpty() && device.id() == savedId)
      return device;
  }
  for (const auto &name : preferred) {
    for (const auto &device : devices) {
      if (device.description().contains(name, Qt::CaseInsensitive) || device.id().contains(name.toUtf8()))
        return device;
    }
  }
  return {};
}

} // namespace

AudioSharing::AudioSharing(QObject *parent) : QObject(parent)
{
  connect(&m_engine, &AudioEngine::captured, &m_link, &AudioLink::sendLocal);
  connect(&m_link, &AudioLink::frameReceived, this, [this](const Frame &frame) {
    m_engine.play(frame.source, frame.payload);
  });
  connect(&m_link, &AudioLink::statusChanged, this, &AudioSharing::setStatus);
  connect(&m_link, &AudioLink::peersChanged, this, [this](int count) {
    if (count > 0)
      setStatus(tr("Sharing audio with %n computer(s)", "", count));
  });
  connect(&m_engine, &AudioEngine::error, this, &AudioSharing::setStatus);
  connect(&m_devices, &QMediaDevices::audioInputsChanged, this, &AudioSharing::followDevices);
  connect(&m_devices, &QMediaDevices::audioOutputsChanged, this, &AudioSharing::followDevices);
  // The main window is not always destroyed on quit, so the destructor alone can leave a Mac on BlackHole.
  connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
    m_link.stop();
    m_engine.stop();
    restoreSystemDevices();
    m_active = false;
  });

  connect(Settings::instance(), &Settings::settingsChanged, this, [this](const QString &key) {
    if (key.startsWith(QStringLiteral("audio/")) || key == Settings::Core::CoreMode ||
        key == Settings::Client::RemoteHost || key == Settings::Core::Port)
      restart();
  });
  restart();
}

AudioSharing::~AudioSharing()
{
  m_link.stop();
  m_engine.stop();
  restoreSystemDevices();
}

Role AudioSharing::role()
{
  const auto value = Settings::value(Settings::Audio::Role).toInt();
  return decodeHello(QByteArray(1, static_cast<char>(value))).value_or(Role::Off);
}

QAudioDevice AudioSharing::captureDevice(Role role)
{
  const auto inputs = QMediaDevices::audioInputs();
  const auto saved = Settings::value(Settings::Audio::CaptureDevice).toByteArray();
  if (role == Role::Remote)
    return pick(inputs, saved, {kSoundCaptureId});
  auto device = pick(inputs, saved, {});
  return device.isNull() ? QMediaDevices::defaultAudioInput() : device;
}

QAudioDevice AudioSharing::playbackDevice(Role role)
{
  const auto outputs = QMediaDevices::audioOutputs();
  const auto saved = Settings::value(Settings::Audio::PlaybackDevice).toByteArray();
  if (role == Role::Remote)
    return pick(outputs, saved, {kMicFeedId});
  auto device = pick(outputs, saved, {});
  return device.isNull() ? QMediaDevices::defaultAudioOutput() : device;
}

bool AudioSharing::blackHoleInstalled()
{
  return !pick(QMediaDevices::audioInputs(), {}, {kSoundCaptureId}).isNull() &&
         !pick(QMediaDevices::audioOutputs(), {}, {kMicFeedId}).isNull();
}

void AudioSharing::setCoreRunning(bool running)
{
  if (running == m_coreRunning)
    return;
  m_coreRunning = running;
  restart();
}

void AudioSharing::restart()
{
  m_link.stop();
  m_engine.stop();
  m_engineDevices.reset();
  if (m_active)
    restoreSystemDevices();
  m_active = false;

  const auto localRole = role();
  const auto mode = Settings::value(Settings::Core::CoreMode).value<Settings::CoreMode>();
  if (localRole == Role::Off) {
    setStatus(tr("Audio sharing is off"));
    return;
  }
  if (!m_coreRunning || mode == Settings::CoreMode::None) {
    setStatus(tr("Audio starts when Deskflow starts"));
    return;
  }

  m_active = true;
  followDevices();

  const LinkTls tls{
      TlsUtility::isEnabled(), Settings::value(Settings::Security::Certificate).toString(),
      mode == Settings::CoreMode::Server ? Settings::tlsTrustedClientsDb() : Settings::tlsTrustedServersDb(),
      mode == Settings::CoreMode::Server ? Settings::value(Settings::Security::CheckPeers).toBool() : true
  };
  const auto port = audioPort(Settings::value(Settings::Core::Port).toUInt());
  if (mode == Settings::CoreMode::Server) {
    m_link.listen(port, localRole, tls);
    return;
  }

  QStringList hosts;
  for (const auto &host : Settings::value(Settings::Client::RemoteHost).toString().split(',')) {
    if (const auto trimmed = host.trimmed(); !trimmed.isEmpty())
      hosts.append(trimmed);
  }
  m_link.connectTo(hosts, port, localRole, tls);
}

void AudioSharing::followDevices()
{
  if (!m_active)
    return;
  const auto localRole = role();
  const auto capture = captureDevice(localRole);
  const auto playback = playbackDevice(localRole);
  const EngineDevices devices{capture.id(), playback.id()};
  if (m_engineDevices == devices)
    return;
  m_engineDevices = devices;
  switchSystemDevices(localRole, capture, playback);
  m_engine.start(localRole, capture, playback);
}

void AudioSharing::setStatus(const QString &status)
{
  if (status == m_status)
    return;
  m_status = status;
  qInfo().noquote() << "audio:" << status;
  Q_EMIT statusChanged(status);
}

void AudioSharing::switchSystemDevices(Role role, const QAudioDevice &capture, const QAudioDevice &playback)
{
#ifdef Q_OS_MACOS
  if (role != Role::Remote || capture.isNull())
    return;
  if (m_savedDefaultOutput.isEmpty()) {
    m_savedDefaultOutput = macDefaultAudioDevice(false);
    m_savedDefaultInput = macDefaultAudioDevice(true);
  }
  macSetDefaultAudioDevice(false, kLoopbackName);
  if (!playback.isNull())
    macSetDefaultAudioDevice(true, kLoopbackName);
#else
  Q_UNUSED(role)
  Q_UNUSED(capture)
  Q_UNUSED(playback)
#endif
}

void AudioSharing::restoreSystemDevices()
{
#ifdef Q_OS_MACOS
  if (!m_savedDefaultOutput.isEmpty())
    macSetDefaultAudioDevice(false, m_savedDefaultOutput);
  if (!m_savedDefaultInput.isEmpty())
    macSetDefaultAudioDevice(true, m_savedDefaultInput);
#endif
  m_savedDefaultOutput.clear();
  m_savedDefaultInput.clear();
}

} // namespace deskflow::gui::audio
