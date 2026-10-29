/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioLink.h"

#include "net/FingerprintDatabase.h"

#include <QDebug>
#include <QFile>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QTcpServer>

namespace deskflow::gui::audio {

namespace {
constexpr int kRetryMs = 3000;
// Beyond this much unsent data the receiver is not keeping up; dropping audio beats growing latency.
constexpr qint64 kMaxQueuedBytes = kChunkBytes * 50;

// Hands out QSslSocket without encryption so plain and TLS peers share one socket type.
class PlainServer : public QTcpServer
{
public:
  using QTcpServer::QTcpServer;

protected:
  void incomingConnection(qintptr descriptor) override
  {
    auto socket = new QSslSocket(this);
    if (socket->setSocketDescriptor(descriptor))
      addPendingConnection(socket);
    else
      delete socket;
  }
};
QSslConfiguration tlsConfiguration(const LinkTls &tls)
{
  auto config = QSslConfiguration::defaultConfiguration();
  config.setLocalCertificateChain(QSslCertificate::fromPath(tls.certificatePath));
  if (QFile keyFile(tls.certificatePath); keyFile.open(QIODevice::ReadOnly))
    config.setPrivateKey(QSslKey(&keyFile, QSsl::Rsa));
  // Deskflow certificates are self-signed; trust comes from the fingerprint database instead of a CA.
  config.setPeerVerifyMode(QSslSocket::QueryPeer);
  return config;
}
} // namespace

AudioLink::AudioLink(QObject *parent) : QObject(parent)
{
  m_retry.setSingleShot(true);
  m_retry.setInterval(kRetryMs);
  connect(&m_retry, &QTimer::timeout, this, &AudioLink::reconnect);
}

AudioLink::~AudioLink()
{
  stop();
}

void AudioLink::configureTls(QSslSocket *socket) const
{
  socket->setSslConfiguration(tlsConfiguration(m_tls));
  connect(socket, &QSslSocket::sslErrors, socket, [socket](const QList<QSslError> &) { socket->ignoreSslErrors(); });
}

bool AudioLink::peerTrusted(QSslSocket *socket) const
{
  if (!m_tls.enabled || !m_tls.requirePeerTrust)
    return true;
  const auto cert = socket->peerCertificate();
  if (cert.isNull())
    return false;
  FingerprintDatabase db;
  db.read(m_tls.trustedPeersDb);
  return db.isTrusted({QCryptographicHash::Sha256, cert.digest(QCryptographicHash::Sha256)});
}

void AudioLink::listen(quint16 port, Role localRole, const LinkTls &tls)
{
  stop();
  m_localRole = localRole;
  m_tls = tls;

  if (tls.enabled) {
    auto server = new QSslServer(this);
    auto config = tlsConfiguration(tls);
    // Schannel (Windows) only asks clients for a certificate in VerifyPeer mode. The CA errors this raises are
    // ignored below, and peerTrusted() checks the certificate against the fingerprint database instead.
    config.setPeerVerifyMode(QSslSocket::VerifyPeer);
    server->setSslConfiguration(config);
    connect(server, &QSslServer::sslErrors, this, [](QSslSocket *socket, const QList<QSslError> &) {
      socket->ignoreSslErrors();
    });
    m_server = server;
  } else {
    m_server = new PlainServer(this);
  }

  connect(m_server, &QTcpServer::pendingConnectionAvailable, this, &AudioLink::acceptPendingConnections);
  if (!m_server->listen(QHostAddress::Any, port)) {
    Q_EMIT statusChanged(tr("Audio could not listen on port %1: %2").arg(port).arg(m_server->errorString()));
    return;
  }
  Q_EMIT statusChanged(tr("Waiting for computers on port %1").arg(port));
}

void AudioLink::acceptPendingConnections()
{
  while (m_server && m_server->hasPendingConnections()) {
    auto socket = qobject_cast<QSslSocket *>(m_server->nextPendingConnection());
    if (!socket)
      continue;
    if (m_tls.enabled) {
      if (socket->isEncrypted())
        addPeer(socket);
      else
        connect(socket, &QSslSocket::encrypted, this, [this, socket] { addPeer(socket); });
    } else {
      addPeer(socket);
    }
    connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
  }
}

void AudioLink::addPeer(QSslSocket *socket)
{
  if (!peerTrusted(socket)) {
    qWarning() << "audio: rejected untrusted peer" << socket->peerAddress().toString();
    socket->disconnectFromHost();
    return;
  }

  m_peers.insert(socket, Peer{m_nextId++});
  connect(socket, &QSslSocket::readyRead, this, [this, socket] { readPeer(socket); });
  connect(socket, &QSslSocket::disconnected, this, [this, socket] {
    m_peers.remove(socket);
    Q_EMIT peersChanged(peerCount());
    if (m_peers.isEmpty() && m_server)
      Q_EMIT statusChanged(tr("Waiting for computers on port %1").arg(m_server->serverPort()));
  });
  socket->write(encode({FrameKind::Hello, kLocalSource, encodeHello(m_localRole)}));
  Q_EMIT peersChanged(peerCount());
}

void AudioLink::readPeer(QSslSocket *socket)
{
  auto it = m_peers.find(socket);
  if (it == m_peers.end())
    return;

  it->reader.append(socket->readAll());
  while (auto frame = it->reader.next()) {
    if (frame->kind == FrameKind::Hello) {
      if (auto role = decodeHello(frame->payload)) {
        it->role = *role;
        Q_EMIT peersChanged(peerCount());
      }
      continue;
    }
    frame->source = it->id;
    route(*frame, it->role, socket);
  }
  if (it->reader.failed()) {
    qWarning() << "audio: corrupt stream from" << socket->peerAddress().toString();
    socket->disconnectFromHost();
  }
}

void AudioLink::route(const Frame &frame, Role fromRole, QSslSocket *from)
{
  if (from && shouldDeliver(frame.kind, fromRole, m_localRole))
    Q_EMIT frameReceived(frame);

  QByteArray wire;
  for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) {
    if (it.key() == from || !shouldDeliver(frame.kind, fromRole, it->role))
      continue;
    if (it.key()->bytesToWrite() > kMaxQueuedBytes)
      continue;
    if (wire.isEmpty())
      wire = encode(frame);
    it.key()->write(wire);
  }
}

