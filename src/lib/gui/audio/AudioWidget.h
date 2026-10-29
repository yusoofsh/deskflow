/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QGroupBox>

class QComboBox;
class QLabel;
class QMediaDevices;

namespace deskflow::gui::audio {

class AudioSharing;

class AudioWidget : public QGroupBox
{
  Q_OBJECT

public:
  explicit AudioWidget(AudioSharing *sharing, QWidget *parent = nullptr);

private:
  void refresh();
  void roleSelected(int index);

  QComboBox *m_role = nullptr;
  QLabel *m_captureLabel = nullptr;
  QComboBox *m_capture = nullptr;
  QLabel *m_playbackLabel = nullptr;
  QComboBox *m_playback = nullptr;
  QLabel *m_hint = nullptr;
  QLabel *m_status = nullptr;
  QMediaDevices *m_devices = nullptr;
};

} // namespace deskflow::gui::audio
