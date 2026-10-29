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
#include <QFormLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QSignalBlocker>

namespace deskflow::gui::audio {

namespace {

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
      m_hint{new QLabel(this)},
      m_status{new QLabel(this)},
      m_devices{new QMediaDevices(this)}
{
  m_role->addItem(tr("Off"), int(Role::Off));
  m_role->addItem(tr("Use this computer's headset"), int(Role::Headset));
  m_role->addItem(tr("Send sound to the shared headset"), int(Role::Remote));
  m_capture->setPlaceholderText(tr("Choose a device"));
  m_playback->setPlaceholderText(tr("Choose a device"));

  m_hint->setWordWrap(true);
  m_hint->setOpenExternalLinks(true);
  m_hint->setText(
      tr("To send this Mac's sound, install <a href=\"https://github.com/ExistentialAudio/BlackHole\">BlackHole</a> "
         "2ch and 16ch (<code>brew install blackhole-2ch blackhole-16ch</code>), then restart Deskflow.")
  );
  m_status->setWordWrap(true);

  auto layout = new QFormLayout(this);
  layout->addRow(tr("Share audio:"), m_role);
  layout->addRow(m_captureLabel, m_capture);
  layout->addRow(m_playbackLabel, m_playback);
  layout->addRow(m_hint);
  layout->addRow(m_status);

  connect(m_role, &QComboBox::activated, this, &AudioWidget::roleSelected);
  connect(m_capture, &QComboBox::activated, this, [this](int index) {
    Settings::setValue(Settings::Audio::CaptureDevice, m_capture->itemData(index));
  });
  connect(m_playback, &QComboBox::activated, this, [this](int index) {
    Settings::setValue(Settings::Audio::PlaybackDevice, m_playback->itemData(index));
  });
  connect(m_devices, &QMediaDevices::audioInputsChanged, this, &AudioWidget::refresh);
  connect(m_devices, &QMediaDevices::audioOutputsChanged, this, &AudioWidget::refresh);
  connect(sharing, &AudioSharing::statusChanged, m_status, &QLabel::setText);

  m_status->setText(sharing->status());
  refresh();
}

void AudioWidget::roleSelected(int index)
{
  // Device choices mean different things per role (headset mic vs loopback), so start from that role's defaults.
  Settings::setValue(Settings::Audio::CaptureDevice, QVariant());
  Settings::setValue(Settings::Audio::PlaybackDevice, QVariant());
  Settings::setValue(Settings::Audio::Role, m_role->itemData(index));
  refresh();
}

void AudioWidget::refresh()
{
  const auto role = AudioSharing::role();
  {
    const QSignalBlocker blocker(m_role);
    m_role->setCurrentIndex(m_role->findData(int(role)));
  }

  const bool remote = role == Role::Remote;
  m_captureLabel->setText(remote ? tr("Capture sound from:") : tr("Headset mic:"));
  m_playbackLabel->setText(remote ? tr("Headset mic appears as:") : tr("Headset speaker:"));
  fillDevices(m_capture, QMediaDevices::audioInputs(), AudioSharing::captureDevice(role));
  fillDevices(m_playback, QMediaDevices::audioOutputs(), AudioSharing::playbackDevice(role));

  const bool on = role != Role::Off;
  for (QWidget *w :
       {static_cast<QWidget *>(m_captureLabel), static_cast<QWidget *>(m_capture),
        static_cast<QWidget *>(m_playbackLabel), static_cast<QWidget *>(m_playback)})
    w->setVisible(on);
  m_hint->setVisible(remote && deskflow::platform::isMac() && !AudioSharing::blackHoleInstalled());
}

} // namespace deskflow::gui::audio
