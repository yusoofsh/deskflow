/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioProtocolTests.h"

#include "gui/audio/AudioProtocol.h"

#include <QtEndian>

using namespace deskflow::gui::audio;

void AudioProtocolTests::roundTripsFramesAcrossSplitReads()
{
  const Frame speaker{FrameKind::Speaker, 7, QByteArray(kChunkBytes, 'a')};
  const Frame hello{FrameKind::Hello, 0, encodeHello(Role::Headset)};
  const auto wire = encode(speaker) + encode(hello);

  FrameReader reader;
  QList<Frame> frames;
  for (qsizetype i = 0; i < wire.size(); i += 5) {
    reader.append(wire.mid(i, 5));
    while (auto frame = reader.next())
      frames.append(*frame);
  }

  QCOMPARE(frames.size(), 2);
  QCOMPARE(frames.at(0), speaker);
  QCOMPARE(frames.at(1), hello);
  QVERIFY(!reader.failed());
}

void AudioProtocolTests::rejectsOversizedFrames()
{
  QByteArray wire(7, '\0');
  qToBigEndian<quint32>(quint32(kMaxPayload + 4), wire.data());
  wire[4] = char(FrameKind::Speaker);

  FrameReader reader;
  reader.append(wire);
  QVERIFY(!reader.next());
  QVERIFY(reader.failed());
}

void AudioProtocolTests::rejectsUnknownKinds()
{
  auto wire = encode({FrameKind::Mic, 1, "xx"});
  wire[4] = char(42);

  FrameReader reader;
  reader.append(wire);
  QVERIFY(!reader.next());
  QVERIFY(reader.failed());
}

void AudioProtocolTests::routesSpeakerToHeadsetAndMicToRemotes()
{
  QVERIFY(shouldDeliver(FrameKind::Speaker, Role::Remote, Role::Headset));
  QVERIFY(!shouldDeliver(FrameKind::Speaker, Role::Remote, Role::Remote));
  QVERIFY(!shouldDeliver(FrameKind::Speaker, Role::Headset, Role::Headset));
  QVERIFY(!shouldDeliver(FrameKind::Speaker, Role::Remote, Role::Off));

  QVERIFY(shouldDeliver(FrameKind::Mic, Role::Headset, Role::Remote));
  QVERIFY(!shouldDeliver(FrameKind::Mic, Role::Headset, Role::Headset));
  QVERIFY(!shouldDeliver(FrameKind::Mic, Role::Remote, Role::Remote));
  QVERIFY(!shouldDeliver(FrameKind::Mic, Role::Headset, Role::Off));

  QVERIFY(!shouldDeliver(FrameKind::Hello, Role::Remote, Role::Headset));

  QCOMPARE(sentKind(Role::Headset), FrameKind::Mic);
  QCOMPARE(sentKind(Role::Remote), FrameKind::Speaker);
  QVERIFY(!sentKind(Role::Off));
}

void AudioProtocolTests::decodesOnlyKnownRoles()
{
  QCOMPARE(decodeHello(encodeHello(Role::Remote)), Role::Remote);
  QVERIFY(!decodeHello(QByteArray(1, char(9))));
  QVERIFY(!decodeHello(QByteArray()));
}

QTEST_MAIN(AudioProtocolTests)
