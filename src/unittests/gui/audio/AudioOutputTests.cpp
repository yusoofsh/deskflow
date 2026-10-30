/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioOutputTests.h"

#include "gui/audio/AudioProtocol.h"

#include <QMediaDevices>

using namespace deskflow::gui::audio;

namespace {
constexpr qsizetype kCapacity = kChunkBytes * 8;
} // namespace

void AudioOutputTests::init()
{
  const auto device = QMediaDevices::defaultAudioOutput();
  if (device.isNull())
    QSKIP("No audio output device");
  m_output = openAudioOutput(device, kCapacity);
  if (!m_output)
    QSKIP(qPrintable(QStringLiteral("Could not open %1").arg(device.description())));
}

void AudioOutputTests::cleanup()
{
  m_output.reset();
}

void AudioOutputTests::takesOnlyWholeFrames()
{
  const QByteArray pcm(kBytesPerFrame * 10 + 1, '\0');
  const auto taken = m_output->write(pcm);
  QCOMPARE(taken, qsizetype(kBytesPerFrame * 10));
}

void AudioOutputTests::takesNoMoreThanItsCapacity()
{
  const QByteArray oneSecond(kSampleRate * kBytesPerFrame, '\0');
  const auto taken = m_output->write(oneSecond);
  QVERIFY2(taken > 0 && taken < oneSecond.size(), qPrintable(QString::number(taken)));
  const auto queued = m_output->queuedBytes();
  QVERIFY2(queued >= kCapacity / 2 && queued <= kCapacity * 2, qPrintable(QString::number(queued)));
}

void AudioOutputTests::playsQueuedAudio()
{
  m_output->write(QByteArray(kCapacity, '\0'));
  const auto queued = m_output->queuedBytes();
  QVERIFY2(queued > 0, qPrintable(QString::number(queued)));
  QTRY_VERIFY_WITH_TIMEOUT(m_output->queuedBytes() < queued, 500);
}

QTEST_MAIN(AudioOutputTests)