void AudioLink::connectTo(const QStringList &hosts, quint16 port, Role localRole, const LinkTls &tls)
{
  stop();
  m_localRole = localRole;
  m_tls = tls;
  m_hosts = hosts;
  m_port = port;
  reconnect();
}

void AudioLink::reconnect()
{
  if (m_hosts.isEmpty())
    return;
  m_host = m_hosts.at(m_nextHost++ % m_hosts.size());
  if (m_uplink)
    m_uplink->deleteLater();

  m_uplinkReader = {};
  auto socket = new QSslSocket(this);
  m_uplink = socket;

  const auto onReady = [this, socket] {
    if (!peerTrusted(socket)) {
      Q_EMIT statusChanged(tr("Audio refused: server fingerprint is not trusted"));
      socket->disconnectFromHost();
      return;
    }
    socket->setProperty("trusted", true);
    socket->write(encode({FrameKind::Hello, kLocalSource, encodeHello(m_localRole)}));
    Q_EMIT statusChanged(tr("Connected to %1").arg(m_host));
    Q_EMIT peersChanged(1);
  };

  connect(socket, &QSslSocket::readyRead, this, &AudioLink::readUplink);
  connect(socket, &QSslSocket::disconnected, this, [this, socket] {
    if (socket != m_uplink)
      return;
    Q_EMIT peersChanged(0);
    if (socket->property("trusted").toBool())
      Q_EMIT statusChanged(tr("Reconnecting to %1").arg(m_host));
    m_retry.start();
  });
  connect(socket, &QSslSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
    if (socket != m_uplink || socket->state() == QAbstractSocket::ConnectedState)
      return;
    Q_EMIT statusChanged(tr("Audio cannot reach %1: %2").arg(m_host, socket->errorString()));
    m_retry.start();
  });

  if (m_tls.enabled) {
    configureTls(socket);
    connect(socket, &QSslSocket::encrypted, this, onReady);
    socket->connectToHostEncrypted(m_host, m_port);
  } else {
    connect(socket, &QSslSocket::connected, this, onReady);
    socket->connectToHost(m_host, m_port);
  }
}

void AudioLink::readUplink()
{
  if (!m_uplink)
    return;
  m_uplinkReader.append(m_uplink->readAll());
  while (auto frame = m_uplinkReader.next()) {
    if (frame->kind != FrameKind::Hello)
      Q_EMIT frameReceived(*frame);
  }
  if (m_uplinkReader.failed())
    m_uplink->disconnectFromHost();
}

void AudioLink::sendLocal(FrameKind kind, const QByteArray &pcm)
{
  const Frame frame{kind, kLocalSource, pcm};
  if (m_server) {
    route(frame, m_localRole, nullptr);
    return;
  }
  if (m_uplink && m_uplink->state() == QAbstractSocket::ConnectedState && (!m_tls.enabled || m_uplink->isEncrypted()) &&
      m_uplink->bytesToWrite() <= kMaxQueuedBytes)
    m_uplink->write(encode(frame));
}

int AudioLink::peerCount() const
{
  if (m_server)
    return m_peers.size();
  return m_uplink && m_uplink->state() == QAbstractSocket::ConnectedState ? 1 : 0;
}

void AudioLink::stop()
{
  m_retry.stop();
  m_hosts.clear();
  m_nextHost = 0;
  if (m_uplink) {
    auto socket = m_uplink.data();
    m_uplink = nullptr;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
  }
  for (auto it = m_peers.keyBegin(); it != m_peers.keyEnd(); ++it) {
    (*it)->disconnect(this);
    (*it)->abort();
    (*it)->deleteLater();
  }
  m_peers.clear();
  if (m_server) {
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
  }
}

} // namespace deskflow::gui::audio
