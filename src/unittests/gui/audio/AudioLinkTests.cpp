/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioLinkTests.h"

#include "gui/audio/AudioLink.h"
#include "net/FingerprintDatabase.h"
#include "net/SecureUtils.h"

#include <QSignalSpy>
#include <QSslCertificate>
#include <QTcpServer>
#include <QTemporaryDir>

using namespace deskflow::gui::audio;

namespace {

quint16 freePort()
{
  QTcpServer probe;
  probe.listen(QHostAddress::LocalHost, 0);
  return probe.serverPort();
}

Frame frameAt(const QSignalSpy &spy, int i)
{
  return spy.at(i).at(0).value<Frame>();
}

// Hellos are exchanged right after connecting; give them a moment to land before sending audio.
void settle()
{
  QTest::qWait(150);
}

} // namespace

void AudioLinkTests::headsetServerHearsRemotesAndFeedsThemMic()
{
  const auto port = freePort();
  AudioLink server, remoteA, remoteB;
  QSignalSpy serverHeard(&server, &AudioLink::frameReceived);
  QSignalSpy aHeard(&remoteA, &AudioLink::frameReceived);
  QSignalSpy bHeard(&remoteB, &AudioLink::frameReceived);

  server.listen(port, Role::Headset, {});
  remoteA.connectTo({"127.0.0.1"}, port, Role::Remote, {});
  remoteB.connectTo({"127.0.0.1"}, port, Role::Remote, {});
  QTRY_COMPARE(server.peerCount(), 2);
  settle();

  remoteA.sendLocal(FrameKind::Speaker, "music-a");
  QTRY_COMPARE(serverHeard.count(), 1);
  QCOMPARE(frameAt(serverHeard, 0).kind, FrameKind::Speaker);
  QCOMPARE(frameAt(serverHeard, 0).payload, QByteArray("music-a"));
  QVERIFY(frameAt(serverHeard, 0).source != kLocalSource);

  server.sendLocal(FrameKind::Mic, "voice");
  QTRY_COMPARE(aHeard.count(), 1);
  QTRY_COMPARE(bHeard.count(), 1);
  QCOMPARE(frameAt(aHeard, 0).kind, FrameKind::Mic);
  QCOMPARE(frameAt(bHeard, 0).payload, QByteArray("voice"));

  settle();
  QCOMPARE(bHeard.count(), 1);
}

void AudioLinkTests::remoteServerRelaysToHeadsetClient()
{
  const auto port = freePort();
  AudioLink server, headset, remote;
  QSignalSpy serverHeard(&server, &AudioLink::frameReceived);
  QSignalSpy headsetHeard(&headset, &AudioLink::frameReceived);
  QSignalSpy remoteHeard(&remote, &AudioLink::frameReceived);

  server.listen(port, Role::Remote, {});
  headset.connectTo({"127.0.0.1"}, port, Role::Headset, {});
  remote.connectTo({"127.0.0.1"}, port, Role::Remote, {});
  QTRY_COMPARE(server.peerCount(), 2);
  settle();

  remote.sendLocal(FrameKind::Speaker, "from-client");
  server.sendLocal(FrameKind::Speaker, "from-server");
  QTRY_COMPARE(headsetHeard.count(), 2);
  QSet<QByteArray> payloads{frameAt(headsetHeard, 0).payload, frameAt(headsetHeard, 1).payload};
  QCOMPARE(payloads, (QSet<QByteArray>{"from-client", "from-server"}));
  QVERIFY(frameAt(headsetHeard, 0).source != frameAt(headsetHeard, 1).source);

  headset.sendLocal(FrameKind::Mic, "voice");
  QTRY_COMPARE(serverHeard.count(), 1);
  QTRY_COMPARE(remoteHeard.count(), 1);
  QCOMPARE(frameAt(serverHeard, 0).kind, FrameKind::Mic);
  QCOMPARE(frameAt(remoteHeard, 0).payload, QByteArray("voice"));
}

void AudioLinkTests::tlsLinkCarriesAudioBetweenTrustedPeers()
{
  QTemporaryDir dir;
  const auto cert = dir.filePath("peer.pem");
  deskflow::generatePemSelfSignedCert(cert, 2048);
  const auto trusted = dir.filePath("trusted.txt");
  FingerprintDatabase db;
  db.addTrusted({QCryptographicHash::Sha256, QSslCertificate::fromPath(cert).first().digest(QCryptographicHash::Sha256)}
  );
  QVERIFY(db.write(trusted));

  const LinkTls tls{true, cert, trusted, true};
  const auto port = freePort();
  AudioLink server, remote;
  QSignalSpy serverHeard(&server, &AudioLink::frameReceived);
  server.listen(port, Role::Headset, tls);
  remote.connectTo({"127.0.0.1"}, port, Role::Remote, tls);
  QTRY_COMPARE_WITH_TIMEOUT(server.peerCount(), 1, 10000);
  settle();

  remote.sendLocal(FrameKind::Speaker, "encrypted");
  QTRY_COMPARE(serverHeard.count(), 1);
  QCOMPARE(frameAt(serverHeard, 0).payload, QByteArray("encrypted"));
}

void AudioLinkTests::tlsLinkRefusesUntrustedServer()
{
  QTemporaryDir dir;
  const auto cert = dir.filePath("peer.pem");
  deskflow::generatePemSelfSignedCert(cert, 2048);
  const auto empty = dir.filePath("empty.txt");
  QVERIFY(FingerprintDatabase().write(empty));

  const auto port = freePort();
  AudioLink server, remote;
  QSignalSpy serverHeard(&server, &AudioLink::frameReceived);
  QSignalSpy remoteStatus(&remote, &AudioLink::statusChanged);
  server.listen(port, Role::Headset, {true, cert, empty, false});
  remote.connectTo({"127.0.0.1"}, port, Role::Remote, {true, cert, empty, true});

  QTRY_VERIFY_WITH_TIMEOUT(
      std::any_of(
          remoteStatus.cbegin(), remoteStatus.cend(),
          [](const QList<QVariant> &args) { return args.at(0).toString().contains("not trusted"); }
      ),
      10000
  );
  remote.sendLocal(FrameKind::Speaker, "leak");
  settle();
  QCOMPARE(serverHeard.count(), 0);
}

void AudioLinkTests::clientWalksTheHostListUntilOneAnswers()
{
  const auto port = freePort();
  AudioLink server, remote;
  QSignalSpy serverHeard(&server, &AudioLink::frameReceived);
  server.listen(port, Role::Headset, {});
  remote.connectTo({"deskflow-audio-test.invalid", "127.0.0.1"}, port, Role::Remote, {});
  QTRY_COMPARE_WITH_TIMEOUT(server.peerCount(), 1, 10000);
  settle();

  remote.sendLocal(FrameKind::Speaker, "second-host");
  QTRY_COMPARE(serverHeard.count(), 1);
}

QTEST_MAIN(AudioLinkTests)
