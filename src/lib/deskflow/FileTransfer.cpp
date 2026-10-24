/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileTransfer.h"

#include "base/Log.h"
#include "deskflow/PacedStreamWriter.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

#include <QDateTime>
#include <QDir>
#include <QFile>

#include <algorithm>
#include <chrono>
#include <cstring>

using namespace deskflow::fileclipboard;

namespace {

namespace Mark {
constexpr uint8_t Entry = 1;
constexpr uint8_t Data = 2;
constexpr uint8_t Done = 3;
constexpr uint8_t Error = 4;
} // namespace Mark

constexpr qint64 kChunkBytes = 64 * 1024;
// the length prefix and header of a kMsgDFileData message
constexpr size_t kMessageOverhead = 16;

bool isMessage(const uint8_t *code, const char *message)
{
  return std::memcmp(code, message, 4) == 0;
}

// folders are named after the time they were made, keep the newest one in case it is still being pasted
void removeOldReceivedFolders(const QDir &root)
{
  auto folders = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  folders.removeIf([](const QString &name) {
    bool isNumber = false;
    name.toLongLong(&isNumber);
    return !isNumber;
  });
  std::sort(folders.begin(), folders.end(), [](const QString &a, const QString &b) {
    return a.toLongLong() < b.toLongLong();
  });
  for (qsizetype i = 0; i + 1 < folders.size(); ++i)
    QDir(root.filePath(folders[i])).removeRecursively();
}

} // namespace

struct FileTransfer::Sending
{
  uint32_t id = 0;
  QList<Entry> entries;
  qsizetype next = 0;
  QFile file;
  uint64_t remaining = 0;
};

struct FileTransfer::Receiving
{
  uint32_t id = 0;
  QString folder;
  //! the name of each copied item, learned from its first entry
  QStringList names;
  Fetched fetched;
  QFile file;
  uint64_t remaining = 0;
  int64_t modified = 0;
  uint64_t total = 0;
  int files = 0;
  std::chrono::steady_clock::time_point started;
};

FileTransfer::FileTransfer(deskflow::IStream *stream, PacedStreamWriter &writer) : m_stream(stream), m_writer(writer)
{
  m_writer.setRefill([this] { return sendNext(); });
}

FileTransfer::~FileTransfer()
{
  m_writer.setRefill({});
  // the connection may already be gone, so don't tell the other computer
  if (m_receiving) {
    m_receiving->file.close();
    QDir(m_receiving->folder).removeRecursively();
  }
}

void FileTransfer::offer(const QStringList &paths)
{
  m_offered = QSet<QString>(paths.begin(), paths.end());
}

void FileTransfer::fetch(const QStringList &remotePaths, Fetched fetched)
{
  cancelFetch();
  if (remotePaths.isEmpty()) {
    fetched({});
    return;
  }

  const QDir root(receiveFolder());
  if (root.exists())
    removeOldReceivedFolders(root);

  auto receiving = std::make_unique<Receiving>();
  receiving->id = ++m_lastFetchId;
  receiving->folder = root.filePath(QString::number(QDateTime::currentMSecsSinceEpoch()));
  receiving->names = QStringList(remotePaths.size(), QString());
  receiving->fetched = std::move(fetched);
  receiving->started = std::chrono::steady_clock::now();
  if (!QDir().mkpath(receiving->folder)) {
    LOG_WARN("can't receive copied files, unable to create %s", qPrintable(receiving->folder));
    receiving->fetched({});
    return;
  }

  const auto id = receiving->id;
  m_receiving = std::move(receiving);
  LOG_INFO("fetching %d copied item(s) from the other computer", static_cast<int>(remotePaths.size()));
  const std::string paths = encodePaths(remotePaths);
  ProtocolUtil::writef(m_stream, kMsgDFileRequest, id, &paths);
}

void FileTransfer::cancelFetch()
{
  if (!m_receiving)
    return;
  ProtocolUtil::writef(m_stream, kMsgDFileCancel, m_receiving->id);
  m_receiving->file.close();
  QDir(m_receiving->folder).removeRecursively();
  m_receiving.reset();
}

