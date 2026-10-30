/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioWidget.h"

#include "AudioSharing.h"

#include "common/PlatformInfo.h"
#include "common/Settings.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFormLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QProcess>
#include <QPushButton>
#include <QSignalBlocker>

namespace deskflow::gui::audio {

namespace {

QString installBlackHoleScript()
{
  auto drivers =
      QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/BlackHole")).absolutePath();
  drivers.replace(QStringLiteral("\\"), QStringLiteral("\\\\")).replace(QStringLiteral("\""), QStringLiteral("\\\""));
  return QStringLiteral(
             "do shell script \"cd \" & quoted form of \"%1\" & \" && for d in *.driver; do "
             "rm -rf \\\"/Library/Audio/Plug-Ins/HAL/$d\\\" && cp -R \\\"$d\\\" /Library/Audio/Plug-Ins/HAL/ "
             "|| exit 1; done && killall coreaudiod\" with administrator privileges"
  )
      .arg(drivers);
}

void fillDevices(QComboBox *combo, const QList<QAudioDevice> &devices, const QAudioDevice &selected)
{
  const QSignalBlocker blocker(combo);
  combo->clear();
  for (const auto &device : devices)
    combo->addItem(device.description(), device.id());
  combo->setCurrentIndex(selected.isNull() ? -1 : combo->findData(selected.id()));
}

} // namespace

AudioWidget::AudioWidget(AudioSharing *sharing, QWidget *parent)
    : QGroupBox(tr("Audio"), parent),
      m_role{new QComboBox(this)},
      m_captureLabel{new QLabel(this)},
      m_capture{new QComboBox(this)},
      m_playbackLabel{new QLabel(this)},
      m_playback{new QComboBox(this)},
      m_blackHole{new QLabel(tr("Uses BlackHole Loopback for this Mac's sound and microphone."), this)},
      m_install{new QPushButton(tr("Install BlackHole Loopback"), this)},
      m_status{new QLabel(this)},
      m_devices{new QMediaDevices(this)}
{
  m_role->addItem(tr("Off"), int(Role::Off));
  m_role->addItem(tr("Use this computer's audio"), int(Role::Headset));
  m_role->addItem(tr("Use another computer's audio"), int(Role::Remote));
  m_capture->setPlaceholderText(tr("Choose a device"));
  m_playback->setPlaceholderText(tr("Choose a device"));
  m_blackHole->setWordWrap(true);
  m_status->setWordWrap(true);

  auto layout = new QFormLayout(this);
  layout->addRow(tr("Share audio:"), m_role);
  layout->addRow(m_captureLabel, m_capture);
  layout->addRow(m_playbackLabel, m_playback);
  layout->addRow(m_blackHole);
  layout->addRow(m_install);
  layout->addRow(m_status);

  connect(m_role, &QComboBox::activated, this, &AudioWidget::roleSelected);
  connect(m_capture, &QComboBox::activated, this, [this](int index) {
    Settings::setValue(Settings::Audio::CaptureDevice, m_capture->itemData(index));
  });
  connect(m_playback, &QComboBox::activated, this, [this](int index) {
    Settings::setValue(Settings::Audio::PlaybackDevice, m_playback->itemData(index));
  });
  connect(m_install, &QPushButton::clicked, this, &AudioWidget::installBlackHole);
  connect(m_devices, &QMediaDevices::audioInputsChanged, this, &AudioWidget::refresh);
  connect(m_devices, &QMediaDevices::audioOutputsChanged, this, &AudioWidget::refresh);
  connect(sharing, &AudioSharing::statusChanged, m_status, &QLabel::setText);

  m_status->setText(sharing->status());
  refresh();
}

void AudioWidget::roleSelected(int index)
{
  Settings::setValue(Settings::Audio::CaptureDevice, QVariant());
  Settings::setValue(Settings::Audio::PlaybackDevice, QVariant());
  Settings::setValue(Settings::Audio::Role, m_role->itemData(index));
  refresh();
}

void AudioWidget::installBlackHole()
{
  m_install->setEnabled(false);
  m_install->setText(tr("Installing BlackHole Loopback..."));
  auto process = new QProcess(this);
  connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitStatus != QProcess::NormalExit || exitCode != 0)
      m_status->setText(tr("BlackHole Loopback was not installed: %1")
                            .arg(QString::fromUtf8(process->readAllStandardError()).trimmed()));
    m_install->setEnabled(true);
    m_install->setText(tr("Install BlackHole Loopback"));
    process->deleteLater();
    refresh();
  });
  process->start(QStringLiteral("osascript"), {QStringLiteral("-e"), installBlackHoleScript()});
}

void AudioWidget::refresh()
{
  const auto role = AudioSharing::role();
  {
    const QSignalBlocker blocker(m_role);
    m_role->setCurrentIndex(m_role->findData(int(role)));
  }

  const bool remote = role == Role::Remote;
  const bool macRemote = remote && deskflow::platform::isMac();
  m_captureLabel->setText(remote ? tr("Capture sound from:") : tr("Microphone:"));
  m_playbackLabel->setText(remote ? tr("Microphone appears as:") : tr("Speaker:"));
  fillDevices(m_capture, QMediaDevices::audioInputs(), AudioSharing::captureDevice(role));
  fillDevices(m_playback, QMediaDevices::audioOutputs(), AudioSharing::playbackDevice(role));

  // On a Mac the remote side always uses BlackHole, so its device pickers would only expose plumbing.
  const bool showDevices = role != Role::Off && !macRemote;
  for (QWidget *w :
       {static_cast<QWidget *>(m_captureLabel), static_cast<QWidget *>(m_capture),
        static_cast<QWidget *>(m_playbackLabel), static_cast<QWidget *>(m_playback)})
    w->setVisible(showDevices);
  const bool installed = AudioSharing::blackHoleInstalled();
  m_blackHole->setVisible(macRemote && installed);
  m_install->setVisible(macRemote && !installed);
  m_status->setVisible(role != Role::Off);
}

} // namespace deskflow::gui::audio
