#include <QtTest>
#include <QSignalSpy>
#include <QMediaPlayer>
#include <QFileInfo>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QAudioBufferOutput>
#include <QAudioBuffer>
#endif
#include <QTcpServer>
#include "streamcache.h"

class StreamCacheTest : public QObject {
    Q_OBJECT
private slots:
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
        StreamCache cache;
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
        StreamCache cache;
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
        StreamCache cache;
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
        cache.m_cache.insert(key, new StreamCache::Entry{path, "audio/mp4"}, 10);
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
        StreamCache cache;
        const auto local = cache.source(QUrl("https://cdn.example/part.bin"));
        cache.m_resources[local.path().mid(1)].remote = QUrl(QString("http://127.0.0.1:%1/part").arg(upstream.serverPort()));
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
        StreamCache cache;
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
    void hlsSeekThroughCache() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        const auto fixture = qEnvironmentVariable("SCRIP_HLS_TEST_PLAYLIST");
        if (fixture.isEmpty()) QSKIP("Запустите с SCRIP_HLS_TEST_PLAYLIST из tests/run-hls-seek.sh");
        QTcpServer upstream; QVERIFY(upstream.listen(QHostAddress::LocalHost));
        connect(&upstream, &QTcpServer::newConnection, this, [&] {
            auto *socket = upstream.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            auto input = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, input] {
                *input += socket->readAll();
                if (!input->contains("\r\n\r\n")) return;
                const auto name = QFileInfo(QString::fromLatin1(input->split(' ').value(1))).fileName();
                QFile file(QFileInfo(fixture).dir().filePath(name));
                if (!file.open(QIODevice::ReadOnly)) { socket->disconnectFromHost(); return; }
                const QByteArray body = file.readAll();
                const QByteArray type = name.endsWith("m3u8") ? "application/vnd.apple.mpegurl" : "audio/mp4";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body.left(body.size() / 2));
                QTimer::singleShot(1500, socket, [socket, body] {
                    socket->write(body.mid(body.size() / 2)); socket->disconnectFromHost();
                });
            });
        });
        StreamCache cache;
        QMediaPlayer player; QAudioBufferOutput output; player.setAudioBufferOutput(&output);
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
        StreamCache cache;
        QUrl remote("https://cdn.example/live.m3u8");
        cache.playlist("#EXTM3U\n#EXTINF:5,\na.ts\n", {remote, "live"});
        QVERIFY(cache.m_children.value("live").isEmpty());
    }
};
QTEST_GUILESS_MAIN(StreamCacheTest)
#include "tst_streamcache.moc"