bool FileTransfer::handleMessage(const uint8_t *code)
{
  if (isMessage(code, kMsgDFileData))
    recvData();
  else if (isMessage(code, kMsgDFileRequest))
    recvRequest();
  else if (isMessage(code, kMsgDFileCancel))
    recvCancel();
  else
    return false;
  return true;
}

void FileTransfer::recvRequest()
{
  uint32_t id = 0;
  std::string paths;
  if (!ProtocolUtil::readf(m_stream, kMsgDFileRequest + 4, &id, &paths))
    return;

  stopSending();
  const QStringList requested = decodePaths(paths);
  for (const auto &path : requested) {
    if (!m_offered.contains(path)) {
      LOG_WARN("refused a request for files that weren't copied here");
      sendData(id, Mark::Error, "not copied");
      return;
    }
  }

  auto sending = std::make_unique<Sending>();
  sending->id = id;
  QString error;
  if (!listEntries(requested, sending->entries, error)) {
    LOG_WARN("can't send copied files, %s", qPrintable(error));
    sendData(id, Mark::Error, error.toStdString());
    return;
  }

  LOG_INFO("sending %d copied item(s) to the other computer", static_cast<int>(requested.size()));
  m_sending = std::move(sending);
  m_writer.pump();
}

void FileTransfer::recvCancel()
{
  uint32_t id = 0;
  if (!ProtocolUtil::readf(m_stream, kMsgDFileCancel + 4, &id))
    return;
  if (m_sending && m_sending->id == id) {
    LOG_DEBUG("the other computer cancelled copied files %u", id);
    stopSending();
  }
}

bool FileTransfer::sendNext()
{
  if (!m_sending)
    return false;
  auto &sending = *m_sending;

  if (sending.file.isOpen()) {
    const auto size = static_cast<qint64>(std::min<uint64_t>(kChunkBytes, sending.remaining));
    std::string chunk(static_cast<size_t>(size), '\0');
    if (sending.file.read(chunk.data(), size) != size) {
      // the file got shorter after it was listed
      LOG_WARN("can't send copied files, %s changed while sending", qPrintable(sending.file.fileName()));
      sendData(sending.id, Mark::Error, "a file changed while sending");
      stopSending();
      return true;
    }
    sending.remaining -= static_cast<uint64_t>(size);
    if (sending.remaining == 0)
      sending.file.close();
    sendData(sending.id, Mark::Data, chunk);
    return true;
  }

  if (sending.next >= sending.entries.size()) {
    sendData(sending.id, Mark::Done, {});
    LOG_INFO("sent copied files to the other computer");
    stopSending();
    return true;
  }

  const Entry &entry = sending.entries[sending.next++];
  if (!entry.isDir && entry.size > 0) {
    sending.file.setFileName(entry.sourcePath);
    if (!sending.file.open(QIODevice::ReadOnly)) {
      LOG_WARN("can't send copied files, unable to read %s", qPrintable(entry.sourcePath));
      sendData(sending.id, Mark::Error, "a file can't be read");
      stopSending();
      return true;
    }
    sending.remaining = entry.size;
  }
  sendData(sending.id, Mark::Entry, encodeEntry(entry));
  return true;
}

void FileTransfer::stopSending()
{
  m_sending.reset();
}

void FileTransfer::sendData(uint32_t id, uint8_t mark, const std::string &payload)
{
  m_writer.enqueue(payload.size() + kMessageOverhead, [stream = m_stream, id, mark, payload] {
    ProtocolUtil::writef(stream, kMsgDFileData, id, mark, &payload);
  });
}

