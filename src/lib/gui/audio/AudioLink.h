/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "AudioProtocol.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QSslServer;
class QSslSocket;
class QTcpServer;

namespace deskflow::gui::audio {

struct LinkTls
{
  bool enabled = false;
  QString certificatePath;
  QString trustedPeersDb;
  bool requirePeerTrust = false;
};

/**
 * @brief Carries audio frames between Deskflow computers.
 *
 * The Deskflow server listens and routes; every client connects to it. The server's own audio
 * is just another peer with source id 0, so routing is one rule applied to every pair.
 */
class AudioLink : public QObject
{
  Q_OBJECT

public:
  explicit AudioLink(QObject *parent = nullptr);
  ~AudioLink() override;

  void listen(quint16 port, Role localRole, const LinkTls &tls);
  /**
   * @param hosts Tried in turn, one per reconnect attempt, like the core's remote host list.
   */
  void connectTo(const QStringList &hosts, quint16 port, Role localRole, const LinkTls &tls);
  void stop();

  void sendLocal(FrameKind kind, const QByteArray &pcm);

  int peerCount() const;

Q_SIGNALS:
  void frameReceived(const deskflow::gui::audio::Frame &frame);
  void peersChanged(int count);
  void statusChanged(const QString &status);

private:
  struct Peer
  {
    SourceId id = kLocalSource;
    Role role = Role::Off;
    FrameReader reader;
  };

  void acceptPendingConnections();
  void addPeer(QSslSocket *socket);
  void readPeer(QSslSocket *socket);
  void route(const Frame &frame, Role fromRole, QSslSocket *from);
  void readUplink();
  void reconnect();
  void configureTls(QSslSocket *socket) const;
  bool peerTrusted(QSslSocket *socket) const;

  QTcpServer *m_server = nullptr;
  QHash<QSslSocket *, Peer> m_peers;
  SourceId m_nextId = 1;

  QPointer<QSslSocket> m_uplink;
  FrameReader m_uplinkReader;
  QTimer m_retry;
  QStringList m_hosts;
  qsizetype m_nextHost = 0;
  QString m_host;
  quint16 m_port = 0;

  Role m_localRole = Role::Off;
  LinkTls m_tls;
};

} // namespace deskflow::gui::audio
