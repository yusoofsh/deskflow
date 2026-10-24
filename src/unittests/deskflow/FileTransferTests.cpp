/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferTests.h"

#include "MockEventQueue.h"
#include "base/Event.h"
#include "common/Settings.h"
#include "deskflow/Clipboard.h"
#include "deskflow/FileClipboard.h"
#include "deskflow/FileTransfer.h"
#include "deskflow/PacedStreamWriter.h"
#include "io/IStream.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cstring>
#include <map>

using namespace deskflow::fileclipboard;

namespace {

//! One direction of a connection, what one side writes the other side reads
class PipeStream : public deskflow::IStream
{
public:
  PipeStream(std::string &out, std::string &in) : m_out(out), m_in(in)
  {
  }

  void close() override
  {
  }
  uint32_t read(void *buffer, uint32_t n) override
  {
    n = std::min<uint32_t>(n, static_cast<uint32_t>(m_in.size()));
    std::memcpy(buffer, m_in.data(), n);
    m_in.erase(0, n);
    return n;
  }
  void write(const void *buffer, uint32_t n) override
  {
    m_out.append(static_cast<const char *>(buffer), n);
  }
  void flush() override
  {
  }
  void shutdownInput() override
  {
  }
  void shutdownOutput() override
  {
  }
  void *getEventTarget() const override
  {
    return const_cast<PipeStream *>(this);
  }
  bool isReady() const override
  {
    return !m_in.empty();
  }
  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(m_in.size());
  }

private:
  std::string &m_out;
  std::string &m_in;
};

//! Keeps handlers so the test can tell a writer its stream was flushed
class RecordingEventQueue : public MockEventQueue
{
public:
  void addHandler(EventTypes type, void *target, const EventHandler &handler) override
  {
    m_handlers[{type, target}] = handler;
  }

  void removeHandler(EventTypes type, void *target) override
  {
    m_handlers.erase({type, target});
  }

  void flushed(void *target)
  {
    if (auto it = m_handlers.find({EventTypes::StreamOutputFlushed, target}); it != m_handlers.end())
      it->second(Event(EventTypes::StreamOutputFlushed, target));
  }

private:
  std::map<std::pair<EventTypes, void *>, EventHandler> m_handlers;
};

//! Two computers joined by pipes
struct Connection
{
  std::string toServer;
  std::string toClient;
  RecordingEventQueue events;
  PipeStream serverStream{toClient, toServer};
  PipeStream clientStream{toServer, toClient};
  PacedStreamWriter serverWriter{&serverStream, &events};
  PacedStreamWriter clientWriter{&clientStream, &events};
  FileTransfer server{&serverStream, serverWriter};
  FileTransfer client{&clientStream, clientWriter};

  //! Delivers messages both ways and reports each flush, until nothing moves
  void run()
  {
    for (int round = 0; round < 100000; ++round) {
      const bool moved = !toServer.empty() || !toClient.empty();
      deliver(serverStream, server);
      deliver(clientStream, client);
      events.flushed(&serverStream);
      events.flushed(&clientStream);
      if (!moved && toServer.empty() && toClient.empty())
        return;
    }
    QFAIL("transfer did not finish");
  }

  static void deliver(PipeStream &stream, FileTransfer &transfer)
  {
    while (stream.getSize() >= 4) {
      uint8_t code[4];
      stream.read(code, 4);
      QVERIFY(transfer.handleMessage(code));
    }
  }
};

void writeFile(const QString &path, const QByteArray &content)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write(content), content.size());
}

QByteArray readFile(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return file.readAll();
}

} // namespace

void FileTransferTests::initTestCase()
{
  QVERIFY(m_settingsDir.isValid());
  QVERIFY(m_sourceDir.isValid());
  Settings::setSettingsFile(m_settingsDir.filePath(QStringLiteral("Deskflow.conf")));
  m_log.setFilter(LogLevel::Level::Debug);
}

void FileTransferTests::pathsRoundTrip()
{
  const QStringList paths = {QStringLiteral("/Users/me/a b.txt"), QStringLiteral("C:\\Users\\me\\ü.pdf")};
  QCOMPARE(decodePaths(encodePaths(paths)), paths);
  QCOMPARE(decodePaths(""), QStringList());
}

void FileTransferTests::entryRoundTrip()
{
  Entry entry;
  entry.top = 2;
  entry.isDir = false;
  entry.size = 5'000'000'000ULL;
  entry.modified = 1'792'000'000;
  entry.relativePath = QStringLiteral("folder/sub dir/naïve\tname.txt");

  // a tab in the name stays part of the path because the path is the last field
  Entry decoded;
  QVERIFY(decodeEntry(encodeEntry(entry), decoded));
  QCOMPARE(decoded.top, entry.top);
  QCOMPARE(decoded.isDir, entry.isDir);
  QCOMPARE(decoded.size, entry.size);
  QCOMPARE(decoded.modified, entry.modified);
  QCOMPARE(decoded.relativePath, entry.relativePath);
}

void FileTransferTests::entryRejectsMalformed()
{
  Entry entry;
  QVERIFY(!decodeEntry("", entry));
  QVERIFY(!decodeEntry("0\tx\t1\t0\tname", entry));
  QVERIFY(!decodeEntry("-1\tf\t1\t0\tname", entry));
  QVERIFY(!decodeEntry("0\tf\tbig\t0\tname", entry));
  QVERIFY(!decodeEntry("0\tf\t1\t0\t", entry));
}

