#include <QtTest>
#include <QSignalSpy>
#include <QMediaPlayer>
#include <QFileInfo>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QAudioBufferOutput>
#include <QAudioOutput>
#include <QAudioBuffer>
#endif
#include <QTcpServer>
#include "streamcache.h"

class StreamCacheTest : public QObject {
    Q_OBJECT
private slots:
    void proxyRespondsWhileGuiThreadWaits() {
        QTcpServer upstream; QVERIFY(upstream.listen(QHostAddress::LocalHost));
        QPointer<QTcpSocket> pending;
        connect(&upstream, &QTcpServer::newConnection, this, [&] {
            pending = upstream.nextPendingConnection();
            connect(pending, &QTcpSocket::disconnected, pending, &QObject::deleteLater);
        });
        StreamCache cache;
        const auto local = cache.source(QUrl(QString("http://127.0.0.1:%1/audio.bin").arg(upstream.serverPort())));
        QTcpSocket decoder;
        decoder.connectToHost(local.host(), local.port());
        QVERIFY(decoder.waitForConnected(1000));
        decoder.write("GET " + local.path().toUtf8() + " HTTP/1.1\r\nHost: localhost\r\n\r\n");
        decoder.flush();
        QTRY_VERIFY(pending && pending->bytesAvailable() > 0);
        pending->readAll();
        pending->write("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\naudio");
        pending->flush();
        // No processEvents: model the GUI joining a decoder during deferred
        // destruction. The proxy must deliver the response independently.
        QByteArray response;
        QElapsedTimer timer; timer.start();
        while (!response.endsWith("audio") && timer.elapsed() < 3000) {
            if (decoder.bytesAvailable() || decoder.waitForReadyRead(3000 - timer.elapsed()))
                response += decoder.readAll();
            else break;
        }
        QVERIFY2(response.endsWith("audio"), response.constData());
        // Completed objects also remain readable with the GUI event loop blocked.
        decoder.abort();
        decoder.connectToHost(local.host(), local.port());
        QVERIFY(decoder.waitForConnected(1000));
        decoder.write("GET " + local.path().toUtf8() + " HTTP/1.1\r\nHost: localhost\r\n\r\n");
        decoder.flush(); response.clear(); timer.restart();
        while (!response.endsWith("audio") && timer.elapsed() < 3000) {
            if (decoder.bytesAvailable() || decoder.waitForReadyRead(3000 - timer.elapsed()))
                response += decoder.readAll();
            else break;
        }
        QVERIFY2(response.endsWith("audio"), response.constData());
    }
    void interruptReleasesStalledDecoderWithoutEventLoop() {
        QTcpServer upstream; QVERIFY(upstream.listen(QHostAddress::LocalHost));
        QPointer<QTcpSocket> pending;
        connect(&upstream, &QTcpServer::newConnection, this, [&] {
            pending = upstream.nextPendingConnection();
            connect(pending, &QTcpSocket::disconnected, pending, &QObject::deleteLater);
        });
        StreamCacheWorker cache;
        const QUrl remote(QString("http://127.0.0.1:%1/stalled.bin").arg(upstream.serverPort()));
        const auto local = cache.source(remote);
        QTcpSocket decoder;
        decoder.connectToHost(local.host(), local.port());
        QVERIFY(decoder.waitForConnected(1000));
        decoder.write("GET " + local.path().toUtf8() + " HTTP/1.1\r\nHost: localhost\r\n\r\n");
        decoder.flush();
        QTRY_VERIFY(pending && pending->bytesAvailable() > 0);
        QCOMPARE(cache.m_foreground, 1);
        cache.interrupt(local);
        // Simulate a decoder join: the GUI event loop cannot run here.
        if (decoder.state() != QAbstractSocket::UnconnectedState)
            QVERIFY(decoder.waitForDisconnected(1000));
        QCOMPARE(cache.m_foreground, 0);
        QCOMPARE(cache.source(remote), local);
        QVERIFY(cache.m_resources.contains(local.path().mid(1)));
        // The same mapping must work once connectivity returns.
        // Disconnect the stalled upstream handler before accepting the retry.
        disconnect(&upstream, &QTcpServer::newConnection, this, nullptr);
        connect(&upstream, &QTcpServer::newConnection, this, [&] {
            auto *socket = upstream.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
                socket->disconnectFromHost();
            });
        });
        QNetworkAccessManager client;
        auto *reply = client.get(QNetworkRequest(local));
        QSignalSpy done(reply, &QNetworkReply::finished);
        QVERIFY(done.wait());
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->readAll(), QByteArray("ok"));
        reply->deleteLater();
    }
    void evictedPlaylistCannotRecreateResources() {
        QTcpServer upstream; QVERIFY(upstream.listen(QHostAddress::LocalHost));
        QPointer<QTcpSocket> pending;
        connect(&upstream, &QTcpServer::newConnection, this, [&] {
            pending = upstream.nextPendingConnection();
            connect(pending, &QTcpSocket::disconnected, pending, &QObject::deleteLater);
        });
        StreamCacheWorker cache;
        const QUrl remote(QString("http://127.0.0.1:%1/list.m3u8").arg(upstream.serverPort()));
        const auto local = cache.source(remote);
        QNetworkAccessManager client; auto *reply = client.get(QNetworkRequest(local));
        QSignalSpy done(reply, &QNetworkReply::finished);
        QTRY_VERIFY(pending && pending->bytesAvailable() > 0);
        pending->readAll();
        for (int i = 0; i < 4; ++i) cache.source(QUrl(QString("https://cdn.example/%1.m3u8").arg(i)));
        QVERIFY(!cache.m_resources.contains(local.path().mid(1)));
        const auto resources = cache.m_resources.size();
        const QByteArray body = "#EXTM3U\n#EXTINF:5,\npart.ts\n#EXT-X-ENDLIST\n";
        pending->write("HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        pending->disconnectFromHost();
        QTRY_VERIFY(reply->isFinished());
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 404);
        QCOMPARE(cache.m_resources.size(), resources);
        QVERIFY(!cache.m_cache.contains(local.path().mid(1)));
        reply->deleteLater();
    }
    void progressiveAudioExceedingCacheLimitStillStreams() {
        QTcpServer upstream; QVERIFY(upstream.listen(QHostAddress::LocalHost));
        const QByteArray body(9 * 1024 * 1024, 'a');
        connect(&upstream, &QTcpServer::newConnection, this, [&] {
            auto *socket = upstream.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n");
                socket->write(body.left(1024)); socket->flush();
                QTimer::singleShot(200, socket, [&, socket] {
                    socket->write(body.mid(1024)); socket->disconnectFromHost();
                });
            });
        });
        StreamCacheWorker cache;
        const auto local = cache.source(QUrl(QString("http://127.0.0.1:%1/full.mp3").arg(upstream.serverPort())));
        QNetworkAccessManager client; auto *reply = client.get(QNetworkRequest(local));
        QSignalSpy chunks(reply, &QNetworkReply::readyRead), done(reply, &QNetworkReply::finished);
        QVERIFY(chunks.wait()); QVERIFY(!reply->isFinished());
        QVERIFY(done.wait()); QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->readAll(), body); QVERIFY(!cache.m_cache.contains(local.path().mid(1)));
        reply->deleteLater();
    }
    void livePlaylistRefreshesAndHeadPreservesLength() {
        QTcpServer upstream; QVERIFY(upstream.listen(QHostAddress::LocalHost));
        int gets = 0;
        connect(&upstream, &QTcpServer::newConnection, this, [&] {
            auto *socket = upstream.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const bool head = socket->readAll().startsWith("HEAD ");
                if (!head) ++gets;
                const QByteArray body = "#EXTM3U\n#EXTINF:5,\npart" + QByteArray::number(gets) + ".ts\n";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n");
                if (!head) socket->write(body);
                socket->disconnectFromHost();
            });
        });
        StreamCacheWorker cache;
        const auto local = cache.source(QUrl(QString("http://127.0.0.1:%1/live.m3u8").arg(upstream.serverPort())));
        QNetworkAccessManager client;
        for (int i = 1; i <= 2; ++i) {
            auto *reply = client.get(QNetworkRequest(local));
            QSignalSpy done(reply, &QNetworkReply::finished); QVERIFY(done.wait());
            QCOMPARE(reply->error(), QNetworkReply::NoError);
            QVERIFY(!reply->readAll().isEmpty()); QCOMPARE(gets, i);
            QVERIFY(!cache.m_cache.contains(local.path().mid(1)));
            reply->deleteLater();
        }
        auto *reply = client.head(QNetworkRequest(local));
        QSignalSpy done(reply, &QNetworkReply::finished); QVERIFY(done.wait());
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->rawHeader("Content-Length"), QByteArray::number(QByteArray("#EXTM3U\n#EXTINF:5,\npart2.ts\n").size()));
        QVERIFY(reply->readAll().isEmpty()); reply->deleteLater();
    }
    void cachedRangeRejectsOverflow() {
        StreamCacheWorker cache;
        const auto local = cache.source(QUrl("https://cdn.example/part.bin"));
        const auto path = cache.m_directory.filePath("range");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("0123456789"); file.close();
        cache.m_cache.insert(local.path().mid(1), new StreamCacheWorker::Entry{path, "application/octet-stream"}, 10);
        QNetworkAccessManager client;
        for (const QByteArray &range : {QByteArray("bytes=999999999999999999999999-"), QByteArray("bytes=-999999999999999999999999")}) {
            QNetworkRequest request(local); request.setRawHeader("Range", range);
            auto *reply = client.get(request); QSignalSpy done(reply, &QNetworkReply::finished); QVERIFY(done.wait());
            QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 416);
            QCOMPARE(reply->rawHeader("Content-Range"), QByteArray("bytes */10")); reply->deleteLater();
        }
    }
    void disabledBackgroundStillServesDemand() {
        QTcpServer upstream; QVERIFY(upstream.listen(QHostAddress::LocalHost));
        int requests = 0;
        connect(&upstream, &QTcpServer::newConnection, this, [&] {
            auto *socket = upstream.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                socket->readAll(); ++requests;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 4\r\nConnection: close\r\n\r\ndata"); socket->disconnectFromHost();
            });
        });
        StreamCacheWorker cache;
        QUrl remote(QString("http://127.0.0.1:%1/part").arg(upstream.serverPort()));
        const auto local = cache.source(remote);
        cache.playlist("#EXTM3U\n#EXTINF:5,\npart\n#EXT-X-ENDLIST\n", {remote, remote.toString(QUrl::FullyEncoded)});
        cache.setBackgroundEnabled(false); cache.warm(local); cache.next();
        QVERIFY(!cache.m_timer.isActive()); QVERIFY(!cache.m_background); QCOMPARE(requests, 0);
        QNetworkAccessManager client; auto *reply = client.get(QNetworkRequest(local));
        QSignalSpy done(reply, &QNetworkReply::finished); QVERIFY(done.wait());
        QCOMPARE(reply->readAll(), QByteArray("data")); QCOMPARE(requests, 1);
        cache.warm(local); cache.next(); QTest::qWait(50); QCOMPARE(requests, 1);
        cache.discard(local); QVERIFY(cache.m_resources.isEmpty());
        reply->deleteLater();
    }
    void disablingAbortsActiveBackground() {
        QTcpServer upstream; QVERIFY(upstream.listen(QHostAddress::LocalHost));
        StreamCacheWorker cache;
        const QUrl remote(QString("http://127.0.0.1:%1/list").arg(upstream.serverPort()));
        const auto local = cache.source(remote);
        cache.playlist("#EXTM3U\n#EXTINF:5,\npart\n#EXT-X-ENDLIST\n", {remote, remote.toString(QUrl::FullyEncoded)});
        cache.warm(local); cache.next(); QVERIFY(cache.m_background);
        QPointer<QNetworkReply> reply = cache.m_background;
        cache.setBackgroundEnabled(false);
        QVERIFY(!cache.m_background); QVERIFY(!cache.m_timer.isActive());
        if (reply) QCOMPARE(reply->error(), QNetworkReply::OperationCanceledError);
        cache.next(); QVERIFY(!cache.m_background);
    }

    void playlistAndRanges() {
        StreamCacheWorker cache;
        const QUrl remote("https://cdn.example/track/list.m3u8?signature=secret");
        const QUrl local = cache.source(remote);
        QVERIFY(local.host() == "127.0.0.1");
        QVERIFY(!local.toString().contains("secret"));
        const QByteArray rewritten = cache.playlist("#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:5,\npart.m4s\n#EXT-X-ENDLIST\n", {remote, remote.toString(QUrl::FullyEncoded)});
        QVERIFY(!rewritten.contains("cdn.example"));
        QVERIFY(rewritten.contains("URI=\"http://127.0.0.1:"));
        QCOMPARE(cache.m_children.value(remote.toString(QUrl::FullyEncoded)).size(), 2);
        const auto key = cache.m_children.value(remote.toString(QUrl::FullyEncoded)).last();
        const auto path = cache.m_directory.filePath("segment");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("0123456789"); file.close();
        cache.m_cache.insert(key, new StreamCacheWorker::Entry{path, "audio/mp4"}, 10);
        QNetworkAccessManager client;
        QUrl segment = local; segment.setPath('/' + key);
        QNetworkRequest request(segment); request.setRawHeader("Range", "bytes=3-6");
        auto *reply = client.get(request); QSignalSpy done(reply, &QNetworkReply::finished);
        QVERIFY(done.wait()); QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 206);
        QCOMPARE(reply->readAll(), QByteArray("3456"));
        QCOMPARE(reply->rawHeader("Content-Range"), QByteArray("bytes 3-6/10")); reply->deleteLater();
        cache.warm(local); QVERIFY(cache.m_timer.isActive()); cache.suspend();
        QVERIFY(!cache.m_timer.isActive()); QVERIFY(cache.m_root.isEmpty());
    }
    void demandStreamsAndCaches() {
        QTcpServer upstream; QVERIFY(upstream.listen(QHostAddress::LocalHost));
        int requests = 0;
        connect(&upstream, &QTcpServer::newConnection, this, [&] {
            auto *socket = upstream.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                socket->readAll(); ++requests;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 10\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n01234"); socket->flush();
                QTimer::singleShot(200, socket, [socket] { socket->write("56789"); socket->disconnectFromHost(); });
            });
        });
        StreamCacheWorker cache;
        const auto local = cache.source(QUrl(QString("http://127.0.0.1:%1/part.bin").arg(upstream.serverPort())));
        QNetworkAccessManager client;
        auto *reply = client.get(QNetworkRequest(local));
        QSignalSpy chunks(reply, &QNetworkReply::readyRead), done(reply, &QNetworkReply::finished);
        QVERIFY(chunks.wait()); QVERIFY(!reply->isFinished()); // Первые байты идут до завершения загрузки.
        QVERIFY(done.wait()); QCOMPARE(reply->readAll(), QByteArray("0123456789")); reply->deleteLater();
        reply = client.get(QNetworkRequest(local)); QSignalSpy cachedDone(reply, &QNetworkReply::finished);
        QVERIFY(cachedDone.wait()); QCOMPARE(reply->readAll(), QByteArray("0123456789")); QCOMPARE(requests, 1); reply->deleteLater();
    }
    void truncatedSegmentIsRetried() {
        QTcpServer upstream; QVERIFY(upstream.listen(QHostAddress::LocalHost));
        int requests = 0;
        connect(&upstream, &QTcpServer::newConnection, this, [&] {
            auto *socket = upstream.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                socket->readAll(); ++requests;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 10\r\nContent-Type: audio/mp4\r\nConnection: close\r\n\r\n01234"); socket->flush();
                QTimer::singleShot(200, socket, [socket, &requests] { if (requests > 1) socket->write("56789"); socket->disconnectFromHost(); });
            });
        });
        StreamCacheWorker cache;
        const auto local = cache.source(QUrl("https://cdn.example/part.m4s"));
        cache.m_resources[local.path().mid(1)].remote = QUrl(QString("http://127.0.0.1:%1/part").arg(upstream.serverPort()));
        QNetworkAccessManager client;
        auto *reply = client.get(QNetworkRequest(local));
        QSignalSpy done(reply, &QNetworkReply::finished);
        // A truncated response must not reach the HLS demuxer.
        QVERIFY(done.wait()); QCOMPARE(reply->error(), QNetworkReply::NoError); QCOMPARE(reply->readAll(), QByteArray("0123456789")); reply->deleteLater();
        reply = client.get(QNetworkRequest(local)); QSignalSpy cachedDone(reply, &QNetworkReply::finished);
        QVERIFY(cachedDone.wait()); QCOMPARE(reply->readAll(), QByteArray("0123456789")); QCOMPARE(requests, 2); reply->deleteLater();
    }
    void opaqueFmp4UrlsKeepDecoderExtensions() {
        StreamCacheWorker cache;
        const QUrl remote("https://cdn.example/list.m3u8");
        cache.source(remote);
        const auto body = cache.playlist("#EXTM3U\n#EXT-X-MAP:URI=\"init\"\n#EXTINF:5,\npart\n#EXT-X-ENDLIST\n", {remote, remote.toString(QUrl::FullyEncoded)});
        const auto lines = body.split('\n');
        QVERIFY(lines[1].contains(".mp4\""));
        QVERIFY(lines[3].endsWith(".m4s"));
        QVERIFY(!body.contains("cdn.example"));
        const auto key = QUrl(QString::fromUtf8(lines[3])).path().mid(1);
        QCOMPARE(cache.m_resources.value(key).remote, QUrl("https://cdn.example/part"));
    }
    void extensionlessHlsSegmentIsRetriedBeforeDelivery() {
        QTcpServer upstream; QVERIFY(upstream.listen(QHostAddress::LocalHost));
        int requests = 0;
        connect(&upstream, &QTcpServer::newConnection, this, [&] {
            auto *socket = upstream.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                socket->readAll();
                const bool truncate = ++requests == 1;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 10\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n01234");
                QTimer::singleShot(30, socket, [socket, truncate] {
                    if (!truncate) socket->write("56789");
                    socket->disconnectFromHost();
                });
            });
        });
        StreamCacheWorker cache;
        const QUrl remote(QString("http://127.0.0.1:%1/list.m3u8").arg(upstream.serverPort()));
        cache.source(remote);
        const auto body = cache.playlist("#EXTM3U\n#EXTINF:5,\nopaque-segment\n#EXT-X-ENDLIST\n", {remote, remote.toString(QUrl::FullyEncoded)});
        const QUrl segment(QString::fromUtf8(body.split('\n').value(2)));
        QNetworkAccessManager client;
        auto *reply = client.get(QNetworkRequest(segment));
        QSignalSpy done(reply, &QNetworkReply::finished);
        QVERIFY(done.wait());
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->readAll(), QByteArray("0123456789"));
        QCOMPARE(requests, 2);
        reply->deleteLater();
    }
    void hlsSeekThroughCache_data() {
        QTest::addColumn<bool>("opaqueInterrupted");
        QTest::newRow("normal") << false;
        QTest::newRow("opaque-interrupted-segment") << true;
    }
    void hlsSeekThroughCache() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        const auto fixture = qEnvironmentVariable("SCRIP_HLS_TEST_PLAYLIST");
        if (fixture.isEmpty()) QSKIP("Запустите с SCRIP_HLS_TEST_PLAYLIST из tests/run-hls-seek.sh");
        QFETCH(bool, opaqueInterrupted);
        int interrupted = 0;
        QTcpServer upstream; QVERIFY(upstream.listen(QHostAddress::LocalHost));
        connect(&upstream, &QTcpServer::newConnection, this, [&] {
            auto *socket = upstream.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            auto input = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, input] {
                *input += socket->readAll();
                if (!input->contains("\r\n\r\n")) return;
                auto name = QFileInfo(QString::fromLatin1(input->split(' ').value(1))).fileName();
                if (opaqueInterrupted && !name.contains('.')) name += ".m4s";
                QFile file(QFileInfo(fixture).dir().filePath(name));
                if (!file.open(QIODevice::ReadOnly)) { socket->disconnectFromHost(); return; }
                QByteArray body = file.readAll();
                if (opaqueInterrupted && name.endsWith("m3u8")) body.replace(".m4s", "");
                const bool truncate = opaqueInterrupted && name == "test7.m4s" && interrupted++ == 0;
                const QByteArray type = name.endsWith("m3u8") ? "application/vnd.apple.mpegurl" : opaqueInterrupted ? "application/octet-stream" : "audio/mp4";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body.left(body.size() / 2));
                QTimer::singleShot(1500, socket, [socket, body, truncate] {
                    if (!truncate) socket->write(body.mid(body.size() / 2));
                    socket->disconnectFromHost();
                });
            });
        });
        StreamCacheWorker cache;
        QMediaPlayer player; QAudioOutput audio; QAudioBufferOutput output;
        player.setAudioOutput(&audio); player.setAudioBufferOutput(&output);
        int buffers = 0;
        connect(&output, &QAudioBufferOutput::audioBufferReceived, this, [&](const QAudioBuffer &b) { if (b.isValid()) ++buffers; });
        const QUrl remote(QString("http://127.0.0.1:%1/%2").arg(upstream.serverPort()).arg(QFileInfo(fixture).fileName()));
        const auto local = cache.source(remote);
        player.setSource(local); player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > 500 && buffers > 10, 10000);
        // Seek while the destination segments have not been prefetched.
        QVERIFY(!cache.m_cache.contains(cache.m_children.value(remote.toString(QUrl::FullyEncoded)).last()));
        buffers = 0; player.setPosition(75000);
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > 75500 && buffers > 10, 10000);
        QCOMPARE(player.error(), QMediaPlayer::NoError);
        if (opaqueInterrupted) QVERIFY(interrupted >= 2);
        player.pause();
        cache.warm(local);
        QTRY_VERIFY_WITH_TIMEOUT(cache.m_pending.isEmpty() && !cache.m_background && cache.m_cache.size() >= 10, 35000);
        // После прогрева источник отключён: перемотка должна читать только кэш.
        upstream.close();
        for (qint64 target : {60000, 10000, 75000}) {
            buffers = 0; player.setPosition(target); player.play();
            QTRY_VERIFY_WITH_TIMEOUT(player.position() > target + 500 && buffers > 10, 10000);
            QCOMPARE(player.error(), QMediaPlayer::NoError);
        }
#else
        QSKIP("Нужен Qt 6.8 для проверки декодированных буферов");
#endif
    }
    void liveIsNotPrefetched() {
        StreamCacheWorker cache;
        QUrl remote("https://cdn.example/live.m3u8");
        cache.playlist("#EXTM3U\n#EXTINF:5,\na.ts\n", {remote, "live"});
        QVERIFY(cache.m_children.value("live").isEmpty());
    }
};
QTEST_GUILESS_MAIN(StreamCacheTest)
#include "tst_streamcache.moc"
