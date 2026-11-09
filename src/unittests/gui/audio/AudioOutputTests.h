/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/audio/AudioOutput.h"

#include <QTest>

class AudioOutputTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void init();
  void cleanup();
  void takesOnlyWholeFrames();
  void takesNoMoreThanItsCapacity();
  void playsQueuedAudio();

private:
  std::unique_ptr<deskflow::gui::audio::AudioOutput> m_output;
};
