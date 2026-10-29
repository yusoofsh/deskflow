/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "StreamConverterTests.h"

#include "gui/audio/StreamConverter.h"

#include <algorithm>
#include <cstring>

using namespace deskflow::gui::audio;

namespace {

QAudioFormat monoFloat16k()
{
  QAudioFormat format;
  format.setSampleRate(16000);
  format.setChannelCount(1);
  format.setSampleFormat(QAudioFormat::Float);
  return format;
}

QByteArray floats(const QList<float> &samples)
{
  QByteArray bytes(samples.size() * qsizetype(sizeof(float)), '\0');
  std::memcpy(bytes.data(), samples.constData(), bytes.size());
  return bytes;
}

QList<qint16> int16s(const QByteArray &pcm)
{
  QList<qint16> samples(pcm.size() / qsizetype(sizeof(qint16)));
  std::memcpy(samples.data(), pcm.constData(), samples.size() * sizeof(qint16));
  return samples;
}

} // namespace

void StreamConverterTests::passesStreamFormatThrough()
{
  QByteArray pcm(kChunkBytes, '\0');
  for (qsizetype i = 0; i < pcm.size(); ++i)
    pcm[i] = char(i * 7);

  StreamConverter converter(streamFormat());
  QCOMPARE(converter.convert(pcm), pcm);
}

void StreamConverterTests::convertsMonoFloatMicToStreamFormat()
{
  const qsizetype tenthOfASecond = 1600;
  StreamConverter converter(monoFloat16k());
  const auto out = int16s(converter.convert(floats(QList<float>(tenthOfASecond, 0.5f))));

  const auto outFrames = out.size() / kChannels;
  QVERIFY2(qAbs(outFrames - kSampleRate / 10) <= 3, qPrintable(QString::number(outFrames)));
  QVERIFY(std::all_of(out.cbegin(), out.cend(), [](qint16 s) { return qAbs(s - 16384) <= 1; }));
}

void StreamConverterTests::splitReadsGiveTheSameOutput()
{
  QList<float> ramp;
  for (int i = 0; i < 1000; ++i)
    ramp.append(float(i % 200) / 200 - 0.5f);
  const auto input = floats(ramp);

  StreamConverter whole(monoFloat16k());
  const auto expected = whole.convert(input);

  StreamConverter split(monoFloat16k());
  QByteArray actual;
  for (qsizetype i = 0, n = 1; i < input.size(); i += n, n = n % 13 + 1)
    actual += split.convert(input.mid(i, n));

  QVERIFY(!expected.isEmpty());
  QCOMPARE(actual, expected);
}

QTEST_MAIN(StreamConverterTests)