void FileTransfer::recvData()
{
  uint32_t id = 0;
  uint8_t mark = 0;
  std::string payload;
  if (!ProtocolUtil::readf(m_stream, kMsgDFileData + 4, &id, &mark, &payload))
    return;

  // data still on its way for a fetch that was cancelled
  if (!m_receiving || m_receiving->id != id)
    return;

  switch (mark) {
  case Mark::Entry:
    if (!receiveEntry(payload)) {
      ProtocolUtil::writef(m_stream, kMsgDFileCancel, id);
      finishFetch(false, QStringLiteral("an unusable file entry, or more than 1 GiB"));
    }
    break;

  case Mark::Data:
    if (!receiveData(payload)) {
      ProtocolUtil::writef(m_stream, kMsgDFileCancel, id);
      finishFetch(false, QStringLiteral("unable to write a received file"));
    }
    break;

  case Mark::Done:
    finishFetch(!m_receiving->file.isOpen(), QStringLiteral("the last file was incomplete"));
    break;

  case Mark::Error:
    finishFetch(false, QString::fromStdString(payload));
    break;

  default:
    ProtocolUtil::writef(m_stream, kMsgDFileCancel, id);
    finishFetch(false, QStringLiteral("unknown file data"));
    break;
  }
}

bool FileTransfer::receiveEntry(const std::string &payload)
{
  auto &receiving = *m_receiving;
  Entry entry;
  if (!decodeEntry(payload, entry) || entry.top >= receiving.names.size() || receiving.file.isOpen())
    return false;

  const QString path = sanitizeRelativePath(entry.relativePath);
  if (path.isEmpty())
    return false;

  receiving.total += entry.size;
  if (receiving.total > kMaxBytes)
    return false;

  // every entry of a copied item starts with that item's name
  const QString name = path.section(QLatin1Char('/'), 0, 0);
  auto &knownName = receiving.names[entry.top];
  if (knownName.isEmpty())
    knownName = name;
  else if (knownName != name)
    return false;

  // each item gets its own folder so two copied items with the same name don't collide
  const QString target = QDir(receiving.folder).filePath(QString::number(entry.top) + QLatin1Char('/') + path);
  if (entry.isDir)
    return QDir().mkpath(target);

  if (!QDir().mkpath(QFileInfo(target).absolutePath()))
    return false;
  receiving.file.setFileName(target);
  if (!receiving.file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  receiving.remaining = entry.size;
  receiving.modified = entry.modified;
  ++receiving.files;
  if (receiving.remaining == 0)
    closeReceivedFile();
  return true;
}

bool FileTransfer::receiveData(const std::string &payload)
{
  auto &receiving = *m_receiving;
  const auto size = static_cast<qint64>(payload.size());
  if (!receiving.file.isOpen() || payload.size() > receiving.remaining ||
      receiving.file.write(payload.data(), size) != size)
    return false;

  receiving.remaining -= payload.size();
  if (receiving.remaining == 0)
    closeReceivedFile();
  return true;
}

void FileTransfer::closeReceivedFile()
{
  auto &receiving = *m_receiving;
  if (receiving.modified > 0)
    receiving.file.setFileTime(
        QDateTime::fromSecsSinceEpoch(receiving.modified), QFileDevice::FileTime::FileModificationTime
    );
  receiving.file.close();
}

void FileTransfer::finishFetch(bool ok, const QString &reason)
{
  auto receiving = std::move(m_receiving);
  receiving->file.close();

  QStringList localPaths;
  for (qsizetype top = 0; ok && top < receiving->names.size(); ++top) {
    const QString &name = receiving->names[top];
    ok = !name.isEmpty();
    localPaths.append(QDir(receiving->folder).filePath(QString::number(top) + QLatin1Char('/') + name));
  }

  if (!ok) {
    LOG_WARN("couldn't receive copied files: %s", qPrintable(reason));
    QDir(receiving->folder).removeRecursively();
    receiving->fetched({});
    return;
  }

  const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - receiving->started;
  LOG_INFO(
      "received %d file(s), %.1f MB in %.1f s, ready to paste", receiving->files,
      static_cast<double>(receiving->total) / (1024 * 1024), elapsed.count()
  );
  receiving->fetched(localPaths);
}