void FileTransferTests::sanitizeRejectsEscapes()
{
  QCOMPARE(sanitizeRelativePath(QStringLiteral("../secret")), QString());
  QCOMPARE(sanitizeRelativePath(QStringLiteral("a/../../b")), QString());
  QCOMPARE(sanitizeRelativePath(QStringLiteral("/etc/passwd")), QString());
  QCOMPARE(sanitizeRelativePath(QStringLiteral("a//b")), QString());
  QCOMPARE(sanitizeRelativePath(QStringLiteral("./a")), QString());
  QCOMPARE(sanitizeRelativePath(QString()), QString());
#ifdef Q_OS_WIN
  // a trailing dot is dropped by Windows, which would make this ".."
  QCOMPARE(sanitizeRelativePath(QStringLiteral("a/...")), QString());
  QCOMPARE(sanitizeRelativePath(QStringLiteral("C:\\Windows")), QStringLiteral("C__Windows"));
#endif
}

void FileTransferTests::sanitizeKeepsNormalNames()
{
  QCOMPARE(sanitizeRelativePath(QStringLiteral("report.pdf")), QStringLiteral("report.pdf"));
  QCOMPARE(sanitizeRelativePath(QStringLiteral("Photos/2026/IMG 01.jpg")), QStringLiteral("Photos/2026/IMG 01.jpg"));
  QCOMPARE(sanitizeRelativePath(QStringLiteral(".gitignore")), QStringLiteral(".gitignore"));
#ifdef Q_OS_WIN
  QCOMPARE(sanitizeRelativePath(QStringLiteral("a:b?.txt")), QStringLiteral("a_b_.txt"));
  QCOMPARE(sanitizeRelativePath(QStringLiteral("con.txt")), QStringLiteral("_con.txt"));
#endif
}

void FileTransferTests::clipboardPaths()
{
  Clipboard clipboard;
  QCOMPARE(copiedPaths(clipboard), QStringList());

  const QStringList paths = {QStringLiteral("/tmp/a"), QStringLiteral("/tmp/b")};
  setCopiedPaths(clipboard, paths);
  QCOMPARE(copiedPaths(clipboard), paths);
}

void FileTransferTests::removeKeepsOtherFormats()
{
  Clipboard clipboard;
  QVERIFY(clipboard.open(0));
  clipboard.empty();
  clipboard.add(IClipboard::Format::Text, "a.txt");
  clipboard.add(IClipboard::Format::Files, "/tmp/a.txt");
  clipboard.close();

  removeCopiedPaths(clipboard);
  QCOMPARE(copiedPaths(clipboard), QStringList());
  QVERIFY(clipboard.open(0));
  QCOMPARE(clipboard.get(IClipboard::Format::Text), "a.txt");
  clipboard.close();
}

void FileTransferTests::transferFilesAndFolders()
{
  const QDir source(m_sourceDir.path());
  QByteArray big(300 * 1024, '\0');
  for (int i = 0; i < big.size(); ++i)
    big[i] = static_cast<char>(i * 7 % 251);
  writeFile(source.filePath(QStringLiteral("big.bin")), big);
  writeFile(source.filePath(QStringLiteral("empty.txt")), {});
  writeFile(source.filePath(QStringLiteral("Folder/inner/note.txt")), "hello");
  QVERIFY(QDir().mkpath(source.filePath(QStringLiteral("Folder/empty"))));
  // two copied items with the same name must not overwrite each other
  writeFile(source.filePath(QStringLiteral("other/big.bin")), "second");

  const QStringList copied = {
      source.filePath(QStringLiteral("big.bin")), source.filePath(QStringLiteral("empty.txt")),
      source.filePath(QStringLiteral("Folder")), source.filePath(QStringLiteral("other/big.bin"))
  };

  Connection connection;
  connection.server.offer(copied);
  QStringList received = {QStringLiteral("not called")};
  connection.client.fetch(copied, [&received](const QStringList &paths) { received = paths; });
  connection.run();

  QCOMPARE(received.size(), 4);
  QCOMPARE(QFileInfo(received[0]).fileName(), QStringLiteral("big.bin"));
  QCOMPARE(readFile(received[0]), big);
  QVERIFY(QFileInfo(received[1]).isFile());
  QCOMPARE(QFileInfo(received[1]).size(), 0);
  QCOMPARE(readFile(QDir(received[2]).filePath(QStringLiteral("inner/note.txt"))), QByteArray("hello"));
  QVERIFY(QFileInfo(QDir(received[2]).filePath(QStringLiteral("empty"))).isDir());
  QCOMPARE(readFile(received[3]), QByteArray("second"));
  QVERIFY(received[0].startsWith(receiveFolder()));
}

void FileTransferTests::refusesPathsNotOffered()
{
  const QString secret = m_sourceDir.filePath(QStringLiteral("secret.txt"));
  writeFile(secret, "private");

  Connection connection;
  connection.server.offer({m_sourceDir.filePath(QStringLiteral("big.bin"))});
  QStringList received = {QStringLiteral("not called")};
  connection.client.fetch({secret}, [&received](const QStringList &paths) { received = paths; });
  connection.run();

  QVERIFY(received.isEmpty());
}

void FileTransferTests::newFetchCancelsOld()
{
  const QString path = m_sourceDir.filePath(QStringLiteral("big.bin"));
  Connection connection;
  connection.server.offer({path});

  bool firstCalled = false;
  QStringList second;
  connection.client.fetch({path}, [&firstCalled](const QStringList &) { firstCalled = true; });
  connection.client.fetch({path}, [&second](const QStringList &paths) { second = paths; });
  connection.run();

  QVERIFY(!firstCalled);
  QCOMPARE(second.size(), 1);
  QCOMPARE(QFileInfo(second[0]).size(), QFileInfo(path).size());
}

QTEST_MAIN(FileTransferTests)
