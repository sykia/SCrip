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
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 10\r\nContent-Type: audio/mp4\r\nConnection: close\r\n\r\n01234"); socket->flush();
                QTimer::singleShot(200, socket, [socket] { socket->write("56789"); socket->disconnectFromHost(); });
            });
        });
        StreamCache cache;
        const auto local = cache.source(QUrl("https://cdn.example/part.m4s"));
        cache.m_resources[local.path().mid(1)].remote = QUrl(QString("http://127.0.0.1:%1/part").arg(upstream.serverPort()));
        QNetworkAccessManager client;
        auto *reply = client.get(QNetworkRequest(local));
        QSignalSpy chunks(reply, &QNetworkReply::readyRead), done(reply, &QNetworkReply::finished);
        QVERIFY(chunks.wait()); QVERIFY(!reply->isFinished()); // Первые байты идут до завершения загрузки.
        QVERIFY(done.wait()); QCOMPARE(reply->readAll(), QByteArray("0123456789")); reply->deleteLater();
        reply = client.get(QNetworkRequest(local)); QSignalSpy cachedDone(reply, &QNetworkReply::finished);
        QVERIFY(cachedDone.wait()); QCOMPARE(reply->readAll(), QByteArray("0123456789")); QCOMPARE(requests, 1); reply->deleteLater();
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
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
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
        cache.warm(local);
        QTRY_VERIFY_WITH_TIMEOUT(cache.m_pending.isEmpty() && !cache.m_background && cache.m_cache.size() >= 10, 15000);
        // После прогрева источник отключён: перемотка должна читать только кэш.
        upstream.close();
        for (qint64 target : {60000, 10000, 75000}) {
            buffers = 0; player.setPosition(target);
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
