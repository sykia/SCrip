#include "soundcloud.h"
#include "lyrics.h"
#include <QtTest>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <cstring>
#include <cmath>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QDesktopServices>
#include <QSettings>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QDataStream>
#include <QTcpSocket>
#include <QWheelEvent>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

// Транспорт подменяется только в тестах. Рабочий клиент использует фиксированные HTTPS-хосты.
#ifndef Q_MOC_RUN
struct Response { int status = 200; QByteArray body; int delay = 0; QNetworkReply::NetworkError error = QNetworkReply::NoError; int chunkSize = 0; qint64 contentLength = -2; bool lateSignals = false; bool redirectBeforeBody = false; };
class Reply : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, Response response, QObject *parent) : QNetworkReply(parent), bytes(response.body), chunkSize(response.chunkSize > 0 ? response.chunkSize : qMax(1, response.body.size())) {
        setRequest(request); setUrl(request.url()); setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        const auto contentLength = response.contentLength != -2 ? response.contentLength : qint64(response.body.size());
        setHeader(QNetworkRequest::ContentLengthHeader, contentLength);
        setRawHeader("Content-Length", QByteArray::number(contentLength));
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        auto *timer = new QTimer(this); timer->setInterval(1);
        connect(timer, &QTimer::timeout, this, [this, response, timer] {
            if (aborted) { timer->stop(); timer->deleteLater(); return; }
            if (!metadataEmitted) {
                metadataEmitted = true;
                if (response.redirectBeforeBody) {
                    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 302);
                    setHeader(QNetworkRequest::ContentLengthHeader, 0);
                    emit metaDataChanged();
                    if (aborted) return;
                    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
                    setHeader(QNetworkRequest::ContentLengthHeader, response.contentLength != -2 ? response.contentLength : qint64(response.body.size()));
                }
                emit metaDataChanged();
            }
            if (response.error != QNetworkReply::NoError) setError(response.error, "Test network failure");
            else if (response.status >= 400) setError(QNetworkReply::ContentAccessDenied, "Test failure");
            released = qMin(bytes.size(), released + chunkSize);
            emit readyRead();
            if (released >= bytes.size()) { timer->stop(); timer->deleteLater(); setFinished(true); emit finished(); if (response.lateSignals) { emit readyRead(); emit finished(); } }
        });
        QTimer::singleShot(response.delay, timer, [timer] { timer->start(); });
    }
    void abort() override { if (isFinished()) return; aborted = true; setError(QNetworkReply::OperationCanceledError, "Cancelled"); setFinished(true); emit finished(); }
    qint64 bytesAvailable() const override { return released - offset + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 size) override {
        qint64 count = qMin(size, qint64(released - offset));
        if (count <= 0) return -1;
        memcpy(data, bytes.constData() + offset, size_t(count)); offset += count; return count;
    }
private:
    QByteArray bytes; qint64 offset = 0, released = 0; int chunkSize = 1; bool aborted = false, metadataEmitted = false;
};
class Network : public QNetworkAccessManager {
public:
    QList<Response> responses;
    QList<QNetworkRequest> requests;
    QList<QByteArray> bodies;
    QList<QByteArray> methods;
    QSet<QString> responseHosts;
    QHash<QString, QList<Response>> hostResponses;
    bool routeByHost = false;
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoing) override {
        Q_UNUSED(op)
        requests.append(request); bodies.append(outgoing ? outgoing->readAll() : QByteArray{});
        methods.append(request.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray());
        const auto host = request.url().host();
        Response r;
        if (routeByHost) r = hostResponses[host].isEmpty() ? Response{404, "{}"} : hostResponses[host].takeFirst();
        else if (!responseHosts.isEmpty() && !responseHosts.contains(host)) r = {404, "{}"};
        else r = responses.isEmpty() ? Response{500, "{}"} : responses.takeFirst();
        return new Reply(request, r, this);
    }
};
#endif
class CaptureMessages {
public:
    QStringList messages;
    static inline QStringList *destination = nullptr;
    QtMessageHandler previous;
    CaptureMessages() {
        destination = &messages;
        previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
            if (destination) destination->append(message);
        });
    }
    ~CaptureMessages() { qInstallMessageHandler(previous); destination = nullptr; }
};
class MemorySecrets final : public SecretStore {
public:
    QMap<QString, QByteArray> records;
    bool readable = true, writable = true, removable = true, corrupt = false;
    int writesBeforeFailure = -1;
    int readsBeforeFailure = -1;
    bool corruptNextWrite = false;
    bool read(const QString &key, QByteArray &value) override {
        if (!readable) return false;
        if (readsBeforeFailure == 0) { readsBeforeFailure = -1; return false; }
        if (readsBeforeFailure > 0) --readsBeforeFailure;
        value = records.value(key); return true;
    }
    bool write(const QString &key, const QByteArray &value) override {
        if (!writable) return false;
        if (writesBeforeFailure == 0) { writesBeforeFailure = -1; return false; }
        if (writesBeforeFailure > 0) --writesBeforeFailure;
        if (corruptNextWrite) { corruptNextWrite = false; records[key] = QByteArray("corrupt"); return true; }
        records[key] = corrupt ? QByteArray("corrupt") : value; return true;
    }
    bool remove(const QString &key) override {
        if (!removable) return false;
        records.remove(key); return true;
    }
};
class SoundCloudTest : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    QString config;
    std::shared_ptr<MemorySecrets> secrets;
    QUrl authorizationUrl;
    quint16 callbackPort = 0;
public slots:
    void captureUrl(const QUrl &url) { authorizationUrl = url; }
private:
    static Response guest() { return {200, "{\"access_token\":\"test-access\",\"refresh_token\":\"test-refresh\",\"expires_in\":3600}"}; }
    void prepare() {
        secrets = std::make_shared<MemorySecrets>();
        QFile::remove(config + "/session.json");
        QFile file(config + "/soundcloud.json"); QVERIFY(file.open(QIODevice::WriteOnly));
        QTcpServer freePort; QVERIFY(freePort.listen(QHostAddress::LocalHost, 0)); callbackPort = freePort.serverPort();
        file.write(QJsonDocument(QJsonObject{{"client_id", "test-client"}, {"client_secret", "test-secret"}, {"redirect_uri", QString("http://127.0.0.1:%1/callback").arg(callbackPort)}}).toJson());
    }
private slots:
#ifdef Q_OS_LINUX
    void linuxSecretInitializationDoesNotBlockOrOutliveOwner() {
        QElapsedTimer timer; timer.start();
        {
            Network net;
            SoundCloud sc(nullptr, &net);
            QVERIFY2(timer.elapsed() < 3000, "SoundCloud construction blocked on Secret Service");
            sc.login(); sc.connectCatalog();
            QCoreApplication::processEvents();
            QVERIFY(net.requests.isEmpty());
        }
        // Completion may arrive after the owner is gone; the watcher is context-bound.
        QTest::qWait(20);
    }
#endif
    void playlistPreviewPreservesCatalogAndRejectsStaleResponses() {
        Network net;
        SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false);
        sc.m_access = "test-access";
        sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        const QVariantList catalog{QVariantMap{{"urn", "soundcloud:playlists:1"}}};
        sc.set("tracks", catalog);
        sc.set("heading", "Catalog");
        const QVariantMap first{{"urn", "soundcloud:playlists:1"}};
        const QVariantMap second{{"urn", "soundcloud:playlists:2"}};
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"}]}", 80},
                         {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:2\"}],\"next_href\":\"https://api.soundcloud.com/playlists/2/tracks?cursor=next\"}"}};
        sc.loadPlaylist(first);
        sc.loadPlaylist(second);
        QTRY_VERIFY(!sc.state()["playlistBusy"].toBool());
        QCOMPARE(sc.state()["playlistTracks"].toList().first().toMap()["urn"].toString(), QString("soundcloud:tracks:2"));
        QTest::qWait(100);
        QCOMPARE(sc.state()["playlistTracks"].toList().size(), 1);
        QCOMPARE(sc.state()["tracks"].toList(), catalog);
        QCOMPARE(sc.state()["heading"].toString(), QString("Catalog"));
        QVERIFY(sc.state()["playlistHasMore"].toBool());
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:2\"},{\"urn\":\"soundcloud:tracks:3\"}]}"}};
        sc.loadPlaylist(second, true);
        QTRY_VERIFY(!sc.state()["playlistBusy"].toBool());
        QCOMPARE(sc.state()["playlistTracks"].toList().size(), 2);
        QVERIFY(!sc.state()["playlistHasMore"].toBool());
    }
    void stationAndMixPreviewsDoNotStartPlayback() {
        Network net;
        SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false);
        sc.m_access = "test-access";
        sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        const QVariantMap seed{{"urn", "soundcloud:tracks:1"}, {"title", "Seed"}};
        const QVariantMap related{{"urn", "soundcloud:tracks:2"}, {"title", "Related"}};
        const QVariantMap station{{"urn", seed["urn"]}, {"preview_type", "station"}, {"seed", seed}};
        const QVariantList catalog{QVariantMap{{"urn", "soundcloud:playlists:1"}}};
        sc.set("tracks", catalog);
        net.responses = {{200, QJsonDocument(QJsonObject{{"collection", QJsonArray{QJsonObject::fromVariantMap(seed), QJsonObject::fromVariantMap(related)}}}).toJson()}};
        sc.loadPlaylist(station);
        QTRY_VERIFY(!sc.state()["playlistBusy"].toBool());
        QCOMPARE(sc.state()["playlistTracks"].toList().size(), 2);
        QCOMPARE(sc.state()["playlistTracks"].toList().first().toMap()["urn"], seed["urn"]);
        QVERIFY(net.requests.last().url().path().endsWith("/related"));
        QVERIFY(sc.state()["current"].toMap().isEmpty());
        QVERIFY(sc.state()["queue"].toList().isEmpty());
        QCOMPARE(sc.state()["tracks"].toList(), catalog);
        net.responses = {{200, QJsonDocument(QJsonArray{QJsonObject::fromVariantMap(seed)}).toJson(), 80}};
        sc.loadPlaylist(station);
        const auto requestCount = net.requests.size();
        sc.loadPlaylist({{"preview_type", "mix"}, {"tracks", QVariantList{related}}});
        QCOMPARE(net.requests.size(), requestCount);
        QTest::qWait(100);
        QCOMPARE(sc.state()["playlistTracks"].toList().size(), 1);
        QCOMPARE(sc.state()["playlistTracks"].toList().first().toMap()["urn"], related["urn"]);
        QVERIFY(!sc.state()["playlistBusy"].toBool());
        QVERIFY(!sc.state()["playlistHasMore"].toBool());
        QVERIFY(sc.state()["current"].toMap().isEmpty());
    }
    void volumeAndCrossfadePreferencesRestore() {
        QSettings preferences;
        preferences.remove("player/volume"); preferences.remove("player/crossfade");
        {
            Network firstRunNet;
            SoundCloud firstRun(nullptr, &firstRunNet, secrets);
            QVERIFY(qAbs(firstRun.state()["volume"].toDouble() - 0.7) < 0.001);
            QVERIFY(!firstRun.state()["crossfade"].toBool());
        }
        preferences.setValue("player/volume", 0.42);
        preferences.setValue("player/crossfade", false);
        {
            Network net;
            SoundCloud sc(nullptr, &net, secrets);
            QVERIFY(qAbs(sc.state()["volume"].toDouble() - 0.42) < 0.001);
            QVERIFY(!sc.state()["crossfade"].toBool());
            sc.volume(0.25);
            sc.setCrossfade(true);
            QCOMPARE(preferences.value("player/volume").toDouble(), 0.25);
            QVERIFY(preferences.value("player/crossfade").toBool());
        }
        Network net;
        SoundCloud restarted(nullptr, &net, secrets);
        QVERIFY(qAbs(restarted.state()["volume"].toDouble() - 0.25) < 0.001);
        QVERIFY(restarted.state()["crossfade"].toBool());
        preferences.remove("player/volume"); preferences.remove("player/crossfade");
    }
    void playbackProgressDoesNotInvalidateCatalog() {
        Network net;
        SoundCloud sc(nullptr, &net, secrets);
        QSignalSpy legacy(&sc, &SoundCloud::changed);
        QSignalSpy catalog(&sc, &SoundCloud::catalogChanged);
        QSignalSpy progress(&sc, &SoundCloud::progressChanged);
        const QList<QMediaPlayer *> players{&sc.m_player, &sc.m_sparePlayer};
        // Сигнал backend: активный плеер обновляет прогресс, запасной игнорируется.
        for (auto *media : players)
            media->positionChanged(1234);
        QCOMPARE(sc.position(), 1234);
        QCOMPARE(progress.count(), 1);
        QCOMPARE(legacy.count(), 1);
        QCOMPARE(catalog.count(), 0);
        sc.m_activePlayer->durationChanged(90000);
        QCOMPARE(sc.duration(), 90000);
        QCOMPARE(progress.count(), 2);
        QCOMPARE(catalog.count(), 0);
        sc.m_activePlayer->positionChanged(1234); // Повторное значение не уведомляет QML.
        QCOMPARE(progress.count(), 2);
        sc.setRepeat(!sc.state().value("repeat").toBool());
        QCOMPARE(catalog.count(), 1);
        QCOMPARE(progress.count(), 2);
    }
    void androidRestoreWaitsForSessionAndDoesNotAutoplay() {
        Network net; net.responses = {{200, "{\"hls_aac_160_url\":\"https://cdn.soundcloud.com/test.m3u8\"}"}};
        SoundCloud sc(nullptr, &net, secrets);
        const QVariantMap track{{"urn", "soundcloud:tracks:restore"}, {"title", "Восстановленный трек"}, {"duration", 120000}};
        sc.restoreAndroidTrack(track, 42000);
        QVERIFY(sc.m_androidRestorePending); QVERIFY(sc.state()["current"].toMap().isEmpty());
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.set("connected", true); sc.set("signedIn", true);
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), track["urn"].toString());
        QCOMPARE(sc.state()["queueIndex"].toInt(), 0);
        QVERIFY(!sc.state()["playbackActive"].toBool()); QVERIFY(!sc.state()["playing"].toBool());
        QVERIFY(sc.m_restoringPlayback); QCOMPARE(sc.m_restorePosition, qint64(42000));
        QTRY_VERIFY(!net.requests.isEmpty());
        QVERIFY(!sc.state()["playbackActive"].toBool());
    }
    void androidRestoreWithoutSessionIsDiscarded() {
        QSettings settings;
        settings.setValue("playback/session", QByteArray("stale snapshot"));
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.restoreAndroidTrack({{"urn", "soundcloud:tracks:restore"}, {"title", "Трек"}}, 9000);
        QVERIFY(sc.m_androidRestorePending);
        sc.set("connected", true); sc.tryRestoreAndroidTrack();
        QVERIFY(!sc.m_androidRestorePending); QVERIFY(sc.state()["restoreCleared"].toBool());
        QVERIFY(sc.state()["current"].toMap().isEmpty()); QVERIFY(!sc.state()["playbackActive"].toBool());
        settings.remove("playback/session");
    }
    void androidRestoreFailureClearsTrackAndQueue() {
        Network net; net.responses = {{500, "{}"}};
        SoundCloud sc(nullptr, &net, secrets); sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.set("connected", true); sc.set("signedIn", true);
        sc.restoreAndroidTrack({{"urn", "soundcloud:tracks:missing"}, {"title", "Удалённый трек"}}, 15000);
        QTRY_VERIFY(sc.state()["restoreCleared"].toBool());
        QVERIFY(sc.state()["current"].toMap().isEmpty()); QVERIFY(sc.state()["queue"].toList().isEmpty());
        QCOMPARE(sc.state()["queueIndex"].toInt(), -1); QVERIFY(!sc.state()["buffering"].toBool());
    }
    void androidRestoreIsInvalidatedByNewSelectionAndStop() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.restoreAndroidTrack({{"urn", "soundcloud:tracks:old"}}, 0);
        QVERIFY(sc.m_androidRestorePending);
        sc.play({{"urn", "soundcloud:tracks:new"}, {"title", "Новый трек"}});
        QVERIFY(!sc.m_androidRestorePending);
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), QString("soundcloud:tracks:new"));
        sc.stopPlayback();
        QVERIFY(sc.state()["restoreCleared"].toBool()); QVERIFY(sc.state()["current"].toMap().isEmpty());
    }
    void lateAndroidRestoreResponseCannotReplaceNewTrack() {
        Network net; net.responses = {{200, "{\"hls_aac_160_url\":\"https://cdn.soundcloud.com/old.m3u8\"}", 100},
            {200, "{\"hls_aac_160_url\":\"https://cdn.soundcloud.com/new.m3u8\"}"}};
        SoundCloud sc(nullptr, &net, secrets); sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.set("connected", true); sc.set("signedIn", true);
        sc.restoreAndroidTrack({{"urn", "soundcloud:tracks:old"}}, 2500);
        QTRY_COMPARE(net.requests.size(), 1);
        sc.play({{"urn", "soundcloud:tracks:new"}, {"title", "Новый"}});
        QTRY_COMPARE(net.requests.size(), 2);
        QTest::qWait(150);
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), QString("soundcloud:tracks:new"));
        QVERIFY(!sc.state()["restoreCleared"].toBool());
    }
    void initTestCase() {
#ifdef Q_OS_WIN
        QStandardPaths::setTestModeEnabled(true);
#endif
        QCoreApplication::setOrganizationName("SCripTests"); QCoreApplication::setApplicationName("SCripTests");
        QVERIFY(directory.isValid()); qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
        qputenv("SCRIP_AUTH_MODE", "local");
        qunsetenv("SOUNDCLOUD_CLIENT_ID"); qunsetenv("SOUNDCLOUD_CLIENT_SECRET");
        config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/SCrip";
        QVERIFY(QDir().mkpath(config));
    }
    void init() { qputenv("SCRIP_AUTH_MODE", "local"); prepare(); }
    void migratesSecretsAndRestoresSession() {
        const QJsonObject session{{"client_id", "test-client"}, {"access_token", "old-access"}, {"refresh_token", "old-refresh"},
            {"expires_at", QDateTime::currentSecsSinceEpoch() + 3600}, {"user_session", true}};
        QFile file(config + "/session.json"); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(session).toJson()); file.close();
        Network net;
        SoundCloud sc(nullptr, &net, secrets);
        QCOMPARE(sc.m_access, QString("old-access")); QCOMPARE(sc.m_refresh, QString("old-refresh"));
        QVERIFY(sc.m_userSession);
        QVERIFY(!QFile::exists(config + "/session.json")); QVERIFY(!QFile::exists(config + "/soundcloud.json"));
        QCOMPARE(QJsonDocument::fromJson(secrets->records["session"]).object(), session);
        QVERIFY(!QJsonDocument::fromVariant(sc.state()).toJson().contains("old-access"));
        QVERIFY(!QJsonDocument::fromVariant(sc.state()).toJson().contains("test-secret"));
        SoundCloud restored(nullptr, &net, secrets);
        QCOMPARE(restored.m_refresh, QString("old-refresh"));
    }
    void migrationFailurePreservesLegacy_data() {
        QTest::addColumn<int>("failure");
        QTest::newRow("read") << 0; QTest::newRow("write") << 1; QTest::newRow("readback") << 2;
    }
    void migrationFailurePreservesLegacy() {
        QFETCH(int, failure);
        secrets->readable = failure != 0; secrets->writable = failure != 1; secrets->corrupt = failure == 2;
        Network net;
        SoundCloud sc(nullptr, &net, secrets);
        QVERIFY(QFile::exists(config + "/soundcloud.json"));
        QVERIFY(!sc.m_storageReady); QVERIFY(sc.m_clientSecret.isEmpty());
        sc.login(); sc.connectCatalog();
        QCoreApplication::processEvents(); QVERIFY(net.requests.isEmpty());
        QVERIFY(sc.state()["error"].toString().contains("хранилище"));
        QVERIFY(!sc.state()["error"].toString().contains("test-secret"));
    }
    void sessionMigrationFailurePreservesTokens() {
        AuthStorage storage(secrets);
        QFile file(config + "/session.json"); QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray legacy = "{\"access_token\":\"old-access\",\"refresh_token\":\"old-refresh\"}";
        file.write(legacy); file.close(); secrets->writable = false;
        QJsonObject loaded;
        QVERIFY(!storage.load("session", file.fileName(), loaded));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), legacy); file.close();
        QVERIFY(loaded.isEmpty());
        secrets->writable = true;
        QVERIFY(storage.load("session", file.fileName(), loaded));
        QCOMPARE(loaded["refresh_token"].toString(), QString("old-refresh"));
        QVERIFY(!file.exists());
    }
    void migrationCleanupFailureIsReported() {
        AuthStorage storage(secrets);
        QVERIFY(storage.save("session", {{"refresh_token", "new-refresh"}}));
        QTemporaryDir legacy;
        QJsonObject loaded;
        QVERIFY(!storage.load("session", legacy.path(), loaded));
        QVERIFY(QFileInfo::exists(legacy.path()));
        QVERIFY(secrets->records.contains("session"));
    }
    void failedAuthStorageRollbackIsReported() {
        AuthStorage storage(secrets);
        const QJsonObject previous{{"refresh_token", "old-refresh"}};
        const QJsonObject candidate{{"refresh_token", "new-refresh"}};
        QVERIFY(storage.save("session", previous));
        secrets->readsBeforeFailure = 1; // allow the pre-write read, fail verification once
        secrets->writesBeforeFailure = 1; // allow candidate write, reject rollback
        QVERIFY(!storage.save("session", candidate));
        const auto quarantined = QJsonDocument::fromJson(secrets->records["session"]).object();
        QVERIFY(quarantined["access_token"].toString().isEmpty());
        QVERIFY(quarantined["refresh_token"].toString().isEmpty());
        QVERIFY(!secrets->records["session"].contains("old-refresh"));
        QVERIFY(!secrets->records["session"].contains("new-refresh"));
        QJsonObject loaded;
        QVERIFY(!storage.load("session", {}, loaded));
        AuthStorage restartedStorage(secrets);
        QVERIFY(restartedStorage.load("session", {}, loaded));
        QVERIFY(loaded["access_token"].toString().isEmpty());
        QVERIFY(loaded["refresh_token"].toString().isEmpty());
    }
    void hostileOAuthErrorDoesNotExposeBody() {
        QStringList logs;
        QVariantMap state;
        {
            CaptureMessages capture;
            Network net; net.responses = {{400, "{\"error_description\":\"private-server-detail\",\"access_token\":\"private-token\"}"}};
            SoundCloud sc(nullptr, &net, secrets);
            QTRY_VERIFY(!sc.state()["error"].toString().isEmpty());
            state = sc.state(); logs = capture.messages;
        }
        QVERIFY(!QJsonDocument::fromVariant(state).toJson().contains("private-"));
        QVERIFY(!logs.join('\n').contains("private-"));
        QVERIFY(!logs.join('\n').contains("test-secret"));
    }
    void interruptedMigrationPrefersSecureRecord() {
        AuthStorage storage(secrets);
        const QJsonObject latest{{"refresh_token", "latest"}};
        QVERIFY(storage.save("session", latest));
        QFile file(config + "/session.json"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{invalid"); file.close();
        QJsonObject loaded;
        QVERIFY(storage.load("session", file.fileName(), loaded)); QCOMPARE(loaded, latest);
        QVERIFY(!file.exists());
    }
    void failedRefreshPersistenceNeverWritesPlaintext() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets);
        int failures = 0;
        sc.request("/waiting", [](QJsonDocument) {}, "GET", {}, true, [&failures](QString message) {
            QVERIFY(message.contains("хранилище")); ++failures;
        });
        secrets->writable = false;
        QTRY_VERIFY(!sc.state()["error"].toString().isEmpty());
        QCOMPARE(failures, 1);
        QVERIFY(sc.m_storageReady); QVERIFY(!sc.state()["connected"].toBool());
        QVERIFY(!QFile::exists(config + "/session.json"));
        QVERIFY(!sc.state()["error"].toString().contains("test-access"));
        QVERIFY(!sc.state()["error"].toString().contains("test-refresh"));
    }
    void authenticatedLogoutRevokesAndErasesSession() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.m_userSession = true; sc.set("signedIn", true);
        net.responses = {{200, "{}"}};
        sc.logout();
        QTRY_VERIFY(!sc.state()["connected"].toBool());
        QCOMPARE(net.requests.last().url().path(), QString("/disconnect"));
        QCOMPARE(net.requests.last().rawHeader("Authorization"), QByteArray("OAuth test-access"));
        QVERIFY(!secrets->records.contains("session"));
        QVERIFY(secrets->records.contains("connection"));
        QVERIFY(sc.m_access.isEmpty()); QVERIFY(sc.m_refresh.isEmpty());
        QVERIFY(!secrets->records["session"].contains("old-access"));
        QVERIFY(!secrets->records["session"].contains("refresh-invalid"));
    }
    void logoutIsImmediateWhenRevocationFailsOrIsLate() {
        const QList<Response> failures = {
            {401, "{}"}, {500, "{}"},
            {0, "{}", 0, QNetworkReply::HostNotFoundError},
            {200, "{}", 120}
        };
        for (const auto &response : failures) {
            Network net; net.responses = {guest(), response};
            SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
            sc.m_userSession = true; sc.set("signedIn", true);
            QVERIFY(secrets->records.contains("session"));
            sc.logout();
            QVERIFY(!sc.state()["connected"].toBool()); QVERIFY(!sc.signedIn());
            QVERIFY(sc.m_access.isEmpty()); QVERIFY(sc.m_refresh.isEmpty());
            QVERIFY(!secrets->records.contains("session"));
            QCOMPARE(net.requests.size(), 2);
            sc.logout();
            QCOMPARE(net.requests.size(), 2);
            QTest::qWait(150);
            QVERIFY(!sc.state()["connected"].toBool()); QVERIFY(!sc.signedIn());
            QVERIFY(!secrets->records.contains("session"));
            QVERIFY(sc.state()["error"].toString().isEmpty());
        }
    }
    void failedLogoutDeletionIsVisible() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        secrets->removable = false;
        sc.logout();
        QVERIFY(!sc.state()["connected"].toBool()); QVERIFY(sc.m_access.isEmpty());
        QVERIFY(sc.state()["error"].toString().contains("хранилище"));
        QVERIFY(secrets->records.contains("session"));
        const auto quarantined = QJsonDocument::fromJson(secrets->records["session"]).object();
        QVERIFY(quarantined["access_token"].toString().isEmpty());
        QVERIFY(quarantined["refresh_token"].toString().isEmpty());
        Network restartNet; SoundCloud restarted(nullptr, &restartNet, secrets);
        QVERIFY(restarted.m_access.isEmpty()); QVERIFY(restarted.m_refresh.isEmpty());
        secrets->removable = true;
        sc.logout(); QVERIFY(!secrets->records.contains("session"));
    }
    void trackModelUpdatesWithoutReset() {
        TrackModel model;
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        const QVariantMap first{{"urn", "soundcloud:tracks:1"}, {"user_favorite", false}};
        const QVariantMap second{{"urn", "soundcloud:tracks:2"}};
        model.update({first}); model.update({first, second});
        QCOMPARE(resets.size(), 0); QCOMPARE(inserted.size(), 2);
        auto liked = first; liked["user_favorite"] = true;
        model.update({liked, second});
        QCOMPARE(resets.size(), 0); QCOMPARE(changed.size(), 1);
        QCOMPARE(model.data(model.index(0), Qt::UserRole + 1).toMap(), liked);
    }
    void cachedPagesAvoidReloadAndSupportRefresh() {
        Network net; net.responses = {guest(), {200, R"({"collection":[{"urn":"soundcloud:tracks:1"}]})"},
            {200, R"({"collection":[{"urn":"soundcloud:tracks:2"}]})"},
            {200, R"({"collection":[{"urn":"soundcloud:tracks:3"}]})"}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.browse("tracks", "one"); QTRY_COMPARE(sc.state()["tracks"].toList().size(), 1);
        sc.browse("tracks", "two"); QTRY_COMPARE(sc.state()["tracks"].toList().size(), 1); QCOMPARE(sc.state()["tracks"].toList().first().toMap()["urn"].toString(), QString("soundcloud:tracks:2"));
        const int requests = net.requests.size();
        sc.browse("tracks", "one"); QCOMPARE(net.requests.size(), requests);
        QCOMPARE(sc.state()["tracks"].toList().first().toMap()["urn"].toString(), QString("soundcloud:tracks:1"));
        sc.browse("tracks", "one", true);
        QTRY_COMPARE(sc.state()["tracks"].toList().size(), 1);
        QCOMPARE(sc.state()["tracks"].toList().first().toMap()["urn"].toString(), QString("soundcloud:tracks:3"));
    }
    void playbackSnapshotRestoresOnlyForSameAccount() {
        QSettings settings; settings.remove("playback/session"); settings.remove("playback/position");
        const QVariantMap profile{{"urn", "soundcloud:users:1"}};
        const QVariantMap first{{"urn", "soundcloud:tracks:1"}, {"title", "Первый"}, {"duration", 180000}};
        const QVariantMap second{{"urn", "soundcloud:tracks:2"}, {"title", "Второй"}, {"duration", 200000}};
        {
            Network net; SoundCloud sc(nullptr, &net, secrets);
            sc.m_restoreChecked = true; sc.set("signedIn", true); sc.set("profile", profile);
            sc.m_queue = {first, second}; sc.m_queueIndex = 1;
            sc.set("current", second); sc.set("position", 42000); sc.savePlayback();
        }
        {
            Network net; SoundCloud sc(nullptr, &net, secrets);
            sc.set("signedIn", true); sc.restorePlayback(profile);
            QCOMPARE(sc.state()["queue"].toList(), QVariantList({first, second}));
            QCOMPARE(sc.state()["queueIndex"].toInt(), 1);
            QCOMPARE(sc.state()["current"].toMap(), second);
            QCOMPARE(sc.position(), 42000); QVERIFY(!sc.state()["playbackActive"].toBool());
            QVERIFY(net.requests.isEmpty());
        }
        {
            Network net; SoundCloud sc(nullptr, &net, secrets);
            sc.set("signedIn", true); sc.restorePlayback({{"urn", "soundcloud:users:other"}});
            QVERIFY(sc.state()["current"].toMap().isEmpty());
        }
        settings.remove("playback/session"); settings.remove("playback/position");
    }
    void playbackPositionAndQueueAreSavedIndependently() {
        QSettings settings; settings.remove("playback/session"); settings.remove("playback/position");
        const QVariantMap profile{{"urn", "soundcloud:users:position-test"}};
        const QVariantMap first{{"urn", "soundcloud:tracks:101"}, {"title", "Первый"}};
        const QVariantMap second{{"urn", "soundcloud:tracks:102"}, {"title", "Второй"}};
        {
            Network net; SoundCloud sc(nullptr, &net, secrets);
            sc.m_restoreChecked = true; sc.set("signedIn", true); sc.set("profile", profile);
            sc.m_queue = {first}; sc.m_queueIndex = 0; sc.set("current", first); sc.set("position", 1000); sc.savePlayback();
            sc.enqueue(second);
            sc.set("position", 12750); sc.savePlaybackPosition();
            const auto session = QJsonDocument::fromJson(settings.value("playback/session").toByteArray()).object();
            QCOMPARE(session.value("queue").toArray().size(), 2);
            QCOMPARE(session.value("position").toInt(), 1000);
        }
        {
            Network net; SoundCloud sc(nullptr, &net, secrets);
            sc.set("signedIn", true); sc.restorePlayback(profile);
            QCOMPARE(sc.state()["queue"].toList().size(), 2);
            QCOMPARE(sc.position(), 12750);
        }
        settings.remove("playback/session"); settings.remove("playback/position");
    }
    void seekDuringLoadingOverridesRestoredPosition() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        QTemporaryDir files; QVERIFY(files.isValid());
        QFile wav(files.filePath("seek.wav")); QVERIFY(wav.open(QIODevice::WriteOnly));
        QDataStream data(&wav); data.setByteOrder(QDataStream::LittleEndian);
        constexpr int bytes = 44100 * 2 * 10;
        data.writeRawData("RIFF", 4); data << quint32(bytes + 36);
        data.writeRawData("WAVEfmt ", 8); data << quint32(16) << quint16(1) << quint16(1)
            << quint32(44100) << quint32(88200) << quint16(2) << quint16(16);
        data.writeRawData("data", 4); data << quint32(bytes);
        wav.write(QByteArray(bytes, '\0')); wav.close();
        sc.set("current", QVariantMap{{"urn", "soundcloud:tracks:seek"}, {"duration", 10000}});
        sc.set("duration", 10000);
        sc.m_restoringPlayback = true; sc.m_restorePosition = 1000;
        QSignalSpy seeks(&sc, &SoundCloud::playbackSeeked);
        // A click while the stream URL is still resolving must survive setSource.
        sc.seek(7000);
        QCOMPARE(sc.position(), 7000);
        QCOMPARE(sc.m_pendingSeek, 7000);
        sc.m_player.setSource(QUrl::fromLocalFile(wav.fileName()));
        QTRY_COMPARE(sc.m_pendingSeek, -1);
        QTRY_COMPARE(sc.m_player.position(), 7000);
        QCOMPARE(sc.position(), 7000);
        QVERIFY(!sc.m_restoringPlayback);
        QCOMPARE(seeks.size(), 1);
        sc.seek(2000); QTRY_COMPARE(sc.m_player.position(), 2000);
        QCOMPARE(sc.m_player.playbackState(), QMediaPlayer::StoppedState);
        sc.stopPlayback(); QCOMPARE(sc.m_pendingSeek, -1);
    }
    void likesSyncUpdatesEveryContextAndFutureTracks() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("signedIn", true);
        const QVariantMap track{{"urn", "soundcloud:tracks:2"}, {"user_favorite", false}};
        sc.set("current", track); sc.set("tracks", QVariantList{track});
        sc.set("queue", QVariantList{track});
        sc.set("home", QVariantList{QVariantMap{{"tracks", QVariantList{track}}}});
        sc.m_listings.insert("cached", {QVariantList{track}, {}});
        net.responses = {
            {200, "{\"collection\":[{\"track\":{\"urn\":\"soundcloud:tracks:1\"}}],\"next_href\":\"https://api.soundcloud.com/me/likes/tracks?cursor=2\"}"},
            {200, "{\"collection\":[{\"track\":{\"urn\":\"soundcloud:tracks:2\"}}]}"}};
        sc.syncLikes();
        QTRY_VERIFY(sc.m_likesComplete);
        QVERIFY(sc.state()["current"].toMap()["user_favorite"].toBool());
        QVERIFY(sc.state()["tracks"].toList()[0].toMap()["user_favorite"].toBool());
        QVERIFY(sc.m_queue[0].toMap()["user_favorite"].toBool());
        QVERIFY(sc.state()["home"].toList()[0].toMap()["tracks"].toList()[0].toMap()["user_favorite"].toBool());
        QVERIFY(sc.m_listings["cached"].tracks[0].toMap()["user_favorite"].toBool());
        sc.set("current", QVariantMap{{"urn", "soundcloud:tracks:1"}});
        QVERIFY(sc.state()["current"].toMap()["user_favorite"].toBool());
        // A stale queue entry must still send DELETE, then stay unliked after a late sync page.
        net.responses = {{204, ""}};
        sc.like(track);
        QTRY_VERIFY(sc.m_likeOverrides.contains("soundcloud:tracks:2"));
        QCOMPARE(net.methods.last(), QByteArray("DELETE"));
        sc.m_likedTracks.insert("soundcloud:tracks:2"); sc.refreshLikeStates();
        QVERIFY(!sc.m_queue[0].toMap()["user_favorite"].toBool());
        sc.set("current", track); QVERIFY(!sc.state()["current"].toMap()["user_favorite"].toBool());
        sc.set("signedIn", false);
        QVERIFY(sc.m_likedTracks.isEmpty()); QVERIFY(sc.m_likeOverrides.isEmpty());
    }
    void progressSliderClickAndDrag() {
        for (const auto &path : {"/qml/Main.qml", "/qml/android/Main.qml"}) {
            QFile file(QString(SCRIP_SOURCE_DIR) + path); QVERIFY(file.open(QIODevice::ReadOnly));
            const QString source = QString::fromUtf8(file.readAll());
            const bool android = QString(path).contains("android");
            const QString id = android ? "progress" : "trackProgress";
            const int start = source.lastIndexOf(android ? "FineSlider {" : "LineSlider {", source.indexOf("id: " + id));
            int end = start, depth = 0;
            for (; end < source.size(); ++end) {
                if (source[end] == '{') ++depth;
                if (source[end] == '}' && --depth == 0) { ++end; break; }
            }
            QString slider = source.mid(start, end - start);
            slider.replace(android ? "FineSlider {" : "LineSlider {", "Slider {");
            slider.replace("Layout.fillWidth: true;", "");
            slider.replace("Layout.fillWidth: true", "");
            QQmlEngine engine;
            QQmlComponent component(&engine);
            component.setData(("import QtQuick; import QtQuick.Controls; Item { width: 400; height: 60; "
                "QtObject { id: sc; property real position: 10000; property real duration: 100000; "
                "property real target: -1; function seek(value) { target = value; position = value } } "
                "property alias target: sc.target; " + slider + " }").toUtf8(), QUrl());
            QScopedPointer<QObject> root(component.create());
            QVERIFY2(root, qPrintable(component.errorString()));
            auto *item = qobject_cast<QQuickItem *>(root.data()); QVERIFY(item);
            QQuickWindow window; window.resize(400, 60); item->setParentItem(window.contentItem());
            auto *control = item->childItems().first(); control->setWidth(400); control->setHeight(60);
            window.show(); QTest::qWait(80);
            QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(300, 30));
            QVERIFY2(root->property("target").toDouble() > 70000, qPrintable(path));
            QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(300, 30));
            QTest::mouseMove(&window, QPoint(100, 30));
            QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(100, 30));
            QVERIFY(root->property("target").toDouble() < 30000);
            item->setParentItem(nullptr);
        }
    }
    void repostLikesAreMatchedAcrossPages() {
        Network net;
        net.responses = {guest(),
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"},{\"urn\":\"soundcloud:tracks:2\"}]}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:9\"}],\"next_href\":\"https://api.soundcloud.com/me/likes/tracks?cursor=2\"}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:2\"}]}"}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("signedIn", true); sc.browse("reposts");
        QTRY_VERIFY(sc.state()["tracks"].toList().size() == 2 && sc.state()["tracks"].toList()[1].toMap().value("user_favorite").toBool());
        QVERIFY(!sc.state()["tracks"].toList()[0].toMap().value("user_favorite").toBool());
        QCOMPARE(net.requests.size(), 4);
    }
    void authenticatesAndSearches() {
        Network net; net.responses = {guest(), {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\",\"title\":\"Real response\"}],\"next_href\":null}"}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        QCOMPARE(net.requests.size(), 1); QVERIFY(net.requests[0].rawHeader("Authorization").startsWith("Basic "));
        QCOMPARE(net.bodies[0], QByteArray("grant_type=client_credentials"));
        sc.browse("tracks", "rock & roll"); QTRY_COMPARE(sc.state()["tracks"].toList().size(), 1);
        QCOMPARE(QUrlQuery(net.requests.last().url()).queryItemValue("q"), QString("rock & roll"));
        QCOMPARE(net.requests.last().rawHeader("Authorization"), QByteArray("OAuth test-access"));
        QVERIFY(!sc.state()["busy"].toBool()); QVERIFY(sc.state()["error"].toString().isEmpty());
    }
    void rejectsForeignPagination() {
        Network net; net.responses = {guest(), {200, "{\"collection\":[],\"next_href\":\"https://evil.example/steal\"}"}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.browse("tracks", "test"); QTRY_VERIFY(sc.state()["hasMore"].toBool()); sc.more();
        QCOMPARE(net.requests.size(), 2); QVERIFY(!sc.state()["error"].toString().isEmpty());
    }
    void deduplicatesPagination() {
        Network net; net.responses = {guest(), {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"}],\"next_href\":\"https://api.soundcloud.com/tracks?cursor=2\"}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"},{\"urn\":\"soundcloud:tracks:2\"}]}"}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.browse("tracks", "test"); QTRY_VERIFY(sc.state()["hasMore"].toBool()); sc.more();
        QTRY_COMPARE(sc.state()["tracks"].toList().size(), 2); QVERIFY(!sc.state()["hasMore"].toBool());
        const auto tracks = sc.state()["tracks"].toList();
        QCOMPARE(tracks[0].toMap()["urn"].toString(), QString("soundcloud:tracks:1"));
        QCOMPARE(tracks[1].toMap()["urn"].toString(), QString("soundcloud:tracks:2"));
    }
    void deduplicatesWithinPageAndPreservesOrder() {
        Network net; net.responses = {guest(), {200, R"({"collection":[
            {"urn":"soundcloud:tracks:3","title":"Первый"},
            {"urn":"soundcloud:tracks:1"},
            {"urn":"soundcloud:tracks:3","title":"Дубликат"},
            {"urn":"soundcloud:tracks:2"}]})"}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.browse("tracks", "test"); QTRY_COMPARE(sc.state()["tracks"].toList().size(), 3);
        const auto tracks = sc.state()["tracks"].toList();
        QCOMPARE(tracks[0].toMap()["urn"].toString(), QString("soundcloud:tracks:3"));
        QCOMPARE(tracks[0].toMap()["title"].toString(), QString("Первый"));
        QCOMPARE(tracks[1].toMap()["urn"].toString(), QString("soundcloud:tracks:1"));
        QCOMPARE(tracks[2].toMap()["urn"].toString(), QString("soundcloud:tracks:2"));
    }
    void deduplicatesLargeCollection() {
        QJsonArray page;
        for (int i = 0; i < 2000; ++i) {
            const auto id = QString("soundcloud:tracks:%1").arg(i);
            page.append(QJsonObject{{"urn", id}});
            page.append(QJsonObject{{"urn", id}});
        }
        Network net; net.responses = {guest(), {200, QJsonDocument(QJsonObject{{"collection", page}}).toJson()}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.browse("tracks", "large"); QTRY_COMPARE(sc.state()["tracks"].toList().size(), 2000);
        const auto tracks = sc.state()["tracks"].toList();
        QCOMPARE(tracks.first().toMap()["urn"].toString(), QString("soundcloud:tracks:0"));
        QCOMPARE(tracks.last().toMap()["urn"].toString(), QString("soundcloud:tracks:1999"));
    }
    void ignoresOutdatedResults() {
        Network net; net.responses = {guest(), {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"}]}", 100}, {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:2\"}]}"}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.browse("tracks", "old"); sc.browse("tracks", "new");
        QTRY_COMPARE(sc.state()["tracks"].toList().size(), 1); QTest::qWait(150);
        QCOMPARE(sc.state()["tracks"].toList()[0].toMap()["urn"].toString(), QString("soundcloud:tracks:2"));
    }
    void independentRequestErrorsDoNotChangePlayerBuffering() {
        for (const auto &response : {Response{500, "{}"}, Response{200, "{invalid"}}) {
            Network net; net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"}]}", 100}, response};
            SoundCloud sc(nullptr, &net, secrets);
            sc.set("configured", false); sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
            sc.set("buffering", true); sc.browse("tracks", "loading"); sc.profile();
            QTRY_VERIFY(!sc.state()["error"].toString().isEmpty());
            QVERIFY(sc.state()["busy"].toBool());
            QVERIFY(sc.state()["buffering"].toBool());
            QTRY_VERIFY(!sc.state()["busy"].toBool());
            QCOMPARE(sc.state()["tracks"].toList().size(), 1);
            QVERIFY(sc.state()["buffering"].toBool());
        }
    }
    void stalePageFailureDoesNotChangeCurrentPagination() {
        Network net; net.responses = {guest(),
            {500, "{}", 100},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:2\"}],\"next_href\":\"https://api.soundcloud.com/tracks?cursor=next\"}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:3\"}]}"}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.browse("tracks", "old"); sc.browse("tracks", "current");
        QTRY_COMPARE(sc.state()["tracks"].toList().size(), 1);
        QTRY_VERIFY(sc.state()["hasMore"].toBool()); QTest::qWait(120);
        QVERIFY(!sc.state()["busy"].toBool()); QVERIFY(sc.state()["hasMore"].toBool());
        QCOMPARE(sc.state()["tracks"].toList().first().toMap()["urn"].toString(), QString("soundcloud:tracks:2"));
        sc.more(); QTRY_COMPARE(sc.state()["tracks"].toList().size(), 2);
        QVERIFY(!sc.state()["hasMore"].toBool());
        QCOMPARE(sc.state()["tracks"].toList().last().toMap()["urn"].toString(), QString("soundcloud:tracks:3"));
    }
    void refreshesOnceForConcurrentRequests() {
        QFile session(config + "/session.json"); QVERIFY(session.open(QIODevice::WriteOnly));
        session.write("{\"client_id\":\"test-client\",\"access_token\":\"old\",\"refresh_token\":\"refresh-once\",\"expires_at\":0,\"user_session\":false}"); session.close();
        Network net; auto token = guest(); token.delay = 50; net.responses = {token, {200,"[]"}, {200,"[]"}};
        SoundCloud sc(nullptr, &net, secrets); sc.browse("tracks", "first"); sc.browse("tracks", "second");
        QTRY_VERIFY(sc.state()["connected"].toBool()); QTRY_COMPARE(net.requests.size(), 3);
        int exchanges = 0; for (const auto &r : net.requests) if (r.url().host() == "secure.soundcloud.com") ++exchanges;
        QCOMPARE(exchanges, 1); QVERIFY(net.bodies[0].contains("grant_type=refresh_token"));
    }
    void failedRefreshFailsEveryWaitingOperation() {
        QFile session(config + "/session.json"); QVERIFY(session.open(QIODevice::WriteOnly));
        session.write("{\"client_id\":\"test-client\",\"access_token\":\"old\",\"refresh_token\":\"refresh-fails\",\"expires_at\":0,\"user_session\":true}"); session.close();
        Response tokenFailure{500, "{}"}; tokenFailure.delay = 40;
        Network net; net.responses = {tokenFailure};
        SoundCloud sc(nullptr, &net, secrets);
        sc.set("connected", true); sc.set("signedIn", true);
        sc.set("busy", true); sc.set("buffering", true); sc.set("playbackActive", true);
        int firstFailures = 0, secondFailures = 0;
        sc.request("/first", [](QJsonDocument) {}, "GET", {}, true, [&firstFailures](QString) { ++firstFailures; });
        sc.request("/second", [](QJsonDocument) {}, "GET", {}, true, [&secondFailures](QString) { ++secondFailures; });
        sc.listDestinations();
        sc.saveToPlaylist("soundcloud:tracks:1", "", "Тестовый альбом");
        sc.loadHome();

        QTRY_VERIFY(!sc.state()["error"].toString().isEmpty());
        QCOMPARE(firstFailures, 1);
        QCOMPARE(secondFailures, 1);
        QVERIFY(!sc.state()["loadingDestinations"].toBool());
        QVERIFY(!sc.state()["savingTrack"].toBool());
        QVERIFY(!sc.state()["homeBusy"].toBool());
        QVERIFY(sc.state()["busy"].toBool());
        QVERIFY(sc.state()["buffering"].toBool());
        QVERIFY(sc.state()["playbackActive"].toBool());
        QCOMPARE(net.requests.size(), 1);
        QVERIFY(net.requests.first().url().path().endsWith("/oauth/token"));
    }
    void failedRefreshCompletesWaitersForOAuthFailures() {
        QList<Response> failures{{400, "{}"}, {401, "{}"}, {429, "{}"}, {500, "{}"},
            {0, "{}", 0, QNetworkReply::TimeoutError}, {200, "not-json"}};
        for (const auto &failure : failures) {
            secrets->records.clear(); prepare();
            QFile session(config + "/session.json"); QVERIFY(session.open(QIODevice::WriteOnly));
            session.write("{\"client_id\":\"test-client\",\"access_token\":\"old\",\"refresh_token\":\"refresh-fails\",\"expires_at\":0,\"user_session\":true}"); session.close();
            Network net; net.responses = {failure};
            SoundCloud sc(nullptr, &net, secrets);
            int errors = 0;
            sc.request("/waiting", [](QJsonDocument) {}, "GET", {}, true, [&errors](QString message) {
                QVERIFY(!message.isEmpty()); ++errors;
            });
            QTRY_COMPARE(errors, 1);
            QCOMPARE(net.requests.size(), 1);
            if (failure.status == 400 || failure.status == 401) {
                QVERIFY(sc.m_access.isEmpty()); QVERIFY(sc.m_refresh.isEmpty()); QVERIFY(!sc.m_userSession);
                QVERIFY(!secrets->records.contains("session"));
            } else {
                QCOMPARE(sc.m_access, QString("old")); QCOMPARE(sc.m_refresh, QString("refresh-fails"));
                QVERIFY(sc.m_userSession); QVERIFY(secrets->records.contains("session"));
            }
            QTest::qWait(10);
            QCOMPARE(errors, 1);
        }
    }
    void invalidRefreshRemovalFailureQuarantinesSession() {
        AuthStorage storage(secrets);
        const QJsonObject oldSession{{"client_id", "test-client"}, {"access_token", "old-access"},
            {"refresh_token", "refresh-invalid"}, {"expires_at", 0}, {"user_session", true}};
        QVERIFY(storage.save("session", oldSession));
        secrets->removable = false;
        Network net; net.responses = {{401, "{}"}};
        SoundCloud sc(nullptr, &net, secrets);
        int errors = 0;
        QString failureMessage;
        sc.request("/waiting", [](QJsonDocument) {}, "GET", {}, true, [&errors, &failureMessage](QString message) {
            failureMessage = message; ++errors;
        });
        QTRY_COMPARE(errors, 1);
        QVERIFY(sc.m_storageReady); // verified token-free quarantine was stored
        QVERIFY(failureMessage.contains("хранилище"));
        QTest::qWait(10);
        QCOMPARE(errors, 1);
        QVERIFY(sc.m_access.isEmpty()); QVERIFY(sc.m_refresh.isEmpty());
        QCOMPARE(net.requests.size(), 1);
        Network restartNet; SoundCloud restarted(nullptr, &restartNet, secrets);
        QVERIFY(restarted.m_access.isEmpty());
        QVERIFY(restarted.m_refresh.isEmpty());
        QVERIFY(!restarted.signedIn());
        QCOMPARE(restartNet.requests.size(), 0);
    }
    void invalidRefreshQuarantineFailureDisablesStorageUse() {
        AuthStorage storage(secrets);
        QVERIFY(storage.save("session", {{"client_id", "test-client"}, {"access_token", "old-access"},
            {"refresh_token", "refresh-invalid"}, {"expires_at", 0}, {"user_session", true}}));
        Network net; net.responses = {{401, "{}"}};
        SoundCloud sc(nullptr, &net, secrets);
        secrets->removable = false; secrets->writable = false;
        int errors = 0;
        sc.request("/waiting", [](QJsonDocument) {}, "GET", {}, true, [&errors](QString) { ++errors; });
        QTRY_COMPARE(errors, 1);
        QVERIFY(!sc.m_storageReady);
        QVERIFY(sc.m_access.isEmpty()); QVERIFY(sc.m_refresh.isEmpty()); QVERIFY(!sc.signedIn());
        QCOMPARE(net.requests.size(), 1);
    }
    void invalidRefreshInvalidatesLateApiResponse() {
        AuthStorage storage(secrets);
        QVERIFY(storage.save("session", {{"client_id", "test-client"}, {"access_token", "old-access"},
            {"refresh_token", "refresh-invalid"}, {"expires_at", QDateTime::currentSecsSinceEpoch() + 3600}, {"user_session", true}}));
        Response lateMe{200, "{\"username\":\"Stale account\"}"}; lateMe.delay = 100;
        Network net; net.responses = {lateMe, {401, "{}"}};
        SoundCloud sc(nullptr, &net, secrets);
        sc.profile();
        QTRY_COMPARE(net.requests.size(), 1);
        sc.m_expires = 0;
        int errors = 0;
        sc.request("/trigger-refresh", [](QJsonDocument) {}, "GET", {}, true, [&errors](QString) { ++errors; });
        QTRY_COMPARE(errors, 1);
        QTRY_VERIFY(net.requests.size() >= 2);
        QTest::qWait(130);
        QVERIFY(sc.state()["profile"].toMap().isEmpty());
    }
    void logoutCancelsWaitingRefreshCallbacks() {
        QFile session(config + "/session.json"); QVERIFY(session.open(QIODevice::WriteOnly));
        session.write("{\"client_id\":\"test-client\",\"access_token\":\"old\",\"refresh_token\":\"refresh-late\",\"expires_at\":0,\"user_session\":true}"); session.close();
        Response lateToken = guest(); lateToken.delay = 80;
        Network net; net.responses = {lateToken, {500, "{}"}};
        SoundCloud sc(nullptr, &net, secrets); QCoreApplication::processEvents(); sc.set("signedIn", true);
        int continued = 0, failed = 0;
        sc.request("/first", [&continued](QJsonDocument) { ++continued; }, "GET", {}, true, [&failed](QString) { ++failed; });
        sc.request("/second", [&continued](QJsonDocument) { ++continued; }, "GET", {}, true, [&failed](QString) { ++failed; });
        QCOMPARE(net.requests.size(), 1);
        sc.logout();
        QTest::qWait(130);
        QCOMPARE(continued, 0); QCOMPARE(failed, 0);
        QVERIFY(!sc.signedIn()); QVERIFY(sc.m_waiting.isEmpty());
        QCOMPARE(net.requests.size(), 2);
        QCOMPARE(net.requests.last().url().path(), QString("/disconnect"));
    }
    void failedAuthenticationIsVisible() {
        Network net; net.responses = {{401, "{\"error\":\"invalid_client\"}"}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(!sc.state()["error"].toString().isEmpty());
        QVERIFY(!sc.state()["connected"].toBool()); QVERIFY(!sc.state()["busy"].toBool());
        QVERIFY(!sc.state()["error"].toString().contains("test-secret"));
    }
    void rateLimitDoesNotRetryLoop() {
        Network net; net.responses = {guest(), {429,"{}"}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool()); sc.browse("tracks", "test");
        QTRY_VERIFY(!sc.state()["error"].toString().isEmpty()); QCOMPARE(net.requests.size(), 2); QVERIFY(!sc.state()["busy"].toBool());
    }
    void personalPagesRequireUser() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool()); sc.browse("likes");
        QCOMPARE(net.requests.size(), 1); QVERIFY(sc.state()["tracks"].toList().isEmpty());
        sc.like({{"urn","soundcloud:tracks:1"}}); QCOMPARE(net.requests.size(), 1);
    }
    void blockedTrackIsNotStreamed() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool()); sc.play({{"urn","soundcloud:tracks:1"},{"access","blocked"}});
        QCOMPARE(net.requests.size(), 1); QVERIFY(!sc.state()["error"].toString().isEmpty());
        sc.enqueue({{"urn","soundcloud:tracks:2"}}); QCOMPARE(sc.state()["queue"].toList().size(), 2);
        sc.removeQueue(0); QCOMPARE(sc.state()["queue"].toList().size(), 1);
    }
    void pauseAndStopDuringTrackLoading() {
        Network net; net.responses = {guest(), {200, "{}", 150}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.play({{"urn", "soundcloud:tracks:1"}});
        QVERIFY(sc.state()["playbackActive"].toBool());
        QVERIFY(sc.state()["buffering"].toBool());
        sc.pausePlayback(); QVERIFY(!sc.state()["playbackActive"].toBool());
        sc.resumePlayback(); QVERIFY(sc.state()["playbackActive"].toBool());
        sc.stopPlayback();
        QTest::qWait(200); // Поздний ответ не должен оживить остановленную сессию.
        QVERIFY(!sc.state()["playbackActive"].toBool());
        QVERIFY(!sc.state()["buffering"].toBool());
        QVERIFY(sc.state()["current"].toMap().isEmpty());
        QVERIFY(sc.state()["error"].toString().isEmpty());
    }
    void streamFailureEndsPlaybackSession() {
        Network net; net.responses = {guest(), {503, "{}"}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.play({{"urn", "soundcloud:tracks:1"}});
        QVERIFY(sc.state()["playbackActive"].toBool());
        QTRY_VERIFY(!sc.state()["error"].toString().isEmpty());
        QVERIFY(!sc.state()["playbackActive"].toBool());
        QVERIFY(!sc.state()["buffering"].toBool());
    }
    void aac96IsPreferredToLegacyAndStartupLogIsRedacted() {
        Network net;
        net.responses = {{200, "{\"hls_aac_96_url\":\"https://cdn.example.test/96.m3u8?signature=private-signature\",\"hls_mp3_128_url\":\"https://cdn.example.test/mp3.m3u8\"}"}};
        SoundCloud sc(nullptr, &net, secrets);
        sc.m_access = "private-oauth-token"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        CaptureMessages messages;
        // Здесь проверяется выбор URL, а не декодирование вымышленного CDN.
        // Ошибка DNS не должна успеть переключить кандидат во время QTRY_COMPARE.
        const QSignalBlocker playerSignals(sc.m_activePlayer);
        sc.startTrack({{"urn", "soundcloud:tracks:96"}}, false);
        QTRY_COMPARE(sc.m_streamCandidates.size(), 2);
        QCOMPARE(sc.m_streamCandidates.first().first, QString("hls_aac_96_url"));
        QCOMPARE(sc.m_streamCandidateIndex, 0);
        const QString log = messages.messages.join('\n');
        QVERIFY(log.contains("SCripStartup"));
        QVERIFY(log.contains("delta_ms="));
        QVERIFY(log.contains("streams_http_start"));
        QVERIFY(!log.contains("private-oauth-token"));
        QVERIFY(!log.contains("private-signature"));
    }
    void restoredPositionDoesNotCountAsFirstProgress() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        CaptureMessages messages;
        sc.m_startupClock.start(); sc.m_startupPending = true; sc.m_startupAudible = true;
        sc.m_startupInitialPosition = 25000;
        QVERIFY(QMetaObject::invokeMethod(sc.m_activePlayer, "mediaStatusChanged", Qt::DirectConnection,
            Q_ARG(QMediaPlayer::MediaStatus, QMediaPlayer::LoadedMedia)));
        QVERIFY(!messages.messages.join('\n').contains("qt_loaded"));
        QVERIFY(QMetaObject::invokeMethod(sc.m_activePlayer, "positionChanged", Qt::DirectConnection, Q_ARG(qint64, 25000)));
        QVERIFY(!messages.messages.join('\n').contains("first_progress"));
        QVERIFY(QMetaObject::invokeMethod(sc.m_activePlayer, "positionChanged", Qt::DirectConnection, Q_ARG(qint64, 25001)));
        QVERIFY(messages.messages.join('\n').contains("first_progress"));
        QVERIFY(!sc.m_startupPending);
    }
    void invalidAacStreamFallsBackTo96() {
        Network net;
        net.responses = {{200, "{\"hls_aac_160_url\":\"http://invalid.example/160.m3u8\",\"hls_aac_96_url\":\"https://cdn.example.test/96.m3u8\"}"}};
        SoundCloud sc(nullptr, &net, secrets);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.startTrack({{"urn", "soundcloud:tracks:fallback"}}, false);
        QTRY_COMPARE(sc.m_streamCandidateIndex, 1);
        QCOMPARE(sc.m_streamCandidates[1].first, QString("hls_aac_96_url"));
        QCOMPARE(net.requests.size(), 1);
    }
    void previewOnlyIsNotStartedAsFullTrack() {
        Network net;
        net.responses = {{200, "{\"preview_mp3_128_url\":\"https://cdn.example.test/preview.mp3\"}"}};
        SoundCloud sc(nullptr, &net, secrets);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.startTrack({{"urn", "soundcloud:tracks:preview-only"}});
        QTRY_VERIFY(!sc.state()["error"].toString().isEmpty());
        QVERIFY(sc.state()["error"].toString().contains("только короткий фрагмент"));
        QVERIFY(!sc.state()["playbackActive"].toBool());
        QVERIFY(!sc.state()["buffering"].toBool());
        QVERIFY(sc.m_streamCandidates.isEmpty());
        QVERIFY(sc.m_activePlayer->source().isEmpty());
    }
    void qtStartupErrorFallsBackWithoutExposingUrl() {
        Network net;
        SoundCloud sc(nullptr, &net, secrets);
        sc.m_streamCandidates = {{"hls_aac_160_url", QUrl("https://cdn.example.test/160.m3u8?signature=private")},
                                 {"hls_aac_96_url", QUrl("https://cdn.example.test/96.m3u8")}};
        sc.m_streamCandidateIndex = 0; sc.m_startupPending = true; sc.m_startupClock.start();
        CaptureMessages messages;
        sc.m_activePlayer->errorOccurred(QMediaPlayer::FormatError, "private backend error");
        QCOMPARE(sc.m_streamCandidateIndex, 1);
        QVERIFY(!messages.messages.join('\n').contains("private"));
    }
    void preloadedSourceFailureRefreshesStreamsOnlyOnce() {
        Network net; net.responses = {{200, "{\"hls_aac_160_url\":\"http://invalid.example/expired.m3u8\"}"}};
        SoundCloud sc(nullptr, &net, secrets);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_state["current"] = QVariantMap{{"urn", "soundcloud:tracks:expired"}};
        sc.m_state["playbackActive"] = true;
        sc.m_startupClock.start(); sc.m_startupPending = true; sc.m_startupUsedPreload = true;
        sc.m_activePlayer->errorOccurred(QMediaPlayer::NetworkError, "expired signed URL");
        QTRY_COMPARE(sc.m_streamCandidates.size(), 1);
        QCOMPARE(net.requests.size(), 1);
        QVERIFY(sc.m_preloadedRetryUsed);
        QVERIFY(!sc.m_startupUsedPreload);
        sc.m_activePlayer->errorOccurred(QMediaPlayer::NetworkError, "fresh stream failed");
        QCOMPARE(net.requests.size(), 1);
        QVERIFY(!sc.state()["playbackActive"].toBool());
    }
    void lateStreamsResponseCannotReplaceRapidlySelectedTrack() {
        Network net;
        net.responses = {
            {200, "{\"hls_aac_160_url\":\"http://invalid.example/old.m3u8\",\"hls_aac_96_url\":\"http://invalid.example/old-96.m3u8\"}", 150},
            {200, "{\"hls_aac_96_url\":\"http://invalid.example/new-96.m3u8\"}", 1}
        };
        SoundCloud sc(nullptr, &net, secrets);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.play({{"urn", "soundcloud:tracks:old"}});
        sc.play({{"urn", "soundcloud:tracks:new"}});
        QTRY_VERIFY(!sc.m_startupPending);
        QTest::qWait(180); // Поздний ответ старого трека уже должен прийти.
        QCOMPARE(sc.state()["current"].toMap().value("urn").toString(), QString("soundcloud:tracks:new"));
        QCOMPARE(sc.m_streamCandidates.size(), 1);
        QCOMPARE(sc.m_streamCandidates.first().first, QString("hls_aac_96_url"));
        QVERIFY(sc.m_activePlayer->source().isEmpty());
        QCOMPARE(net.requests.size(), 2);
    }
    void streamResolverRateLimitDoesNotTryOtherFormats() {
        Network net;
        net.responses = {{200, "{\"hls_aac_160_url\":\"https://api.soundcloud.com/resolve-160\",\"hls_aac_96_url\":\"https://api.soundcloud.com/resolve-96\"}"},
                         {429, "{}"}};
        SoundCloud sc(nullptr, &net, secrets);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.startTrack({{"urn", "soundcloud:tracks:limited"}}, false);
        QTRY_VERIFY(sc.state()["error"].toString().contains("ограничил"));
        QCOMPARE(net.requests.size(), 2);
        QVERIFY(!sc.state()["buffering"].toBool());
    }
    void streamRateLimitAndTimeoutFinishBuffering() {
        for (const Response failure : {Response{429, "{}"}, Response{0, "", 0, QNetworkReply::TimeoutError}}) {
            Network net; net.responses = {failure};
            SoundCloud sc(nullptr, &net, secrets);
            sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
            sc.startTrack({{"urn", "soundcloud:tracks:failure"}});
            QTRY_VERIFY(!sc.state()["error"].toString().isEmpty());
            QVERIFY(!sc.state()["buffering"].toBool());
            QVERIFY(!sc.state()["playbackActive"].toBool());
            QCOMPARE(net.requests.size(), 1);
            if (failure.status == 429) QVERIFY(sc.state()["error"].toString().contains("ограничил"));
            else QVERIFY(sc.state()["error"].toString().contains("ожидания"));
        }
    }
    void updateCheckDoesNotDownloadOrInstall() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        const auto previousVersion = QCoreApplication::applicationVersion();
        QCoreApplication::setApplicationVersion("1.0.0");
        const QString name = QFileInfo::exists("/etc/arch-release") ? "scrip-999.0-x86_64.pkg.tar.zst" : "SCrip-999.0-amd64.deb";
        QJsonObject asset{{"name", name}, {"digest", "sha256:" + QString(64, '0')}, {"size", 4},
            {"browser_download_url", "https://github.com/sykia/SCrip/releases/download/v999.0/" + name}};
        net.responses = {{200, QJsonDocument(QJsonObject{{"tag_name", "v999.0"}, {"assets", QJsonArray{asset}}}).toJson()}};
        const auto before = net.requests.size(); sc.checkForUpdates(true);
        QTRY_VERIFY(!sc.state()["updateChecking"].toBool());
        QCOMPARE(net.requests.size(), before + 1);
        QVERIFY(sc.state()["updateAvailable"].toBool());
        QVERIFY(!sc.state()["updateDownloading"].toBool());
        QVERIFY(!sc.state()["updateInstalling"].toBool());
        QCoreApplication::setApplicationVersion(previousVersion);
    }
    void updateDownloadRejectsWrongHashAndCanCancel() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        const auto previousVersion = QCoreApplication::applicationVersion(); QCoreApplication::setApplicationVersion("1.0.0");
        const QString name = QFileInfo::exists("/etc/arch-release") ? "scrip-999.0-x86_64.pkg.tar.zst" : "SCrip-999.0-amd64.deb";
        QJsonObject asset{{"name", name}, {"digest", "sha256:" + QString(64, '0')}, {"size", 4},
            {"browser_download_url", "https://github.com/sykia/SCrip/releases/download/v999.0/" + name}};
        net.responses = {{200, QJsonDocument(QJsonObject{{"tag_name", "v999.0"}, {"assets", QJsonArray{asset}}}).toJson()}};
        sc.checkForUpdates(); QTRY_VERIFY(!sc.state()["updateChecking"].toBool());
        net.responses = {{200, "abcd"}}; sc.downloadUpdate();
        QTRY_VERIFY(!sc.state()["updateDownloading"].toBool());
        QVERIFY(sc.state()["updateError"].toString().contains("SHA-256"));
        QVERIFY(!sc.state()["updateReady"].toBool());
        QCoreApplication::setApplicationVersion(previousVersion);
    }
    void updateDownloadCancellationStopsActiveReply() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        const auto previousVersion = QCoreApplication::applicationVersion(); QCoreApplication::setApplicationVersion("1.0.0");
        const QString name = QFileInfo::exists("/etc/arch-release") ? "scrip-999.0-x86_64.pkg.tar.zst" : "SCrip-999.0-amd64.deb";
        const QByteArray payload("abcd");
        QJsonObject asset{{"name", name}, {"digest", "sha256:" + QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())}, {"size", 4},
            {"browser_download_url", "https://github.com/sykia/SCrip/releases/download/v999.0/" + name}};
        net.responses = {{200, QJsonDocument(QJsonObject{{"tag_name", "v999.0"}, {"assets", QJsonArray{asset}}}).toJson()}};
        sc.checkForUpdates(); QTRY_VERIFY(!sc.state()["updateChecking"].toBool());
        net.responses = {{200, payload, 1000}}; sc.downloadUpdate();
        QVERIFY(sc.state()["updateDownloading"].toBool());
        sc.cancelUpdateDownload();
        QVERIFY(!sc.state()["updateDownloading"].toBool()); QVERIFY(!sc.state()["updateReady"].toBool());
        QTest::qWait(20);
        QVERIFY(!sc.state()["updateDownloading"].toBool());
        QCoreApplication::setApplicationVersion(previousVersion);
    }
    void updateRepeatCheckKeepsVerifiedSameRelease() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        const auto previousVersion = QCoreApplication::applicationVersion(); QCoreApplication::setApplicationVersion("1.0.0");
        const QString name = QFileInfo::exists("/etc/arch-release") ? "scrip-999.0-x86_64.pkg.tar.zst" : "SCrip-999.0-amd64.deb";
        const QByteArray payload("abcd");
        const auto digest = "sha256:" + QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
        QJsonObject asset{{"name", name}, {"digest", digest}, {"size", 4}, {"browser_download_url", "https://github.com/sykia/SCrip/releases/download/v999.0/" + name}};
        const QByteArray metadata = QJsonDocument(QJsonObject{{"tag_name", "v999.0"}, {"assets", QJsonArray{asset}}}).toJson();
        net.responses = {{200, metadata}}; sc.checkForUpdates(); QTRY_VERIFY(!sc.state()["updateChecking"].toBool());
        net.responses = {{200, payload}}; sc.downloadUpdate(); QTRY_VERIFY(sc.state()["updateReady"].toBool());
        net.responses = {{200, metadata}}; sc.checkForUpdates(); QTRY_VERIFY(!sc.state()["updateChecking"].toBool());
        QVERIFY(sc.state()["updateReady"].toBool());
        const QString verifiedPath = sc.m_updatePath;
        net.responses = {{200, QJsonDocument(QJsonObject{{"tag_name", "v999.0"}, {"assets", QJsonArray{}}}).toJson()}};
        sc.checkForUpdates(); QTRY_VERIFY(!sc.state()["updateChecking"].toBool());
        QVERIFY(!sc.state()["updateAvailable"].toBool()); QVERIFY(!sc.state()["updateReady"].toBool());
        QVERIFY(sc.state()["updateError"].toString().isEmpty());
        QCOMPARE(sc.state()["updateResult"].toString(), QString("Обновление для этой системы пока недоступно"));
        QVERIFY(QFileInfo::exists(verifiedPath));
        const QString nextName = QFileInfo::exists("/etc/arch-release") ? "scrip-1000.0-x86_64.pkg.tar.zst" : "SCrip-1000.0-amd64.deb";
        const QJsonObject nextAsset{{"name", nextName}, {"digest", "sha256:" + QString(64, '1')}, {"size", 4}, {"browser_download_url", "https://github.com/sykia/SCrip/releases/download/v1000.0/" + nextName}};
        net.responses = {{200, QJsonDocument(QJsonObject{{"tag_name", "v1000.0"}, {"assets", QJsonArray{nextAsset}}}).toJson()}};
        sc.checkForUpdates(); QTRY_VERIFY(!sc.state()["updateChecking"].toBool());
        QVERIFY(!sc.state()["updateReady"].toBool()); QVERIFY(sc.state()["updateAvailable"].toBool());
        QCoreApplication::setApplicationVersion(previousVersion);
    }
    void updateDownloadStreamsChunksAndChecksLengthAndOversizedBody() {
        const auto previousVersion = QCoreApplication::applicationVersion(); QCoreApplication::setApplicationVersion("1.0.0");
        const QString name = QFileInfo::exists("/etc/arch-release") ? "scrip-999.0-x86_64.pkg.tar.zst" : "SCrip-999.0-amd64.deb";
        const QByteArray payload("multi-block-payload");
        const auto digest = "sha256:" + QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
        const QJsonObject asset{{"name", name}, {"digest", digest}, {"size", payload.size()}, {"browser_download_url", "https://github.com/sykia/SCrip/releases/download/v999.0/" + name}};
        const QByteArray metadata = QJsonDocument(QJsonObject{{"tag_name", "v999.0"}, {"assets", QJsonArray{asset}}}).toJson();
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        net.responses = {{200, metadata}}; sc.checkForUpdates(); QTRY_VERIFY(!sc.state()["updateChecking"].toBool());
        net.responses = {{200, payload, 0, QNetworkReply::NoError, 3, payload.size(), true}}; sc.downloadUpdate(); QTRY_VERIFY(sc.state()["updateReady"].toBool());
        QVERIFY(QFileInfo::exists(sc.m_updatePath));
        QCoreApplication::setApplicationVersion(previousVersion);

        // GitHub отвечает 302 с Content-Length: 0, затем отдаёт APK с итоговым размером.
        Network redirectNet; redirectNet.responses = {guest()}; SoundCloud redirected(nullptr, &redirectNet, secrets); QTRY_VERIFY(redirected.state()["connected"].toBool());
        QCoreApplication::setApplicationVersion("1.0.0"); redirectNet.responses = {{200, metadata}}; redirected.checkForUpdates(); QTRY_VERIFY(!redirected.state()["updateChecking"].toBool());
        redirectNet.responses = {{200, payload, 0, QNetworkReply::NoError, 3, payload.size(), false, true}}; redirected.downloadUpdate(); QTRY_VERIFY(redirected.state()["updateReady"].toBool());
        QCoreApplication::setApplicationVersion(previousVersion);

        Network badNet; badNet.responses = {guest()}; SoundCloud bad(nullptr, &badNet, secrets); QTRY_VERIFY(bad.state()["connected"].toBool());
        QCoreApplication::setApplicationVersion("1.0.0"); badNet.responses = {{200, metadata}}; bad.checkForUpdates(); QTRY_VERIFY(!bad.state()["updateChecking"].toBool());
        badNet.responses = {{200, payload + "overflow", 0, QNetworkReply::NoError, 2}}; bad.downloadUpdate(); QTRY_VERIFY(!bad.state()["updateDownloading"].toBool());
        QVERIFY(!bad.state()["updateReady"].toBool()); QCoreApplication::setApplicationVersion(previousVersion);

        Network lengthNet; lengthNet.responses = {guest()}; SoundCloud invalidLength(nullptr, &lengthNet, secrets); QTRY_VERIFY(invalidLength.state()["connected"].toBool());
        QCoreApplication::setApplicationVersion("1.0.0"); lengthNet.responses = {{200, metadata}}; invalidLength.checkForUpdates(); QTRY_VERIFY(!invalidLength.state()["updateChecking"].toBool());
        // Неизвестная длина допустима: итоговый размер и хеш проверяются по данным.
        lengthNet.responses = {{200, payload, 0, QNetworkReply::NoError, 2, -1}}; invalidLength.downloadUpdate(); QTRY_VERIFY(invalidLength.state()["updateReady"].toBool());
        QCOMPARE(lengthNet.requests.last().rawHeader("Accept-Encoding"), QByteArray("identity"));
        for (const qint64 length : {qint64(0), qint64(-3), qint64(payload.size() + 1)}) {
            lengthNet.responses = {{200, payload, 0, QNetworkReply::NoError, 2, length}};
            invalidLength.downloadUpdate(); QTRY_VERIFY(!invalidLength.state()["updateDownloading"].toBool());
            QVERIFY(!invalidLength.state()["updateReady"].toBool());
            QVERIFY(invalidLength.state()["updateError"].toString().contains("размер"));
        }
        QCoreApplication::setApplicationVersion(previousVersion);
    }
    void updateDownloadRejectsHttpFailuresAndPartialBodies() {
        const auto previousVersion = QCoreApplication::applicationVersion(); QCoreApplication::setApplicationVersion("1.0.0");
        const QString name = QFileInfo::exists("/etc/arch-release") ? "scrip-999.0-x86_64.pkg.tar.zst" : "SCrip-999.0-amd64.deb";
        const QByteArray payload("abcd");
        const auto digest = "sha256:" + QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
        QJsonObject asset{{"name", name}, {"digest", digest}, {"size", 4}, {"browser_download_url", "https://github.com/sykia/SCrip/releases/download/v999.0/" + name}};
        const QByteArray metadata = QJsonDocument(QJsonObject{{"tag_name", "v999.0"}, {"assets", QJsonArray{asset}}}).toJson();
        for (const auto &response : QList<Response>{{403, "denied"}, {404, "missing"}, {500, "server error"}, {200, "abc"}, {200, "", 0, QNetworkReply::TimeoutError}}) {
            Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
            QTRY_VERIFY(sc.state()["connected"].toBool()); net.responses = {{200, metadata}};
            sc.checkForUpdates(); QTRY_VERIFY(!sc.state()["updateChecking"].toBool());
            net.responses = {response}; sc.downloadUpdate(); QTRY_VERIFY(!sc.state()["updateDownloading"].toBool());
            QVERIFY(!sc.state()["updateReady"].toBool()); QVERIFY(!sc.state()["updateError"].toString().isEmpty());
        }
        QCoreApplication::setApplicationVersion(previousVersion);
    }
    void updateInstallBlocksCorruptedArtifactAndReentry() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        const QString path = tmp.filePath("corrupt.bin"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("bad"), qint64(3)); file.close();
        sc.m_updatePath = path; sc.m_updateExpectedSize = 3; sc.m_updateDigest = "sha256:" + QString(64, '0');
        sc.m_updateVersion = "2.0"; sc.m_updateReadyVersion = "2.0"; sc.m_updateReadyName = "corrupt.bin"; sc.m_updateReadyDigest = sc.m_updateDigest;
        sc.m_updateAsset = QJsonObject{{"name", "corrupt.bin"}}; sc.m_state["latestVersion"] = "2.0"; sc.m_state["updateReady"] = true;
        sc.installUpdate(); QVERIFY(!sc.state()["updateReady"].toBool()); QVERIFY(sc.state()["updateError"].toString().contains("Контрольная сумма"));
        sc.m_state["updateReady"] = true; sc.m_state["updateInstalling"] = true; sc.installUpdate();
        QVERIFY(sc.state()["updateInstalling"].toBool());
    }
#ifdef Q_OS_LINUX
    void linuxUpdateInstallUsesPkexecSafelyAndReportsResults() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        const QString bin = tmp.filePath("bin"); QVERIFY(QDir().mkpath(bin));
        const QString capture = tmp.filePath("args");
        const QString fakePkexec = bin + "/pkexec";
        QFile script(fakePkexec); QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$CAPTURE_ARGS\"\nexit \"$CAPTURE_EXIT\"\n"); script.close();
        QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const QByteArray oldPath = qgetenv("PATH"); const bool hadPath = qEnvironmentVariableIsSet("PATH");
        qputenv("PATH", (bin + QLatin1Char(':') + QString::fromLocal8Bit(oldPath)).toLocal8Bit());
        qputenv("CAPTURE_ARGS", capture.toLocal8Bit());
        auto prepare = [&](const QString &name) {
            const QString path = tmp.filePath(name); QFile package(path); if (!package.open(QIODevice::WriteOnly)) return false;
            package.write("safe test package"); package.close();
            sc.m_updatePath = path; sc.m_updateExpectedSize = QFileInfo(path).size();
            sc.m_updateDigest = "sha256:" + QString::fromLatin1(QCryptographicHash::hash(QByteArray("safe test package"), QCryptographicHash::Sha256).toHex());
            sc.m_updateVersion = sc.m_updateReadyVersion = "2.0"; sc.m_updateReadyName = name; sc.m_updateReadyDigest = sc.m_updateDigest;
            sc.m_updateAsset = QJsonObject{{"name", name}}; sc.m_state["latestVersion"] = "2.0"; sc.m_state["updateReady"] = true;
            sc.m_state["updateInstalling"] = false; sc.m_state["updateError"] = ""; sc.m_state["updateResult"] = "";
            return true;
        };
        auto run = [&](const QString &name, int exitCode) {
            QFile::remove(capture); qputenv("CAPTURE_EXIT", QByteArray::number(exitCode));
            QVERIFY(prepare(name)); sc.installUpdate();
            QTRY_VERIFY(!sc.state()["updateInstalling"].toBool());
        };
        run("package with spaces.deb", 0);
        QFile debFile(capture); QVERIFY(debFile.open(QIODevice::ReadOnly)); const auto deb = debFile.readAll().split('\n'); debFile.close();
        QCOMPARE(deb, QList<QByteArray>({"apt-get", "install", "-y", "--", QFile::encodeName(tmp.filePath("package with spaces.deb")), ""}));
        QCOMPARE(sc.state()["updateResult"].toString(), QString("Установка завершена"));
        run("scrip-2.0-x86_64.pkg.tar.zst", 0);
        QFile archFile(capture); QVERIFY(archFile.open(QIODevice::ReadOnly)); const auto arch = archFile.readAll().split('\n'); archFile.close();
        QCOMPARE(arch, QList<QByteArray>({"pacman", "--noconfirm", "-U", "--", QFile::encodeName(tmp.filePath("scrip-2.0-x86_64.pkg.tar.zst")), ""}));
        run("package.deb", 126); QCOMPARE(sc.state()["updateResult"].toString(), QString("Установка отменена пользователем."));
        run("package.deb", 1); QVERIFY(!sc.state()["updateError"].toString().isEmpty());
        QVERIFY(prepare("package.txt")); QFile::remove(capture); sc.installUpdate();
        QVERIFY(!sc.state()["updateReady"].toBool()); QVERIFY(!QFileInfo::exists(capture));
        if (hadPath) qputenv("PATH", oldPath); else qunsetenv("PATH");
        qunsetenv("CAPTURE_ARGS"); qunsetenv("CAPTURE_EXIT");
    }
#endif
    void lyricsParsingAndTransport() {
        const auto lines = Lyrics::parseLrc("[ar:Artist]\n[offset:-100]\n[00:02.50][00:04.125]Repeat\n[00:01.1]First\n[00:05.000]\nmalformed");
        QCOMPARE(lines.size(), 4);
        QCOMPARE(lines[0].toMap()["time"].toLongLong(), 1000);
        QCOMPARE(lines[1].toMap()["time"].toLongLong(), 2400);
        QCOMPARE(lines[2].toMap()["time"].toLongLong(), 4025);
        Network net; net.responseHosts = {"lrclib.net"};
        net.responses = {{200, "{\"id\":42,\"trackName\":\"Song\",\"artistName\":\"Artist\",\"syncedLyrics\":\"[00:01.00]First\\n[00:03.00]Second\"}"}};
        Lyrics lyrics(nullptr, &net);
        lyrics.lookup("Artist - Song (Official Audio)", "Repost", 180000);
        QTRY_VERIFY(!lyrics.state()["busy"].toBool());
        QCOMPARE(lyrics.state()["lines"].toList().size(), 2);
        QCOMPARE(QUrlQuery(net.requests.last().url()).queryItemValue("track_name"), QString("Song"));
        QCOMPARE(QUrlQuery(net.requests.last().url()).queryItemValue("artist_name"), QString("Artist"));
        lyrics.lookup("Artist - Song (Official Audio)", "Repost", 180000);
        QTRY_VERIFY(!lyrics.state()["busy"].toBool());
        QCOMPARE(net.requests.size(), 1);
        net.responses = {{200, "{\"plainLyrics\":\"One\\nTwo\",\"id\":2}"}};
        lyrics.lookup("Plain", "Artist", 1000);
        QTRY_VERIFY(!lyrics.state()["busy"].toBool());
        QVERIFY(!lyrics.state()["synced"].toBool());
        QCOMPARE(lyrics.state()["lines"].toList().size(), 2);
        net.responses = {{200, "{\"instrumental\":true,\"id\":3}"}};
        lyrics.lookup("Instrumental", "Artist", 1000);
        QTRY_VERIFY(!lyrics.state()["busy"].toBool());
        QVERIFY(lyrics.state()["instrumental"].toBool());
        net.responses = {{200, "not JSON"}};
        lyrics.search("Broken"); QTRY_VERIFY(!lyrics.state()["busy"].toBool());
        QCOMPARE(lyrics.state()["message"].toString(), QString("Текст не найден"));
        QVERIFY(lyrics.state()["sourceUnavailable"].toBool());
        for (const auto &request : net.requests) QVERIFY(!request.hasRawHeader("Authorization"));
    }
    void lyricsFallbackAndCustomText() {
        Network net; net.responseHosts = {"lrclib.net"};
        const QByteArray record = "{\"id\":9,\"trackName\":\"Rare song\",\"artistName\":\"Artist\",\"duration\":10,\"syncedLyrics\":\"[00:01.00]One\\n[00:03.00]Two\"}";
        net.responses = {{404, "{}"}, {200, "[" + record + "]"}};
        Lyrics lyrics(nullptr, &net);
        lyrics.lookup("Artist - Rare song", "Repost account", 10000, "test:rare-lyrics");
        QTRY_VERIFY(!lyrics.state()["busy"].toBool());
        QVERIFY(lyrics.state()["synced"].toBool());
        QVERIFY(lyrics.state()["results"].toList().isEmpty());
        QVERIFY(lyrics.setCustomText("[00:02.00]Custom\n[00:04.00]Second"));
        QVERIFY(lyrics.state()["custom"].toBool());
        const auto requests = net.requests.size();
        Lyrics reopened(nullptr, &net);
        reopened.lookup("Artist - Rare song", "Repost account", 10000, "test:rare-lyrics");
        QCOMPARE(net.requests.size(), requests);
        QVERIFY(reopened.state()["custom"].toBool());
        QVERIFY(!reopened.setCustomText("  "));
        QVERIFY(reopened.setCustomText("Plain text"));
        QVERIFY(!reopened.state()["synced"].toBool());
        reopened.removeCustomText();
        QTRY_VERIFY(!reopened.state()["busy"].toBool());
        QVERIFY(!reopened.state()["custom"].toBool());
        net.responses = {{200, record, 100}};
        lyrics.lookup("Other", "Artist", 10000);
        QVERIFY(lyrics.setCustomText("[00:00.00]Keep this"));
        QTest::qWait(150);
        QCOMPARE(lyrics.state()["lines"].toList()[0].toMap()["text"].toString(), QString("Keep this"));
    }
    void lyricsMultipleProvidersAndAutomaticMatching() {
        const QByteArray timed = "[00:01.00]First line\n[00:03.00]Second line";
        const QByteArray netSearch = "{\"code\":200,\"result\":{\"songs\":[{\"id\":12,\"name\":\"Rare song\",\"duration\":150468,\"artists\":[{\"name\":\"Archive account\"}]}]}}";
        const QByteArray qqSearch = "{\"code\":0,\"data\":{\"song\":{\"list\":[{\"songmid\":\"mid12\",\"songname\":\"Rare song\",\"interval\":150,\"singer\":[{\"name\":\"Artist\"}]}]}}}";
        const QByteArray ovhSearch = "{\"data\":[{\"id\":12,\"title\":\"Rare song\",\"duration\":150,\"artist\":{\"name\":\"Artist\"}}]}";
        const QByteArray geniusSearch = "{\"response\":{\"sections\":[{\"type\":\"song\",\"hits\":[{\"result\":{\"id\":12,\"title\":\"Rare song\",\"url\":\"https://genius.com/Artist-rare-song-lyrics\",\"primary_artist\":{\"name\":\"Artist\"}}}]}]}}";
        const QByteArray kugouSearch = "{\"candidates\":[{\"id\":\"12\",\"accesskey\":\"public-download-token\",\"song\":\"Rare song\",\"singer\":\"Artist\",\"duration\":150000}]}";
        const QList<QPair<QString, QList<Response>>> fixtures{
            {"music.163.com", {{200, netSearch}, {200, QJsonDocument(QJsonObject{{"lrc", QJsonObject{{"lyric", QString::fromUtf8(timed)}}}}).toJson()}}},
            {"c.y.qq.com", {{200, qqSearch}, {200, "{\"code\":0,\"lyric\":\"[00:01.00]First &amp; line&#10;[00:03.00]Second line\"}"}}},
            {"api.lyrics.ovh", {{200, ovhSearch}, {200, "{\"lyrics\":\"First line\\nSecond line\"}"}}},
            {"genius.com", {{200, geniusSearch}, {200, "<div data-lyrics-container=\"true\"><div data-exclude-from-selection=\"true\"><div>17 Contributors</div><div><span>Translations</span> English</div><h2>Rare song Lyrics</h2></div><div>First &amp; line</div>Second line<br><span data-exclude-from-selection=\"true\">Read more</span>Third line</div>"}}},
            {"lyrics.kugou.com", {{200, kugouSearch}, {200, QJsonDocument(QJsonObject{{"content", QString::fromLatin1(timed.toBase64())}}).toJson()}}}
        };
        const QStringList sources{"NetEase", "QQ Music", "Lyrics.ovh", "Genius", "KuGou"};
        for (int i = 0; i < fixtures.size(); ++i) {
            Network net; net.routeByHost = true;
            net.hostResponses["lrclib.net"] = {{404, "{}"}, {200, "[]"}};
            net.hostResponses[fixtures[i].first] = fixtures[i].second;
            Lyrics lyrics(nullptr, &net);
            lyrics.lookup("Rare song", "Artist", 150000);
            QTRY_VERIFY(!lyrics.state()["busy"].toBool());
            QCOMPARE(lyrics.state()["source"].toString(), sources[i]);
            QVERIFY2(lyrics.state()["lines"].toList().size() >= 2, qPrintable(sources[i]));
            QCOMPARE(lyrics.state()["synced"].toBool(), i == 0 || i == 1 || i == 4);
            QVERIFY(lyrics.state()["results"].toList().isEmpty());
            if (i == 1) QCOMPARE(lyrics.state()["lines"].toList()[0].toMap()["text"].toString(), QString("First & line"));
            if (i == 3) {
                const auto text = lyrics.state()["rawText"].toString();
                QVERIFY(!text.contains("Contributors")); QVERIFY(!text.contains("Translations"));
                QVERIFY(!text.contains("Rare song Lyrics")); QVERIFY(!text.contains("Read more"));
                QVERIFY(text.contains("First & line")); QVERIFY(text.contains("Third line"));
            }
            for (const auto &request : net.requests) QVERIFY(!request.hasRawHeader("Authorization"));
        }
        // Some NetEase entries have plain text in the lrc field; skip an empty duplicate first.
        Network plain; plain.routeByHost = true;
        plain.hostResponses["music.163.com"] = {
            {200, "{\"result\":{\"songs\":[{\"id\":12,\"name\":\"Rare song\",\"duration\":150000,\"artists\":[{\"name\":\"Artist\"}]},{\"id\":13,\"name\":\"Rare song\",\"duration\":150000,\"artists\":[{\"name\":\"Artist\"}]}]}}"},
            {200, "{\"uncollected\":true}"},
            {200, QJsonDocument(QJsonObject{{"lrc", QJsonObject{{"lyric", "First line\nSecond line"}}}}).toJson()}
        };
        Lyrics plainLyrics(nullptr, &plain);
        plainLyrics.lookup("Rare song", "Artist", 150000);
        QTRY_VERIFY(!plainLyrics.state()["busy"].toBool());
        QCOMPARE(plainLyrics.state()["lines"].toList().size(), 2);
        QVERIFY(!plainLyrics.state()["synced"].toBool());
        QCOMPARE(plainLyrics.state()["source"].toString(), QString("NetEase"));
        // Recommendations with unrelated titles, translations and different versions are ignored.
        Network unrelated; unrelated.routeByHost = true;
        unrelated.hostResponses["api.lyrics.ovh"] = {{200, "{\"data\":[{\"id\":1,\"title\":\"GARCON\",\"duration\":150,\"artist\":{\"name\":\"Artist\"}}]}"}};
        unrelated.hostResponses["music.163.com"] = {{200, "{\"result\": {\"songs\": [{\"id\": 1, \"name\": \"Rare song (Remix)\", \"duration\": 150000, \"artists\": [{\"name\": \"Artist\"}]}, {\"id\": 2, \"name\": \"Rare song\", \"duration\": 250000, \"artists\": [{\"name\": \"Artist\"}]}]}}"}};
        Lyrics missing(nullptr, &unrelated);
        missing.lookup("Rare song", "Artist", 150000);
        QTRY_VERIFY(!missing.state()["busy"].toBool());
        QVERIFY(missing.state()["lines"].toList().isEmpty());
        QCOMPARE(missing.state()["message"].toString(), QString("Текст не найден"));
        QCOMPARE(unrelated.requests.size(), 7); // One exact request and six independent searches, no lyric downloads.
        // Per-source rate limits must not block the other providers or leak stale replies.
        Network limited; limited.routeByHost = true;
        limited.hostResponses["lrclib.net"] = {{429, "{}"}};
        limited.hostResponses["music.163.com"] = {{200, netSearch, 100}, {200, netSearch}, {200, QJsonDocument(QJsonObject{{"lrc", QJsonObject{{"lyric", QString::fromUtf8(timed)}}}}).toJson()}};
        Lyrics retry(nullptr, &limited);
        retry.lookup("Rare song", "Artist", 150000);
        QTRY_VERIFY(limited.requests.size() > 1);
        retry.lookup("Rare song", "Artist", 150000);
        QTRY_VERIFY(!retry.state()["busy"].toBool());
        QCOMPARE(retry.state()["source"].toString(), QString("NetEase"));
        QTest::qWait(150);
        QCOMPARE(retry.state()["lines"].toList().size(), 2);
    }
    void equalizerFiltersAndSpectrum() {
        auto tone = [](double frequency, bool opposite = false) {
            std::vector<float> samples(48000 * 2);
            for (int i = 0; i < 48000; ++i) {
                const float sample = 0.1f * std::sin(2 * 3.141592653589793 * frequency * i / 48000);
                samples[i * 2] = sample; samples[i * 2 + 1] = opposite ? -sample : sample;
            }
            return samples;
        };
        auto rms = [](const std::vector<float> &samples, int channel) {
            double energy = 0;
            for (int i = 24000; i < 48000; ++i) energy += std::pow(samples[i * 2 + channel], 2);
            return std::sqrt(energy / 24000);
        };
        AudioDsp dsp;
        std::array<double, 5> gains{};
        dsp.configure(48000, 2, gains);
        auto unity = tone(1000); const auto original = unity;
        dsp.process(unity.data(), 48000);
        QVERIFY(std::abs(rms(unity, 0) - rms(original, 0)) < 1e-6);
        gains[0] = -12;
        dsp.configure(48000, 2, gains); dsp.reset();
        auto low = tone(60); dsp.process(low.data(), 48000);
        QVERIFY(rms(low, 0) < 0.021); // -12 dB at the bass center, compared with 0.071 RMS.
        gains[0] = 12;
        dsp.configure(48000, 2, gains); dsp.reset();
        low = tone(60); dsp.process(low.data(), 48000);
        auto high = tone(4000); dsp.reset(); dsp.process(high.data(), 48000);
        QVERIFY(rms(low, 0) > rms(high, 0) * 3.5);
        auto isolated = tone(60);
        for (int i = 0; i < 48000; ++i) isolated[i * 2 + 1] = 0;
        dsp.reset(); dsp.process(isolated.data(), 48000);
        QCOMPARE(rms(isolated, 1), 0.0);
        const auto bass = tone(60), treble = tone(4000), antiPhase = tone(4000, true);
        const auto bassBins = AudioDsp::spectrum(bass.data(), 2048, 2, 48000);
        const auto trebleBins = AudioDsp::spectrum(treble.data(), 2048, 2, 48000);
        QCOMPARE(bassBins.size(), AudioDsp::spectrumBands);
        auto peak = [](const QVariantList &bins) {
            int result = 0;
            for (int i = 1; i < bins.size(); ++i)
                if (bins[i].toDouble() > bins[result].toDouble()) result = i;
            return result;
        };
        QVERIFY(peak(bassBins) < 20); QVERIFY(peak(trebleBins) > 60);
        QCOMPARE(AudioDsp::spectrum(antiPhase.data(), 2048, 2, 48000), trebleBins);
        std::vector<float> silence(4096, 0);
        for (const auto &value : AudioDsp::spectrum(silence.data(), 2048, 2, 48000)) QCOMPARE(value.toDouble(), 0.0);
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    void equalizerLocalAudioPlayback() {
        if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("No audio output device available");
        Network net; SoundCloud sc(nullptr, &net, secrets);
        const bool saved = sc.equalizerEnabled(); const auto savedGains = sc.equalizerGains();
        QTemporaryDir audioFiles; QVERIFY(audioFiles.isValid());
        QFile wav(audioFiles.filePath("equalizer.wav")); QVERIFY(wav.open(QIODevice::WriteOnly));
        QDataStream stream(&wav); stream.setByteOrder(QDataStream::LittleEndian);
        constexpr int frames = 48000 * 4, dataSize = frames * 2 * 2;
        stream.writeRawData("RIFF", 4); stream << quint32(dataSize + 36);
        stream.writeRawData("WAVEfmt ", 8); stream << quint32(16) << quint16(1) << quint16(2)
            << quint32(48000) << quint32(48000 * 4) << quint16(4) << quint16(16);
        stream.writeRawData("data", 4); stream << quint32(dataSize);
        for (int frame = 0; frame < frames; ++frame) {
            const qint16 sample = qint16(2000 * std::sin(2 * 3.141592653589793 * 1000 * frame / 48000));
            stream << sample << sample;
        }
        wav.close();
        sc.m_audio.setVolume(0); // Exercise the real output without an audible test tone.
        sc.setEqualizerGain(2, -9);
        sc.setEqualizerEnabled(true);
        sc.setTrackVisualActive(true);
        sc.m_player.setSource(QUrl::fromLocalFile(wav.fileName()));
        sc.m_player.play();
        QTRY_VERIFY_WITH_TIMEOUT(!sc.m_playerEffects.processed().isEmpty(), 5000);
        QVERIFY2(sc.equalizerEnabled(), qPrintable(sc.equalizerError()));
        QTRY_VERIFY(([&] { const auto bins = sc.spectrum(); return std::any_of(bins.cbegin(), bins.cend(), [](const QVariant &v) { return v.toDouble() > 0.1; }); })());
        QTest::qWait(1000);
        QVERIFY2(sc.equalizerEnabled(), qPrintable(sc.equalizerError()));
        // Both decoders must be routed while a crossfade is in progress.
        sc.m_crossfadeStarted = true;
        sc.m_sparePlayer.setSource(QUrl::fromLocalFile(wav.fileName()));
        sc.m_sparePlayer.play();
        QTRY_VERIFY(!sc.m_spareEffects.processed().isEmpty());
        sc.m_sparePlayer.stop(); sc.m_crossfadeStarted = false;
        sc.m_player.pause();
        QVERIFY(sc.m_playerEffects.processed().isEmpty());
        sc.seek(2000); sc.m_player.play();
        QTRY_VERIFY(!sc.m_playerEffects.processed().isEmpty());
        sc.m_player.stop();
        QVERIFY(sc.m_playerEffects.processed().isEmpty());
        sc.setTrackVisualActive(false); sc.setEqualizerEnabled(false);
        for (int i = 0; i < 5; ++i) sc.setEqualizerGain(i, savedGains[i].toDouble());
        sc.setEqualizerEnabled(saved);
    }
    void equalizerRoutingAndSettings() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        const bool saved = sc.equalizerEnabled(); const auto gains = sc.equalizerGains();
        sc.setEqualizerEnabled(true);
        QVERIFY(sc.equalizerEnabled());
        QVERIFY(sc.m_audio.isMuted()); QVERIFY(sc.m_preloadAudio.isMuted());
        QCOMPARE(sc.m_player.audioBufferOutput(), &sc.m_waveOutput);
        sc.setEqualizerGain(0, 50); QCOMPARE(sc.equalizerGains()[0].toDouble(), 12.0);
        sc.setTrackVisualActive(true);
        sc.setEqualizerEnabled(false);
        QVERIFY(!sc.m_audio.isMuted()); QVERIFY(sc.m_waveAudioTimer.isActive());
        QVERIFY(sc.m_player.audioBufferOutput());
        sc.setTrackVisualActive(false);
        QVERIFY(!sc.m_waveAudioTimer.isActive()); QVERIFY(!sc.m_player.audioBufferOutput());
        sc.resetEqualizer();
        for (const auto &gain : sc.equalizerGains()) QCOMPARE(gain.toDouble(), 0.0);
        for (int i = 0; i < 5; ++i) sc.setEqualizerGain(i, gains[i].toDouble());
        sc.setEqualizerEnabled(saved);
    }
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    void waveAudioRespondsToBassAndIgnoresDisabledAnalysis() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        QAudioFormat format;
        format.setSampleRate(48000); format.setChannelCount(2); format.setSampleFormat(QAudioFormat::Float);
        auto tone = [&](double frequency, double amplitude) {
            QByteArray bytes(4800 * 2 * int(sizeof(float)), Qt::Uninitialized);
            auto *samples = reinterpret_cast<float *>(bytes.data());
            for (int frame = 0; frame < 4800; ++frame) {
                const float value = float(amplitude * std::sin(2.0 * 3.141592653589793 * frequency * frame / 48000.0));
                samples[frame * 2] = samples[frame * 2 + 1] = value;
            }
            return QAudioBuffer(bytes, format);
        };
        const auto bass = tone(70, 0.6), treble = tone(4000, 0.6), silence = tone(70, 0);
        sc.analyzeWaveAudio(bass);
        QCOMPARE(sc.m_waveTargetLevel, 0.0);
        sc.setWaveVisualActive(true);
        QCOMPARE(sc.m_player.audioBufferOutput(), &sc.m_waveOutput);
        QCOMPARE(sc.m_sparePlayer.audioBufferOutput(), &sc.m_waveSpareOutput);
        sc.analyzeWaveAudio(silence);
        QCOMPARE(sc.m_waveTargetLevel, 0.0);
        sc.analyzeWaveAudio(treble);
        const double trebleBass = sc.m_waveTargetBass;
        QVERIFY(sc.m_waveTargetLevel > 0.5);
        sc.m_waveTargetBass = 0; sc.m_waveLowPass = 0;
        QTest::qWait(190);
        sc.analyzeWaveAudio(bass);
        QVERIFY(sc.m_waveTargetBeat > 0);
        QVERIFY(sc.m_waveTargetBass > trebleBass * 3);
        QVERIFY(sc.m_waveTargetBass <= 1.0);
        QSignalSpy changes(&sc, &SoundCloud::waveAudioChanged);
        QTRY_VERIFY(!changes.isEmpty());
        QVERIFY(sc.waveLevel() > 0);
        sc.setWaveVisualActive(false);
        QCOMPARE(sc.waveLevel(), 0.0); QCOMPARE(sc.waveBass(), 0.0); QCOMPARE(sc.waveBeat(), 0.0);
        QVERIFY(!sc.m_waveAudioTimer.isActive());
        QVERIFY(!sc.m_player.audioBufferOutput());
        QVERIFY(!sc.m_sparePlayer.audioBufferOutput());
    }
#endif
    void wavePanelRendersAcrossSizes() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/WavePanel.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QQuickWindow window;
        QScopedPointer<QObject> object(component.createWithInitialProperties({{"backend", QVariant::fromValue(&sc)}, {"motion", false}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *panel = qobject_cast<QQuickItem *>(object.data()); QVERIFY(panel);
        panel->setParentItem(window.contentItem());
        window.show();
        for (int mode = 0; mode < 2; ++mode) {
            if (mode) {
                auto *selector = object->findChild<QObject *>("waveModeSelector"); QVERIFY(selector);
                QVERIFY(selector->setProperty("currentIndex", 1));
                QVERIFY(object->property("discovery").toBool());
            }
            for (bool dark : {false, true}) {
                object->setProperty("dark", dark);
                for (int width : {320, 720, 1920}) {
                    panel->setWidth(width); panel->setHeight(panel->implicitHeight());
                    window.resize(width, qCeil(panel->height()));
                    QTest::qWait(100);
                    auto grab = panel->grabToImage(QSize(width, qCeil(panel->height())));
                    QVERIFY(grab);
                    QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
                    QVERIFY(ready.wait(3000));
                    QVERIFY(grab->image().save(QString("ui-wave-%1-%2-%3-%4.png")
                        .arg(qEnvironmentVariable("QT_QUICK_BACKEND", "rhi")).arg(mode).arg(width).arg(dark ? "dark" : "light")));
                }
            }
        }
        sc.set("waveMode", "discovery"); sc.set("playbackActive", true);
        object->setProperty("motion", true);
        sc.m_waveLevel = 0.6; sc.m_waveBass = 0.75; sc.m_waveBeat = 1.0;
        emit sc.waveAudioChanged();
        auto *shader = object->findChild<QObject *>("waveShader");
        const bool gpu = shader != nullptr;
        if (gpu) {
            QCOMPARE(shader->property("bass").toDouble(), 0.75);
            QCOMPARE(shader->property("beat").toDouble(), 1.0);
            QVERIFY(shader->property("width").toReal() <= panel->height() * 1.7);
        }
        object->setProperty("foreground", false);
        QTRY_VERIFY(!object->findChild<QObject *>("waveShader"));
        QVERIFY(!sc.m_waveVisualActive);
        QVERIFY(!sc.m_waveAudioTimer.isActive());
        object->setProperty("foreground", true);
        QVERIFY(sc.m_waveVisualActive);
        if (gpu) QTRY_VERIFY(object->findChild<QObject *>("waveShader"));
        object->setProperty("foreground", false);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        panel->setParentItem(nullptr);
        object.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    void wavePanelClickStartsRetriesAndPauses() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false); // Prevent the fixture's deferred guest connection.
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.set("signedIn", true);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/WavePanel.qml"));
        QScopedPointer<QObject> object(component.createWithInitialProperties({{"backend", QVariant::fromValue(&sc)}, {"motion", false}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        QQuickWindow window;
        auto *panel = qobject_cast<QQuickItem *>(object.data()); QVERIFY(panel);
        panel->setParentItem(window.contentItem()); panel->setWidth(320); panel->setHeight(330);
        window.resize(320, 330); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(100);
        auto *control = object->findChild<QQuickItem *>("wavePlayButton"); QVERIFY(control);
        const auto center = control->mapToScene(QPointF(control->width() / 2, control->height() / 2)).toPoint();
        QVERIFY(control->isEnabled());
        QSignalSpy clicked(control, SIGNAL(clicked()));
        net.responses = {{200, R"({"collection":[]})"}};
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_COMPARE(clicked.size(), 1);
        QTRY_COMPARE(sc.m_waveMode, QString("library"));
        QTRY_VERIFY(!sc.state()["waveError"].toString().isEmpty());
        const int requests = net.requests.size();
        net.responses = {{200, R"({"collection":[]})"}};
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_COMPARE(net.requests.size(), requests + 1);
        QTRY_VERIFY(!sc.state()["waveBusy"].toBool());
        sc.set("waveError", ""); sc.set("playbackActive", true);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center);
        QVERIFY(!sc.state()["playbackActive"].toBool());
        QCOMPARE(sc.m_waveMode, QString("library"));
        panel->setParentItem(nullptr);
        object.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void waveSessionRestoresCursorAndIndependentPositions() {
        QSettings settings; settings.remove("playback");
        const QVariantMap profile{{"urn", "soundcloud:users:wave-test"}};
        const QVariantMap first{{"urn", "soundcloud:tracks:101"}, {"access", "blocked"}};
        const QVariantMap second{{"urn", "soundcloud:tracks:102"}, {"access", "blocked"}};
        {
            Network net; SoundCloud sc(nullptr, &net, secrets);
            sc.m_restoreChecked = true; sc.set("signedIn", true); sc.set("profile", profile);
            sc.m_waveMode = "library"; sc.set("waveMode", "library");
            sc.m_waveNext = "/me/likes/tracks?cursor=next";
            sc.m_waveSeen.insert("soundcloud:tracks:101");
            sc.m_queue = {first}; sc.m_queueIndex = 0;
            sc.set("current", first); sc.set("position", 17000); sc.savePlayback();
            sc.set("position", 23000); sc.savePlaybackPosition();
            sc.stopPlayback();
            sc.m_waveMode = "discovery"; sc.set("waveMode", "discovery");
            sc.m_waveSeeds = {second}; sc.m_waveSeedIndex = 3;
            sc.m_waveRelatedNext = "/tracks/102/related?cursor=saved";
            sc.m_queue = {second}; sc.m_queueIndex = 0;
            sc.set("current", second); sc.set("position", 41000); sc.savePlayback();
        }
        {
            Network net; SoundCloud sc(nullptr, &net, secrets);
            sc.set("signedIn", true); sc.set("profile", profile); sc.restorePlayback(profile);
            QCOMPARE(sc.m_waveMode, QString("discovery"));
            QCOMPARE(sc.m_waveSeedIndex, 3);
            QCOMPARE(sc.m_waveRelatedNext, QString("/tracks/102/related?cursor=saved"));
            QCOMPARE(sc.position(), 41000);
            QVERIFY(!sc.state()["playbackActive"].toBool());
            sc.startWave("library");
            QCOMPARE(sc.position(), 23000);
            QCOMPARE(sc.m_waveNext, QString("/me/likes/tracks?cursor=next"));
            QVERIFY(sc.m_waveSeen.contains("soundcloud:tracks:101"));
            sc.startWave("discovery");
            QCOMPARE(sc.position(), 41000);
            QCOMPARE(sc.m_waveSeeds, QVariantList{second});
            QVERIFY(net.requests.isEmpty());
            QVERIFY(sc.clearSession());
            QVERIFY(!settings.contains("playback/waves/library"));
            QVERIFY(!settings.contains("playback/waves/discovery"));
        }
        settings.remove("playback");
    }
    void waveFiltersUnplayableTracksAndRefillsAtEnd() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_waveMode = "library";
        net.responses = {{200, R"({"collection":[{"urn":"soundcloud:tracks:1","access":"blocked"},{"urn":"soundcloud:tracks:2","streamable":false},{"urn":"soundcloud:tracks:3"}]})"}};
        sc.refillWave();
        QTRY_VERIFY(!sc.state()["waveBusy"].toBool());
        QCOMPARE(sc.m_queue.size(), 1);
        QCOMPARE(sc.m_queue[0].toMap()["urn"].toString(), QString("soundcloud:tracks:3"));
        sc.m_queueIndex = 0;
        net.responses = {{200, R"({"collection":[]})"}};
        sc.next();
        QVERIFY(sc.m_waveWaiting);
        QTRY_VERIFY(!sc.state()["waveBusy"].toBool());
        QCOMPARE(sc.m_queueIndex, 0);
        QVERIFY(!sc.state()["waveError"].toString().isEmpty());
    }
    void discoveryFollowsPaginationAndNeverForgetsDuplicateHistory() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_waveMode = "discovery";
        sc.m_waveSeeds = {QVariantMap{{"urn", "soundcloud:tracks:1"}}};
        for (int i = 1; i <= 3501; ++i) sc.m_waveSeen.insert(QString("soundcloud:tracks:%1").arg(i));
        for (int i = 4000; i < 4010; ++i) {
            sc.m_queue.append(QVariantMap{{"urn", QString("soundcloud:tracks:%1").arg(i)}});
            sc.m_waveSeen.insert(QString("soundcloud:tracks:%1").arg(i));
        }
        sc.m_queueIndex = 0;
        net.responses = {
            {200, R"({"collection":[{"urn":"soundcloud:tracks:42"}],"next_href":"/tracks/1/related?cursor=next"})"},
            {200, R"({"collection":[{"urn":"soundcloud:tracks:42"},{"urn":"soundcloud:tracks:9000"},{"urn":"soundcloud:tracks:9000"}]})"}
        };
        sc.refillWave();
        QTRY_COMPARE(sc.m_queue.size(), 11);
        QCOMPARE(net.requests.size(), 2);
        QVERIFY(net.requests[1].url().query().contains("cursor=next"));
        QCOMPARE(sc.m_queue.last().toMap()["urn"].toString(), QString("soundcloud:tracks:9000"));
        QVERIFY(sc.m_waveSeen.contains("soundcloud:tracks:42"));
        QVERIFY(sc.m_waveSeen.size() > 3500);
        QVERIFY(sc.m_waveRelatedNext.isEmpty());
    }
    void discoveryAutomaticallyRetriesAnExhaustedGraphAndContinuesQueue() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_waveMode = "discovery";
        const QVariantMap seed{{"urn", "soundcloud:tracks:1"}};
        const QVariantMap current{{"urn", "soundcloud:tracks:2"}};
        sc.m_waveSeeds = {seed}; sc.m_waveSeen = {"soundcloud:tracks:1", "soundcloud:tracks:2"};
        sc.m_waveNext = "https://api.soundcloud.com/me/likes/tracks?cursor=more";
        sc.m_queue = {current}; sc.m_queueIndex = 0; sc.set("current", current);
        for (int i = 0; i < 6; ++i)
            net.responses.append({200, R"({"collection":[{"urn":"soundcloud:tracks:2"}]})"});
        sc.next();
        QTRY_VERIFY(sc.m_waveRetryTimer.isActive());
        QCOMPARE(net.requests.size(), 6);
        QCOMPARE(sc.m_queue.size(), 1);
        QVERIFY(sc.state()["waveError"].toString().isEmpty());
        QVERIFY(sc.m_waveLoadSeeds);
        QCOMPARE(sc.m_waveRetryTimer.interval(), 3000);
        net.responses = {
            {200, R"({"collection":[{"urn":"soundcloud:tracks:20"}]})"},
            {200, R"({"collection":[{"urn":"soundcloud:tracks:2"},{"urn":"soundcloud:tracks:3"},{"urn":"soundcloud:tracks:4"},{"urn":"soundcloud:tracks:5"},{"urn":"soundcloud:tracks:6"},{"urn":"soundcloud:tracks:7"},{"urn":"soundcloud:tracks:8"}]})"},
            {403, "{}"} // No real stream request leaves the test transport.
        };
        sc.m_waveRetryTimer.start(1);
        QTRY_COMPARE(sc.m_queueIndex, 1);
        QCOMPARE(sc.m_queue.size(), 7);
        QVERIFY(net.requests[6].url().query().contains("cursor=more"));
        QVERIFY(!sc.m_waveWaiting);
        QVERIFY(!sc.m_waveRetryTimer.isActive());
        QSet<QString> ids;
        for (const auto &track : sc.m_queue) ids.insert(SoundCloud::urn(track.toMap()));
        QCOMPARE(ids.size(), sc.m_queue.size());
    }
    void discoveryNetworkRetryBacksOffAndStopsOnPauseOrStop() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_waveMode = "discovery";
        sc.m_waveSeeds = {QVariantMap{{"urn", "soundcloud:tracks:1"}}};
        sc.set("playbackActive", true);
        net.responses = {{503, "{}"}};
        sc.refillWave();
        QTRY_VERIFY(sc.m_waveRetryTimer.isActive());
        QVERIFY(sc.state()["waveError"].toString().isEmpty());
        net.responses = {{503, "{}"}};
        sc.m_waveRetryTimer.start(1);
        QTRY_COMPARE(sc.m_waveRetryTimer.interval(), 6000);
        sc.pausePlayback();
        QVERIFY(!sc.m_waveRetryTimer.isActive());
        QVERIFY(!sc.state()["waveBusy"].toBool());
        QVERIFY(!sc.m_waveWaiting);
        const int requests = net.requests.size();
        QTest::qWait(30);
        QCOMPARE(net.requests.size(), requests);
        sc.set("playbackActive", true); sc.scheduleWaveRefill();
        QVERIFY(sc.m_waveRetryTimer.isActive());
        sc.stopWave();
        QVERIFY(!sc.m_waveRetryTimer.isActive());
    }
    void discoveryLateRefillDoesNotResumeAfterPause() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_waveMode = "discovery";
        sc.m_waveSeeds = {QVariantMap{{"urn", "soundcloud:tracks:1"}}};
        const QVariantMap current{{"urn", "soundcloud:tracks:2"}};
        sc.m_queue = {current}; sc.m_queueIndex = 0; sc.set("current", current);
        net.responses = {{200, R"({"collection":[{"urn":"soundcloud:tracks:3"},{"urn":"soundcloud:tracks:4"},{"urn":"soundcloud:tracks:5"},{"urn":"soundcloud:tracks:6"},{"urn":"soundcloud:tracks:7"},{"urn":"soundcloud:tracks:8"}]})", 30}};
        sc.next(); sc.pausePlayback();
        QTRY_COMPARE(sc.m_queue.size(), 7);
        QCOMPARE(sc.m_queueIndex, 0);
        QCOMPARE(sc.state()["current"].toMap(), current);
        QVERIFY(!sc.state()["playbackActive"].toBool());
        QCOMPARE(net.requests.size(), 1);
    }
    void waveDeduplicatesRelatedAndCancelsLateReplies() {
        Network net;
        SoundCloud sc(nullptr, &net, secrets);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_waveMode = "discovery";
        sc.m_waveSeeds = {QVariantMap{{"urn", "soundcloud:tracks:1"}}};
        sc.m_waveSeen.insert("soundcloud:tracks:1");
        net.responses = {{200, R"({"collection":[{"urn":"soundcloud:tracks:1"},{"urn":"soundcloud:tracks:2"},{"urn":"soundcloud:tracks:2"}]})"}};
        sc.refillWave();
        QTRY_VERIFY(!sc.state()["waveBusy"].toBool());
        QCOMPARE(sc.m_queue.size(), 1);
        QCOMPARE(sc.m_queue.first().toMap()["urn"].toString(), QString("soundcloud:tracks:2"));
        net.responses = {{200, R"({"collection":[{"urn":"soundcloud:tracks:3"}]})", 30}};
        sc.refillWave(); sc.stopWave();
        QTest::qWait(60);
        QCOMPARE(sc.m_queue.size(), 1);
        QVERIFY(sc.state()["waveMode"].toString().isEmpty());
    }
    void waveEmptyLibraryAndNetworkFailureAllowRetry() {
        Network net;
        SoundCloud sc(nullptr, &net, secrets);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.set("signedIn", true);
        net.responses = {{200, R"({"collection":[]})"}};
        sc.startWave("library");
        QTRY_VERIFY(!sc.state()["waveBusy"].toBool());
        QVERIFY(!sc.state()["waveError"].toString().isEmpty());
        QCOMPARE(sc.m_queueIndex, -1);
        net.responses = {{503, "{}"}};
        sc.retryWave();
        QTRY_VERIFY(!sc.state()["waveBusy"].toBool());
        QVERIFY(!sc.state()["waveError"].toString().isEmpty());
        QVERIFY(sc.m_waveMode == "library");
        sc.stopPlayback();
        QVERIFY(sc.m_waveMode.isEmpty());
    }
    void enqueueInsertsAfterCurrentTrack() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        const QVariantMap first{{"urn", "soundcloud:tracks:1"}, {"access", "blocked"}};
        const QVariantMap current{{"urn", "soundcloud:tracks:2"}, {"access", "blocked"}};
        const QVariantMap last{{"urn", "soundcloud:tracks:3"}, {"access", "blocked"}};
        const QVariantMap added{{"urn", "soundcloud:tracks:4"}, {"access", "blocked"}};
        sc.playMix({first, current, last}, 1);
        sc.setShuffle(true);
        sc.enqueue(added);
        QCOMPARE(sc.state()["queue"].toList(), QVariantList({first, current, added, last}));
        QCOMPARE(sc.state()["queueIndex"].toInt(), 1);
        sc.next();
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), QString("soundcloud:tracks:4"));
    }
    void movingQueuePreservesCurrentAndSetsNext() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        QVariantList tracks;
        for (int i = 0; i < 4; ++i)
            tracks.append(QVariantMap{{"urn", QString("soundcloud:tracks:%1").arg(i)}, {"access", "blocked"}});
        sc.playMix(tracks, 1);
        sc.moveQueue(3, 0);
        QCOMPARE(sc.state()["queueIndex"].toInt(), 2);
        QCOMPARE(sc.state()["current"], tracks[1]);
        sc.setShuffle(true);
        sc.queueNext(0);
        QCOMPARE(sc.state()["queueIndex"].toInt(), 1);
        sc.next();
        QCOMPARE(sc.state()["current"], tracks[3]);
    }
    void playMixKeepsSelectionAfterFiltering() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        const QVariantMap invalid{{"urn", "soundcloud:users:bad"}};
        const QVariantMap first{{"urn", "soundcloud:tracks:1"}, {"access", "blocked"}};
        const QVariantMap selected{{"urn", "soundcloud:tracks:2"}, {"access", "blocked"}};
        const QVariantMap last{{"urn", "soundcloud:tracks:3"}, {"access", "blocked"}};

        sc.playMix({invalid, first, selected, last}, 2);
        QCOMPARE(sc.state()["queueIndex"].toInt(), 1);
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), QString("soundcloud:tracks:2"));

        sc.playMix({invalid, first, selected}, 0);
        QCOMPARE(sc.state()["queueIndex"].toInt(), 0);
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), QString("soundcloud:tracks:1"));

        sc.playMix({first, selected, invalid}, 2);
        QCOMPARE(sc.state()["queueIndex"].toInt(), 1);
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), QString("soundcloud:tracks:2"));

        sc.playMix({invalid, first, first, selected}, 2);
        QCOMPARE(sc.state()["queueIndex"].toInt(), 1);
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), QString("soundcloud:tracks:1"));

        const auto oldQueue = sc.state()["queue"];
        const auto oldCurrent = sc.state()["current"];
        sc.playMix({invalid}, 0);
        QCOMPARE(sc.state()["queue"], oldQueue);
        QCOMPARE(sc.state()["current"], oldCurrent);
        sc.playMix({}, 0);
        QCOMPARE(sc.state()["queue"], oldQueue);
        QCOMPARE(sc.state()["current"], oldCurrent);

        sc.playMix({first, selected}, -8);
        QCOMPARE(sc.state()["queueIndex"].toInt(), 0);
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), QString("soundcloud:tracks:1"));
        sc.playMix({first, selected}, 99);
        QCOMPARE(sc.state()["queueIndex"].toInt(), 1);
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), QString("soundcloud:tracks:2"));
    }
    void removingQueueTracksPreservesPlaybackSelection() {
        const QVariantList tracks{
            QVariantMap{{"urn", "soundcloud:tracks:0"}, {"access", "blocked"}},
            QVariantMap{{"urn", "soundcloud:tracks:1"}, {"access", "blocked"}},
            QVariantMap{{"urn", "soundcloud:tracks:2"}, {"access", "blocked"}},
            QVariantMap{{"urn", "soundcloud:tracks:3"}, {"access", "blocked"}},
            QVariantMap{{"urn", "soundcloud:tracks:4"}, {"access", "blocked"}}
        };
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());

        sc.playMix(tracks, 2);
        sc.removeQueue(0); // До текущего: индекс сдвигается, текущий трек сохраняется.
        QCOMPARE(sc.state()["queueIndex"].toInt(), 1);
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), QString("soundcloud:tracks:2"));

        sc.removeQueue(3); // После текущего: воспроизведение не меняется.
        QCOMPARE(sc.state()["queueIndex"].toInt(), 1);
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), QString("soundcloud:tracks:2"));

        sc.removeQueue(1); // Текущий удален: выбирается следующий.
        QCOMPARE(sc.state()["queueIndex"].toInt(), 1);
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), QString("soundcloud:tracks:3"));

        sc.removeQueue(1); // Для последнего трека выбирается предыдущий.
        QCOMPARE(sc.state()["queueIndex"].toInt(), 0);
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), QString("soundcloud:tracks:1"));
    }
    void removingOnlyQueueTrackStopsAndResetsPlayback() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        const QVariantMap track{{"urn", "soundcloud:tracks:only"}, {"access", "blocked"}};
        sc.playMix({track});
        QCOMPARE(sc.state()["current"].toMap()["urn"].toString(), QString("soundcloud:tracks:only"));
        sc.removeQueue(0);
        QVERIFY(sc.state()["queue"].toList().isEmpty());
        QCOMPARE(sc.state()["queueIndex"].toInt(), -1);
        QVERIFY(sc.state()["current"].toMap().isEmpty());
        QVERIFY(!sc.state()["playbackActive"].toBool());
        QVERIFY(!sc.state()["playing"].toBool());
        QVERIFY(!sc.state()["buffering"].toBool());
        QCOMPARE(sc.state()["position"].toLongLong(), qint64(0));
        QCOMPARE(sc.state()["duration"].toLongLong(), qint64(0));
    }
    void refusesRemoteCallback() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool()); sc.configure("id", "secret", "https://evil.example/callback");
        QVERIFY(!sc.state()["error"].toString().isEmpty()); QCOMPARE(net.requests.size(), 1);
    }
    void configureRejectsWhitespaceSecretWithoutFallbackAndPreservesSession() {
        AuthStorage storage(secrets);
        const QJsonObject connection{{"client_id", "test-client"}, {"client_secret", ""},
            {"redirect_uri", QString("http://127.0.0.1:%1/callback").arg(callbackPort)}};
        const QJsonObject session{{"client_id", "test-client"}, {"access_token", "saved-access"},
            {"refresh_token", "saved-refresh"}, {"expires_at", QDateTime::currentSecsSinceEpoch() + 3600}, {"user_session", true}};
        QVERIFY(storage.save("connection", connection));
        QVERIFY(storage.save("session", session));
        const auto savedConnection = secrets->records["connection"];
        const auto savedSession = secrets->records["session"];
        Network net;
        SoundCloud sc(nullptr, &net, secrets);
        QCoreApplication::processEvents();
        QCOMPARE(sc.m_access, QString("saved-access"));
        QVERIFY(!sc.state()["configured"].toBool());
        sc.configure("updated-client", "   ", QString("http://127.0.0.1:%1/callback").arg(callbackPort));
        QVERIFY(!sc.state()["error"].toString().isEmpty());
        QCOMPARE(secrets->records["connection"], savedConnection);
        QCOMPARE(secrets->records["session"], savedSession);
        QCOMPARE(sc.m_access, QString("saved-access"));
        QVERIFY(net.requests.isEmpty());
    }
    void configureWhitespaceSecretKeepsExistingSecret() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets);
        const auto redirect = QString("http://127.0.0.1:%1/callback").arg(callbackPort);
        sc.configure("  updated-client  ", "   ", redirect);
        const auto saved = QJsonDocument::fromJson(secrets->records["connection"]).object();
        QCOMPARE(saved["client_id"].toString(), QString("updated-client"));
        QCOMPARE(saved["client_secret"].toString(), QString("test-secret"));
    }
    void configureTrimsValidCredentials() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets);
        const auto redirect = QString("http://127.0.0.1:%1/callback").arg(callbackPort);
        sc.configure("  updated-client  ", "  updated-secret  ", redirect);
        const auto saved = QJsonDocument::fromJson(secrets->records["connection"]).object();
        QCOMPARE(saved["client_id"].toString(), QString("updated-client"));
        QCOMPARE(saved["client_secret"].toString(), QString("updated-secret"));
    }
    void oauthRejectsWrongStateAndExchangesValidCode_data() {
        QTest::addColumn<bool>("worker");
        QTest::newRow("local") << false;
        QTest::newRow("worker") << true;
    }
    void oauthRejectsWrongStateAndExchangesValidCode() {
        QFETCH(bool, worker);
        if (worker) { qunsetenv("SCRIP_AUTH_MODE"); callbackPort = 43821; }

        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        if (!worker) QTRY_VERIFY(sc.state()["connected"].toBool());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("sc", &sc);
        engine.load(QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *welcome = engine.rootObjects().first()->findChild<QObject *>("welcomeScreen");
        QVERIFY(welcome && welcome->property("visible").toBool());
        connect(&sc, &SoundCloud::authorizationRequested, this, &SoundCloudTest::captureUrl);
        sc.login(); QVERIFY2(sc.state()["authBusy"].toBool(), qPrintable(sc.state()["error"].toString()));
        QVERIFY(authorizationUrl.host() == "secure.soundcloud.com");
        auto query = QUrlQuery(authorizationUrl); QCOMPARE(query.queryItemValue("code_challenge_method"), QString("S256"));
        QVERIFY(query.queryItemValue("code_challenge").size() >= 43);
        QTcpSocket invalid; invalid.connectToHost(QHostAddress::LocalHost, callbackPort); QVERIFY(invalid.waitForConnected());
        invalid.write("GET /callback?code=wrong&state=invalid HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"); invalid.flush();
        QTRY_VERIFY(invalid.bytesAvailable() > 0); QVERIFY(invalid.readAll().startsWith("HTTP/1.1 400"));
        QCOMPARE(net.requests.size(), worker ? 0 : 1); QVERIFY(sc.state()["authBusy"].toBool());
        net.responses = {guest(), {200, "{\"username\":\"Test account\"}"}, {200, "[]"}};
        QTcpSocket valid; valid.connectToHost(QHostAddress::LocalHost, callbackPort); QVERIFY(valid.waitForConnected());
        const QByteArray callback = "GET /callback?code=valid-code&state=" + query.queryItemValue("state").toUtf8() + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
        valid.write(callback); valid.flush(); QTRY_VERIFY(sc.state()["signedIn"].toBool());
        QTRY_COMPARE(sc.state()["profile"].toMap()["username"].toString(), QString("Test account"));
        QTRY_VERIFY(!welcome->property("visible").toBool());
        QTRY_VERIFY(valid.bytesAvailable() > 0);
        QVERIFY(valid.readAll().contains("Аккаунт SoundCloud подключён"));
        const int exchangeIndex = worker ? 0 : 1;
        const QUrlQuery exchange(QString::fromUtf8(net.bodies[exchangeIndex]));
        if (worker) {
            QCOMPARE(net.requests[0].url().host(), QString("scrip-auth.scrip-desktop-aca3d9.workers.dev"));
            QVERIFY(!exchange.hasQueryItem("client_secret"));
            QVERIFY(!net.requests[0].hasRawHeader("Authorization"));
        }
        QCOMPARE(exchange.queryItemValue("grant_type"), QString("authorization_code"));
        QCOMPARE(exchange.queryItemValue("code"), QString("valid-code"));
        QVERIFY(exchange.queryItemValue("code_verifier").size() >= 43);
        QVERIFY(!sc.state()["authBusy"].toBool());
    }
    void failedAccountSwitchPreservesExistingSession_data() {
        QTest::addColumn<int>("status");
        QTest::newRow("http-400") << 400;
        QTest::newRow("http-401") << 401;
    }
    void failedAccountSwitchPreservesExistingSession() {
        QFETCH(int, status);
        AuthStorage storage(secrets);
        const QJsonObject session{{"client_id", "test-client"}, {"access_token", "account-a-access"},
            {"refresh_token", "account-a-refresh"}, {"expires_at", QDateTime::currentSecsSinceEpoch() + 3600}, {"user_session", true}};
        QVERIFY(storage.save("session", session));
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("connected", true); sc.set("signedIn", true);
        connect(&sc, &SoundCloud::authorizationRequested, this, &SoundCloudTest::captureUrl);
        sc.login(); QVERIFY(sc.state()["authBusy"].toBool());
        net.responses = {{status, "{\"error\":\"invalid_grant\"}"}};
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost, callbackPort); QVERIFY(socket.waitForConnected());
        socket.write("GET /callback?code=account-b-code&state=" + QUrlQuery(authorizationUrl).queryItemValue("state").toUtf8()
                     + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"); socket.flush();
        QTRY_VERIFY(socket.bytesAvailable() > 0);
        QVERIFY(socket.readAll().startsWith("HTTP/1.1 400"));
        QCOMPARE(sc.m_access, QString("account-a-access"));
        QCOMPARE(sc.m_refresh, QString("account-a-refresh"));
        QVERIFY(sc.m_userSession); QVERIFY(sc.signedIn());
        QCOMPARE(QJsonDocument::fromJson(secrets->records["session"]).object(), session);
        QVERIFY(!sc.state()["authBusy"].toBool()); QVERIFY(sc.state()["authStep"].toString().isEmpty());
        Network restoreNet; SoundCloud restored(nullptr, &restoreNet, secrets);
        QCOMPARE(restored.m_access, QString("account-a-access"));
        QCOMPARE(restored.m_refresh, QString("account-a-refresh")); QVERIFY(restored.m_userSession);
    }
    void successfulAccountSwitchReplacesCredentialsAndAccountData() {
        AuthStorage storage(secrets);
        const QJsonObject oldSession{{"client_id", "test-client"}, {"access_token", "account-a-access"},
            {"refresh_token", "account-a-refresh"}, {"expires_at", QDateTime::currentSecsSinceEpoch() + 3600}, {"user_session", true}};
        QVERIFY(storage.save("session", oldSession));
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("connected", true); sc.set("signedIn", true);
        sc.set("profile", QVariantMap{{"username", "Account A"}});
        sc.set("tracks", QVariantList{QVariantMap{{"urn", "soundcloud:tracks:account-a"}}});
        sc.set("home", QVariantList{QVariantMap{{"title", "Account A shelf"}}});
        sc.set("destinations", QVariantList{QVariantMap{{"urn", "soundcloud:playlists:account-a"}}});
        sc.set("destinationNext", "https://api.soundcloud.com/me/playlists?cursor=old-account");
        sc.set("hasMore", true);
        sc.m_nextHref = "https://api.soundcloud.com/me/likes/tracks?cursor=old-account";
        sc.set("visitedProfile", QVariantMap{{"urn", "soundcloud:users:public"}, {"username", "Public user"}});
        connect(&sc, &SoundCloud::authorizationRequested, this, &SoundCloudTest::captureUrl);
        sc.login(); QVERIFY(sc.state()["authBusy"].toBool());
        net.responses = {
            {200, "{\"access_token\":\"account-b-access\",\"refresh_token\":\"account-b-refresh\",\"expires_in\":3600}"},
            {200, "{\"username\":\"Account B\"}"}, {200, "[]"}
        };
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost, callbackPort); QVERIFY(socket.waitForConnected());
        socket.write("GET /callback?code=account-b-code&state=" + QUrlQuery(authorizationUrl).queryItemValue("state").toUtf8()
                     + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"); socket.flush();
        QTRY_VERIFY(sc.signedIn());
        QTRY_COMPARE(sc.state()["profile"].toMap()["username"].toString(), QString("Account B"));
        QCOMPARE(sc.m_access, QString("account-b-access")); QCOMPARE(sc.m_refresh, QString("account-b-refresh"));
        QVERIFY(sc.m_userSession); QVERIFY(!sc.state()["authBusy"].toBool());
        QVERIFY(sc.state()["tracks"].toList().isEmpty()); QVERIFY(sc.state()["home"].toList().isEmpty());
        QVERIFY(sc.state()["destinations"].toList().isEmpty());
        QVERIFY(sc.state()["destinationNext"].toString().isEmpty());
        QVERIFY(!sc.state()["hasMore"].toBool());
        QVERIFY(sc.m_nextHref.isEmpty());
        QCOMPARE(sc.state()["visitedProfile"].toMap()["username"].toString(), QString("Public user"));
        const auto saved = QJsonDocument::fromJson(secrets->records["session"]).object();
        QCOMPARE(saved["access_token"].toString(), QString("account-b-access"));
        QCOMPARE(saved["refresh_token"].toString(), QString("account-b-refresh"));
        QVERIFY(saved["user_session"].toBool());
        Network restoreNet; SoundCloud restored(nullptr, &restoreNet, secrets);
        QCOMPARE(restored.m_access, QString("account-b-access"));
        QCOMPARE(restored.m_refresh, QString("account-b-refresh")); QVERIFY(restored.m_userSession);
    }
    void oauthSaveFailurePreservesExistingSession() {
        AuthStorage storage(secrets);
        const QJsonObject oldSession{{"client_id", "test-client"}, {"access_token", "old-access"},
            {"refresh_token", "old-refresh"}, {"expires_at", QDateTime::currentSecsSinceEpoch() + 3600}, {"user_session", true}};
        QVERIFY(storage.save("session", oldSession));
        Network net; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        connect(&sc, &SoundCloud::authorizationRequested, this, &SoundCloudTest::captureUrl);
        sc.login(); QVERIFY(sc.state()["authBusy"].toBool());
        secrets->corruptNextWrite = true;
        net.responses = {guest()};
        const int requestsBeforeCallback = net.requests.size();
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost, callbackPort);
        QVERIFY(socket.waitForConnected());
        socket.write("GET /callback?code=valid-code&state=" + QUrlQuery(authorizationUrl).queryItemValue("state").toUtf8()
                     + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"); socket.flush();
        QTRY_VERIFY(socket.bytesAvailable() > 0);
        QVERIFY(socket.readAll().startsWith("HTTP/1.1 400"));
        QVERIFY(sc.signedIn()); QVERIFY(sc.state()["connected"].toBool());
        QVERIFY(!sc.state()["authBusy"].toBool());
        QVERIFY(sc.state()["authStep"].toString().isEmpty());
        QCOMPARE(sc.state()["error"].toString(), AuthStorage::errorMessage());
        QVERIFY(sc.m_userSession); QVERIFY(sc.m_storageReady);
        QCOMPARE(sc.m_access, QString("old-access")); QCOMPARE(sc.m_refresh, QString("old-refresh"));
        QCOMPARE(QJsonDocument::fromJson(secrets->records["session"]).object(), oldSession);
        QCOMPARE(net.requests.size(), requestsBeforeCallback + 1);
        Network restoreNet; SoundCloud restored(nullptr, &restoreNet, secrets);
        QCOMPARE(restored.m_access, QString("old-access")); QVERIFY(restored.m_userSession);
    }
    void oauthCancellationBeforeCallbackAndDuringExchange() {
        AuthStorage storage(secrets);
        const QJsonObject oldSession{{"client_id", "test-client"}, {"access_token", "old-access"},
            {"refresh_token", "old-refresh"}, {"expires_at", QDateTime::currentSecsSinceEpoch() + 3600}, {"user_session", true}};
        QVERIFY(storage.save("session", oldSession));
        Network net; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        const int initialRequests = net.requests.size();
        connect(&sc, &SoundCloud::authorizationRequested, this, &SoundCloudTest::captureUrl);

        sc.login(); QVERIFY(sc.state()["authBusy"].toBool());
        sc.cancelLogin();
        QTest::qWait(20);
        QVERIFY(!sc.state()["authBusy"].toBool());
        QCOMPARE(net.requests.size(), initialRequests);

        sc.login(); QVERIFY(sc.state()["authBusy"].toBool());
        const QByteArray callback = "GET /callback?code=cancelled&state=" + QUrlQuery(authorizationUrl).queryItemValue("state").toUtf8()
            + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
        net.responses = {{200, "{\"access_token\":\"late-access\",\"refresh_token\":\"late-refresh\",\"expires_in\":3600}", 150}};
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost, callbackPort); QVERIFY(socket.waitForConnected());
        socket.write(callback); socket.flush();
        QTRY_COMPARE(net.requests.size(), initialRequests + 1);
        QCOMPARE(sc.state()["authStep"].toString(), QString("exchange"));
        sc.cancelLogin();
        QTest::qWait(220);
        QVERIFY(sc.signedIn()); QVERIFY(!sc.state()["authBusy"].toBool());
        QVERIFY(sc.m_userSession); QCOMPARE(sc.m_access, QString("old-access"));
        QCOMPARE(QJsonDocument::fromJson(secrets->records["session"]).object(), oldSession);

        net.responses = {{200, "{\"access_token\":\"fresh-access\",\"refresh_token\":\"fresh-refresh\",\"expires_in\":3600}", 1}, {200, "{\"username\":\"Fresh\"}"}, {200, "[]"}};
        sc.login(); QVERIFY(sc.state()["authBusy"].toBool());
        QTcpSocket retry; retry.connectToHost(QHostAddress::LocalHost, callbackPort); QVERIFY(retry.waitForConnected());
        const QByteArray retryCallback = "GET /callback?code=fresh&state=" + QUrlQuery(authorizationUrl).queryItemValue("state").toUtf8()
            + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
        retry.write(retryCallback); retry.flush();
        QTRY_VERIFY(sc.signedIn());
        QVERIFY(sc.m_userSession);
    }
    void windowControls() {
        Network net;
        net.responses = {guest(), {200, "[]"}};
        SoundCloud sc(nullptr, &net, secrets);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("sc", &sc);
        engine.load(QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(window->flags().testFlag(Qt::FramelessWindowHint));
#ifdef Q_OS_WIN
        const bool native = QGuiApplication::platformName() == "windows";
        const auto hwnd = reinterpret_cast<HWND>(window->winId());
        if (native) {
            QVERIFY(IsWindow(hwnd));
            QVERIFY((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) != WS_CAPTION);
        }
#endif
        std::function<QQuickItem *(QQuickItem *, const QString &)> findControl = [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
            if (parent->objectName() == name) return parent;
            for (auto *child : parent->childItems()) if (auto *found = findControl(child, name)) return found;
            return nullptr;
        };
        auto controlCenter = [window, &findControl](const char *name) {
            auto *item = findControl(window->contentItem(), QString::fromLatin1(name));
            return item ? item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint() : QPoint(-1, -1);
        };
        QVERIFY(controlCenter("windowMaximize") != QPoint(-1, -1));
        // Resolve live geometry: the titlebar moves with the sidebar margins.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, controlCenter("windowMaximize"));
        QTRY_COMPARE(window->visibility(), QWindow::Maximized);
#ifdef Q_OS_WIN
        if (native) QTRY_VERIFY(IsZoomed(hwnd));
#endif
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, controlCenter("windowMaximize"));
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, controlCenter("windowMinimize"));
        QTRY_COMPARE(window->visibility(), QWindow::Minimized);
#ifdef Q_OS_WIN
        if (native) QTRY_VERIFY(IsIconic(hwnd));
#endif
        window->showNormal();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, controlCenter("windowClose"));
        QTRY_VERIFY(!window->isVisible());
    }
    void queueHandleReordersTracks() {
        QFile session(config + "/session.json"); QVERIFY(session.open(QIODevice::WriteOnly));
        session.write(QJsonDocument(QJsonObject{{"client_id", "test-client"}, {"access_token", "test-access"}, {"refresh_token", "test-refresh"}, {"expires_at", QDateTime::currentSecsSinceEpoch() + 3600}, {"user_session", true}}).toJson()); session.close();
        Network net; net.responses = {{200, "{\"username\":\"Тестовый аккаунт\"}"}, {200, "[]"}};
        SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["signedIn"].toBool());
        for (int i = 0; i < 3; ++i)
            sc.enqueue({{"urn", QString("soundcloud:tracks:%1").arg(i + 1)}, {"title", QString("Трек %1").arg(i + 1)}});
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("sc", &sc);
        engine.load(QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->setWidth(1320); window->setHeight(820);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        std::function<QQuickItem *(QQuickItem *, const QString &)> findItem = [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
            if (parent->objectName() == name) return parent;
            for (auto *child : parent->childItems()) if (auto *found = findItem(child, name)) return found;
            return nullptr;
        };
        QTRY_VERIFY(findItem(window->contentItem(), "queueHandle0"));
        auto *first = findItem(window->contentItem(), "queueHandle0");
        auto *third = findItem(window->contentItem(), "queueHandle2"); QVERIFY(third);
        const auto start = first->mapToScene(QPointF(first->width() / 2, first->height() / 2)).toPoint();
        const auto end = third->mapToScene(QPointF(third->width() / 2, third->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        auto *preview = findItem(window->contentItem(), "queueDragPreview"); QVERIFY(preview);
        QVERIFY(preview->isVisible());
        const auto initialY = preview->y();
        QTest::mouseMove(window, end, 50);
        QTRY_VERIFY(preview->y() > initialY);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
        QTRY_COMPARE(sc.state()["queue"].toList().last().toMap()["urn"].toString(), QString("soundcloud:tracks:1"));
    }
    void qmlScreensRender() {
        QFile session(config + "/session.json"); QVERIFY(session.open(QIODevice::WriteOnly));
        session.write(QJsonDocument(QJsonObject{{"client_id", "test-client"}, {"access_token", "test-access"}, {"refresh_token", "test-refresh"}, {"expires_at", QDateTime::currentSecsSinceEpoch()+3600}, {"user_session", true}}).toJson()); session.close();
        Network net; net.responses = {{200, "{\"username\":\"Тестовый аккаунт\"}"}, {200, "[]"}}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        QTRY_VERIFY(!sc.state()["busy"].toBool());
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("sc", &sc);
        Network lyricsNet; lyricsNet.responseHosts = {"lrclib.net"};
        const QByteArray lyricFixture = R"({"id":42,"trackName":"A track page","artistName":"Test artist","syncedLyrics":"[00:00.00]Every sound has a story\n[00:10.00]Every word finds its way\n[00:20.00]Stay a little longer\n[00:30.00]Let the music play"})";
        lyricsNet.responses = {{200, lyricFixture}, {200, lyricFixture}, {200, lyricFixture}};
        Lyrics lyrics(nullptr, &lyricsNet);
        engine.rootContext()->setContextProperty("lyricsBackend", &lyrics);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError> &errors) { for (const auto &error : errors) warnings.append(error.toString()); });
        net.responses = {{200, R"([{"urn":"soundcloud:tracks:10","title":"Релиз"}])"}, {200, "[]"}, {200, R"([{"urn":"soundcloud:tracks:11","title":"Основа"}])"}, {200, R"([{"urn":"soundcloud:tracks:12","title":"Похожий"}])"}};
        engine.load(QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty()); auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QCOMPARE(window->property("api").toMap(), sc.state());
        QTRY_COMPARE(sc.state()["home"].toList().size(), 4);
        QTRY_VERIFY(!sc.state()["homeBusy"].toBool());
        QCOMPARE(window->property("section").toInt(), 5);
        window->setProperty("section", 0);
        window->setWidth(1320); window->setHeight(820);
        auto *preferences = window->findChild<QObject *>("uiPreferences"); QVERIFY(preferences);
        preferences->setProperty("gridView", false);
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\",\"title\":\"Тестовый трек с длинным названием для проверки вёрстки\",\"duration\":185000,\"user\":{\"username\":\"Исполнитель\"}},{\"urn\":\"soundcloud:tracks:2\",\"title\":\"Второй трек\",\"duration\":220000,\"access\":\"preview\"}]}"}};
        sc.browse("tracks", "Тест интерфейса"); QTRY_COMPARE(sc.state()["tracks"].toList().size(), 2);
        QCOMPARE(window->property("api").toMap()["tracks"].toList(), sc.state()["tracks"].toList());
        QTest::qWait(450); QVERIFY(window->grabWindow().save("ui-catalog.png"));
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\",\"title\":\"First track\",\"duration\":185000,\"user\":{\"username\":\"Artist\"}},{\"urn\":\"soundcloud:tracks:2\",\"title\":\"Second track\",\"duration\":220000}]}"}};
        QQmlExpression openPlaylist(QQmlEngine::contextForObject(window), window,
            "playlistView.begin({urn: 'soundcloud:playlists:42', title: 'Evening collection', track_count: 2, user: {username: 'SCrip'}})");
        openPlaylist.evaluate();
        QVERIFY2(!openPlaylist.hasError(), qPrintable(openPlaylist.error().toString()));
        QTRY_VERIFY(!sc.state()["playlistBusy"].toBool());
        QQmlExpression previewObject(QQmlEngine::contextForObject(window), window, "playlistView");
        auto *preview = previewObject.evaluate().value<QObject *>();
        QVERIFY(preview);
        QTest::qWait(45);
        const auto openingOpacity = preview->property("opacity").toReal();
        QVERIFY(openingOpacity > 0 && openingOpacity < 1);
        QVERIFY(QMetaObject::invokeMethod(preview, "close"));
        QVERIFY(qAbs(preview->property("opacity").toReal() - openingOpacity) < 0.1);
        QTest::qWait(30);
        const auto closingOpacity = preview->property("opacity").toReal();
        QVERIFY(QMetaObject::invokeMethod(preview, "open"));
        QVERIFY(qAbs(preview->property("opacity").toReal() - closingOpacity) < 0.1);
        QTRY_VERIFY(preview->property("opened").toBool());
        QTRY_COMPARE(preview->property("opacity").toReal(), 1.0);
        QTRY_COMPARE(preview->property("reveal").toReal(), 1.0);
        QVERIFY(window->grabWindow().save("ui-playlist.png"));
        std::function<QQuickItem *(QQuickItem *)> findTrackActions = [&](QQuickItem *item) -> QQuickItem * {
            if (item->objectName() == "previewTrackActions0") return item;
            for (auto *child : item->childItems()) if (auto *found = findTrackActions(child)) return found;
            return nullptr;
        };
        QTRY_VERIFY(findTrackActions(window->contentItem()));
        auto *actionsButton = findTrackActions(window->contentItem());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            actionsButton->mapToScene(QPointF(actionsButton->width() / 2, actionsButton->height() / 2)).toPoint());
        QQmlExpression actionMenuObject(QQmlEngine::contextForObject(window), window, "trackActions");
        auto *actionMenu = actionMenuObject.evaluate().value<QObject *>();
        QVERIFY(actionMenu);
        QTRY_VERIFY(actionMenu->property("opened").toBool());
        QVERIFY(window->grabWindow().save("ui-track-actions.png"));
        QVERIFY(QMetaObject::invokeMethod(actionMenu, "close"));
        QTRY_VERIFY(!actionMenu->property("visible").toBool());
        QQmlExpression closePlaylist(QQmlEngine::contextForObject(window), window, "playlistView.close()");
        closePlaylist.evaluate();
        QTRY_VERIFY(!preview->property("visible").toBool());

        QVERIFY(window->property("dockedInspector").toBool());
        QJsonArray scrollTracks;
        for (int i=0; i<32; ++i) scrollTracks.append(QJsonObject{{"urn", QString("soundcloud:tracks:%1").arg(i+10)}, {"title", "Проверка прокрутки"}});
        net.responses = {{200, QJsonDocument(QJsonObject{{"collection", scrollTracks}, {"next_href","https://api.soundcloud.com/tracks?cursor=next"}}).toJson()}};
        sc.browse("tracks", "scroll"); QTRY_COMPARE(sc.state()["tracks"].toList().size(),32);
        QTest::qWait(100);
        auto *scroll = window->findChild<QObject *>("libraryScroll"); QVERIFY(scroll);
        QWheelEvent wheel(QPointF(600,100),QPointF(600,100),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(window,&wheel);
        QTRY_VERIFY(scroll->property("contentY").toReal()>0);
        QVERIFY(scroll->property("contentY").toReal()<=scroll->property("contentHeight").toReal()-scroll->property("height").toReal());
        net.responses = {{200, "[]"}};
        net.responses = {{200, R"([{"urn":"soundcloud:tracks:99","title":"Следующая страница"}])"}};
        QTest::qWait(550); // Дождаться завершения анимации колеса перед переходом в конец.
        scroll->setProperty("contentY", scroll->property("contentHeight").toReal() - scroll->property("height").toReal());
        QTRY_COMPARE(sc.state()["tracks"].toList().size(), 33);
        net.responses = {{200, "[]"}};
        preferences->setProperty("gridView", false);
        preferences->setProperty("autoLoad", true);
        auto *trackView = window->findChild<QObject *>("trackListView"); QVERIFY(trackView);
        auto tracksJson = [](int count, bool more = false, int offset = 0) {
            QJsonArray items;
            for (int i = 0; i < count; ++i)
                items.append(QJsonObject{{"urn", QString("soundcloud:tracks:virtual-%1").arg(offset + i)},
                                         {"title", QString("Виртуальный трек %1").arg(offset + i)}, {"duration", 180000}});
            QJsonObject page{{"collection", items}};
            if (more) page["next_href"] = "https://api.soundcloud.com/tracks?cursor=virtual-next";
            return QJsonDocument(page).toJson();
        };
        const QList<int> listSizes{0, 1, 100, 2000, 5000};
        for (int size : listSizes) {
            net.responses = {{200, tracksJson(size, size == 5000)}};
            sc.browse("tracks", QString("virtual-%1").arg(size));
            QTRY_COMPARE(sc.state()["tracks"].toList().size(), size);
            QTRY_VERIFY(!sc.state()["busy"].toBool());
            QTRY_VERIFY(trackView->property("activeDelegateCount").toInt() <= 40);
            if (size == 0) QCOMPARE(trackView->property("activeDelegateCount").toInt(), 0);
            if (size == 1) QCOMPARE(trackView->property("height").toReal(), 58.0);
            if (size == 5000) {
                QCOMPARE(trackView->property("count").toInt(), 5000);
                net.responses = {{200, tracksJson(1, false, 5000)}};
                const qreal end = trackView->property("contentHeight").toReal() - trackView->property("height").toReal();
                trackView->setProperty("contentY", end);
                QTRY_VERIFY(trackView->property("contentY").toReal() >= end - 1);
                QTRY_VERIFY(trackView->property("activeDelegateCount").toInt() <= 40);
                QVERIFY(trackView->property("activeDelegateCount").toInt() > 0);
                QVariant lastTitle;
                QVERIFY(QMetaObject::invokeMethod(trackView, "lastTrackTitle", Q_RETURN_ARG(QVariant, lastTitle)));
                QCOMPARE(lastTitle.toString(), QString("Виртуальный трек 4999"));
                QTRY_COMPARE(sc.state()["tracks"].toList().size(), 5001);
                QTRY_VERIFY(!sc.state()["hasMore"].toBool());
                const qreal appendedEnd = trackView->property("contentHeight").toReal() - trackView->property("height").toReal();
                trackView->setProperty("contentY", appendedEnd);
                QTRY_VERIFY(QMetaObject::invokeMethod(trackView, "lastTrackTitle", Q_RETURN_ARG(QVariant, lastTitle)));
                QTRY_COMPARE(lastTitle.toString(), QString("Виртуальный трек 5000"));
            }
        }
        preferences->setProperty("gridView", true);
        auto *gridView = window->findChild<QObject *>("coverGridView"); QVERIFY(gridView);
        auto gridTracksJson = [](int count, bool more = false, int offset = 0) {
            QJsonArray items;
            for (int i = 0; i < count; ++i)
                items.append(QJsonObject{{"urn", QString("soundcloud:tracks:grid-%1").arg(offset + i)},
                                         {"title", QString("Карточка %1").arg(offset + i)}, {"access", "blocked"},
                                         {"user", QJsonObject{{"username", "Исполнитель"}, {"urn", "soundcloud:users:1"}}}});
            QJsonObject page{{"collection", items}};
            if (more) page["next_href"] = "https://api.soundcloud.com/tracks?cursor=grid-next";
            return QJsonDocument(page).toJson();
        };
        for (int size : listSizes) {
            net.responses = {{200, gridTracksJson(size, size == 5000)}};
            sc.browse("tracks", QString("grid-%1").arg(size));
            QTRY_COMPARE(sc.state()["tracks"].toList().size(), size);
            QTRY_VERIFY(!sc.state()["busy"].toBool());
            QTRY_VERIFY(gridView->property("activeDelegateCount").toInt() <= 40);
            if (size == 0) QCOMPARE(gridView->property("activeDelegateCount").toInt(), 0);
            if (size == 1) QCOMPARE(gridView->property("height").toReal(), gridView->property("tileHeight").toReal());
            if (size == 5000) {
                QCOMPARE(gridView->property("count").toInt(), 5000);
                QVariant title;
                QTRY_VERIFY(([&] {
                    return QMetaObject::invokeMethod(gridView, "trackTitleAt", Q_RETURN_ARG(QVariant, title), Q_ARG(QVariant, 0))
                        && title.toString() == QString("Карточка 0");
                })());
                const int wideColumns = gridView->property("columns").toInt();
                window->setWidth(760); QCoreApplication::processEvents();
                QVERIFY(gridView->property("columns").toInt() < wideColumns);
                window->setWidth(1320); QCoreApplication::processEvents();
                net.responses = {{200, gridTracksJson(1, false, 5000)}};
                auto *pageScroll = window->findChild<QObject *>("libraryScroll"); QVERIFY(pageScroll);
                pageScroll->setProperty("contentY", 0);
                QQuickItem *gridItem = qobject_cast<QQuickItem *>(gridView); QVERIFY(gridItem);
                const QPointF gridPoint = gridItem->mapToScene(QPointF(gridItem->width() / 2, gridItem->height() / 2));
                QTest::mouseMove(window, gridPoint.toPoint());
                QWheelEvent gridWheel(gridPoint, gridPoint, QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(window, &gridWheel);
                QTRY_VERIFY(gridView->property("contentY").toReal() > 0);
                const qreal end = gridView->property("contentHeight").toReal() - gridView->property("height").toReal();
                gridView->setProperty("contentY", end);
                QTRY_VERIFY(gridView->property("contentY").toReal() >= end - 1);
                QTRY_VERIFY(gridView->property("activeDelegateCount").toInt() > 0);
                QVERIFY(gridView->property("activeDelegateCount").toInt() <= 40);
                QVERIFY(QMetaObject::invokeMethod(gridView, "trackTitleAt", Q_RETURN_ARG(QVariant, title), Q_ARG(QVariant, 4999)));
                QCOMPARE(title.toString(), QString("Карточка 4999"));
                QQuickItem *lastTile = nullptr;
                QVERIFY(QMetaObject::invokeMethod(gridView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, lastTile), Q_ARG(int, 4999)));
                QVERIFY(lastTile);
                QVERIFY(QMetaObject::invokeMethod(lastTile, "playTrack"));
                QTRY_COMPARE(sc.state()["current"].toMap().value("urn").toString(), QString("soundcloud:tracks:grid-4999"));
                QVERIFY(QMetaObject::invokeMethod(lastTile, "enqueueTrack"));
                QTRY_COMPARE(sc.state()["queue"].toList().last().toMap().value("urn").toString(), QString("soundcloud:tracks:grid-4999"));
                QTRY_COMPARE(sc.state()["tracks"].toList().size(), 5001);
                QTRY_VERIFY(!sc.state()["hasMore"].toBool());
                const qreal appendedEnd = gridView->property("contentHeight").toReal() - gridView->property("height").toReal();
                gridView->setProperty("contentY", appendedEnd);
                // Qt создаёт делегат после обновления модели и следующего кадра.
                QTRY_VERIFY(QMetaObject::invokeMethod(gridView, "trackTitleAt", Q_RETURN_ARG(QVariant, title), Q_ARG(QVariant, 5000))
                            && title.toString() == QString("Карточка 5000"));
            }
        }
        preferences->setProperty("gridView", false); QCoreApplication::processEvents(); QTest::qWait(120);
        QVERIFY(window->findChild<QObject *>("trackListView"));
        preferences->setProperty("gridView", true); QCoreApplication::processEvents(); QTest::qWait(120);
        QVERIFY(window->findChild<QObject *>("coverGridView"));
        QTRY_VERIFY(!sc.state()["busy"].toBool());
        QTest::qWait(250);
        window->setProperty("section", 4); QTest::qWait(250);
        net.responses = {{200, gridTracksJson(3)}};
        window->setProperty("query", "virtual-return");
        window->setProperty("section", 0);
        QTRY_VERIFY(!sc.state()["busy"].toBool());
        QTRY_COMPARE(sc.state()["tracks"].toList().size(), 3);
        QVERIFY(window->findChild<QObject *>("coverGridView"));
        net.responses = {{200, "[]"}};
        window->setProperty("section", 3); QTest::qWait(400);
        QVERIFY(window->grabWindow().save("ui-profile.png"));
        window->setProperty("section", 4); window->setProperty("settingGroup", 3); QTest::qWait(100);
        QVERIFY(window->grabWindow().save("ui-connection.png"));
        window->setWidth(760); window->setHeight(560); QTest::qWait(350);
        QVERIFY(!window->property("dockedInspector").toBool());
        QVERIFY(window->grabWindow().save("ui-narrow.png"));
        auto *dock = window->findChild<QQuickItem *>("playbackDock"); QVERIFY(dock);
        auto *barrier = window->findChild<QQuickItem *>("playbackDockBarrier"); QVERIFY(barrier);
        QQmlComponent clickProbe(&engine);
        clickProbe.setData("import QtQuick; MouseArea { property int clicks: 0; z: 1; onClicked: clicks++ }", QUrl());
        QScopedPointer<QObject> probeObject(clickProbe.create());
        auto *probe = qobject_cast<QQuickItem *>(probeObject.data()); QVERIFY(probe);
        probe->setParentItem(dock->parentItem());
        probe->setX(dock->x()); probe->setY(dock->y());
        probe->setWidth(dock->width()); probe->setHeight(dock->height());
        const auto dockPoint = dock->mapToScene(QPointF(dock->width() / 2, 3)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, dockPoint);
        QCOMPARE(probe->property("clicks").toInt(), 0);
        barrier->setEnabled(false);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, dockPoint);
        QCOMPARE(probe->property("clicks").toInt(), 1);
        barrier->setEnabled(true);
        probe->setParentItem(nullptr);

        const auto currentBeforePage = sc.state()["current"].toMap();
        const QVariant pageTrack = QVariantMap{{"urn", "soundcloud:tracks:details"}, {"title", "коза (p. youngslashprod)"},
            {"duration", 185000}, {"description", "Track description with enough detail to wrap across lines."},
            {"user", QVariantMap{{"username", "Test artist"}, {"urn", "soundcloud:users:details"}}}};
        QVERIFY(QMetaObject::invokeMethod(window, "openTrack", Q_ARG(QVariant, pageTrack), Q_ARG(QVariant, false)));
        auto *detailsPage = window->findChild<QObject *>("trackPage"); QVERIFY(detailsPage);
        QTRY_VERIFY(detailsPage->property("opened").toBool());
        QCOMPARE(detailsPage->property("track").toMap()["title"].toString(), QString("коза (p. youngslashprod)"));
        QCOMPARE(sc.state()["current"].toMap(), currentBeforePage);
        QTRY_COMPARE(lyrics.state()["lines"].toList().size(), 4);
        QVERIFY(!window->findChild<QObject *>("equalizerToggle"));
        auto *bars = window->findChild<QObject *>("spectrumBars"); QVERIFY(bars);
        QVERIFY(QMetaObject::invokeMethod(bars, "clicked"));
        QCOMPARE(preferences->property("spectrumStyle").toInt(), 1);
        auto *solid = window->findChild<QObject *>("spectrumSolid"); QVERIFY(solid);
        QVERIFY(QMetaObject::invokeMethod(solid, "clicked"));
        QCOMPARE(preferences->property("spectrumStyle").toInt(), 0);
        QTest::qWait(150);
        QVERIFY(window->grabWindow().save("ui-track-narrow.png"));
        window->setWidth(1320); window->setHeight(820);
        QCoreApplication::processEvents();
        auto *centerArea = window->findChild<QQuickItem *>("mainContentArea"); QVERIFY(centerArea);
        auto *detailsContent = detailsPage->property("contentItem").value<QQuickItem *>(); QVERIFY(detailsContent);
        const auto pageStart = detailsContent->mapToScene(QPointF(0, 0));
        QVERIFY(pageStart.x() >= centerArea->x());
        QVERIFY(pageStart.x() + detailsContent->width() <= centerArea->x() + centerArea->width());
        QVERIFY(pageStart.y() + detailsContent->height() <= dock->mapToScene(QPointF(0, 0)).y());
        QVERIFY(!detailsPage->property("modal").toBool());
        auto *trackColumn = window->findChild<QQuickItem *>("trackMainColumn"); QVERIFY(trackColumn);
        auto *lyricsColumn = window->findChild<QQuickItem *>("trackLyricsPane"); QVERIFY(lyricsColumn);
        QVERIFY(lyricsColumn->x() > trackColumn->x() + trackColumn->width());
        QVERIFY(std::abs(trackColumn->width() / (trackColumn->width() + lyricsColumn->width()) - 0.66) < 0.01);
        for (bool dark : {false, true}) {
            preferences->setProperty("darkMode", dark); QTest::qWait(150);
            QVERIFY(window->grabWindow().save(dark ? "ui-track-dark.png" : "ui-track-light.png"));
        }
        auto *pageTitle = window->findChild<QQuickItem *>("trackDetailsTitle"); QVERIFY(pageTitle);
        QVERIFY(pageTitle->property("contentWidth").toDouble() <= pageTitle->width() + 1);
        QVERIFY(!detailsPage->property("showDescription").toBool());
        window->setHeight(1080);
        lyricsNet.responses.prepend({404, "{}"});
        lyrics.lookup("Missing lyrics fixture", "Fixture artist", 1000);
        QTRY_VERIFY(!lyrics.state()["busy"].toBool());
        QVERIFY(lyrics.state()["lines"].toList().isEmpty());
        QVERIFY(!window->findChild<QObject *>("lyricsSearch"));
        for (bool dark : {false, true}) {
            preferences->setProperty("darkMode", dark); QTest::qWait(150);
            QVERIFY(window->grabWindow().save(dark ? "ui-track-empty-dark.png" : "ui-track-empty-light.png"));
        }
        lyrics.lookup(pageTrack.toMap()["title"].toString(), "Test artist", 185000);
        QTRY_COMPARE(lyrics.state()["lines"].toList().size(), 4);
        const auto oldPosition = sc.position();
        const auto wasPlaying = sc.state()["playing"].toBool();
        sc.set("current", pageTrack.toMap()); sc.set("playing", true); sc.set("position", 11000);
        QTRY_COMPARE(detailsPage->property("lyricIndex").toInt(), 1);
        // Exercise actual delegate clicks against a local seekable stream, including pause/resume.
        QTemporaryDir lyricAudio; QVERIFY(lyricAudio.isValid());
        QFile lyricWav(lyricAudio.filePath("lyrics.wav")); QVERIFY(lyricWav.open(QIODevice::WriteOnly));
        QDataStream lyricData(&lyricWav); lyricData.setByteOrder(QDataStream::LittleEndian);
        constexpr int lyricBytes = 44100 * 2 * 40;
        lyricData.writeRawData("RIFF", 4); lyricData << quint32(lyricBytes + 36);
        lyricData.writeRawData("WAVEfmt ", 8); lyricData << quint32(16) << quint16(1) << quint16(1)
            << quint32(44100) << quint32(88200) << quint16(2) << quint16(16);
        lyricData.writeRawData("data", 4); lyricData << quint32(lyricBytes);
        lyricWav.write(QByteArray(lyricBytes, '\0')); lyricWav.close();
        sc.player().setSource(QUrl::fromLocalFile(lyricWav.fileName()));
        QTRY_VERIFY(sc.player().isSeekable());
        sc.pausePlayback();
        auto *lyricView = window->findChild<QQuickItem *>("lyricsList"); QVERIFY(lyricView);
        QVERIFY(QMetaObject::invokeMethod(lyricView, "positionViewAtIndex", Q_ARG(int, 2), Q_ARG(int, 1)));
        auto findLine = [&]() -> QQuickItem * {
            const auto content = lyricView->property("contentItem").value<QQuickItem *>();
            if (!content) return nullptr;
            for (auto *child : content->childItems())
                if (child->objectName() == "lyricLine2") return child;
            return nullptr;
        };
        QTRY_VERIFY(findLine());
        auto *line = findLine();
        QVERIFY(line->isEnabled());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          line->mapToScene(QPointF(line->width() / 2, line->height() / 2)).toPoint());
        QTRY_VERIFY(sc.state()["playbackActive"].toBool());
        QTRY_VERIFY(sc.player().position() >= 20000 && sc.player().position() < 22000);
        QTRY_COMPARE(detailsPage->property("lyricIndex").toInt(), 2);
        sc.pausePlayback();
        auto *activeText = line->property("contentItem").value<QQuickItem *>(); QVERIFY(activeText);
        QTRY_COMPARE(activeText->property("color").value<QColor>().rgba(), window->property("accent").value<QColor>().rgba());
        const auto savedThemes = preferences->property("savedThemes");
        const auto savedThemeName = preferences->property("themeName");
        preferences->setProperty("savedThemes", "[{\"name\":\"Lyrics test\",\"accent\":\"#3388cc\",\"tint\":0}]");
        preferences->setProperty("themeName", "Lyrics test");
        QTRY_COMPARE(activeText->property("color").value<QColor>().rgba(), window->property("accent").value<QColor>().rgba());
        QCOMPARE(window->property("baseAccent").value<QColor>(), QColor("#3388cc"));
        preferences->setProperty("savedThemes", savedThemes);
        preferences->setProperty("themeName", savedThemeName);
        sc.seek(500);
        QTRY_COMPARE(detailsPage->property("lyricIndex").toInt(), 0);
        sc.seek(11000);
        QTRY_COMPARE(detailsPage->property("lyricIndex").toInt(), 1);

        sc.m_waveAudioTimer.stop(); // Deterministic visualization fixture, no network audio.
        for (int i = 0; i < AudioDsp::spectrumBands; ++i)
            sc.m_spectrum[i] = 0.15 + 0.7 * std::abs(std::sin(i * 0.19)) * (1.0 - i / 140.0);
        emit sc.spectrumChanged();
        for (int style : {0, 1}) {
            preferences->setProperty("spectrumStyle", style); QTest::qWait(80);
            QVERIFY(window->grabWindow().save(style ? "ui-track-bars.png" : "ui-track-solid.png"));
        }
        auto *detailsScroll = window->findChild<QObject *>("trackDetailsScroll"); QVERIFY(detailsScroll);
        detailsScroll->setProperty("contentY", 0); QTest::qWait(80);
        QVERIFY(window->grabWindow().save("ui-track-lyrics.png"));
        sc.set("position", 500);
        QTRY_COMPARE(detailsPage->property("lyricIndex").toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(detailsPage, "loadLyrics"));
        auto *editLyrics = window->findChild<QObject *>("editLyrics"); QVERIFY(editLyrics);
        QVERIFY(QMetaObject::invokeMethod(editLyrics, "clicked"));
        auto *editor = window->findChild<QObject *>("customLyricsDialog"); QVERIFY(editor);
        QTRY_VERIFY(editor->property("opened").toBool());
        auto *editorText = window->findChild<QObject *>("customLyricText"); QVERIFY(editorText);
        editorText->setProperty("text", "[00:00.00]Saved words\n[00:05.00]Another line");
        QVERIFY(QMetaObject::invokeMethod(editor, "accept"));
        QTRY_VERIFY(lyrics.state()["custom"].toBool());
        QTRY_VERIFY(!editor->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(detailsPage, "loadLyrics"));
        QVERIFY(lyrics.state()["custom"].toBool());
        lyrics.removeCustomText();
        QTRY_COMPARE(lyrics.state()["lines"].toList().size(), 4);
        // Clicking on a different track starts it and retains the target while resolving streams.
        sc.set("current", currentBeforePage);
        net.responses = {{200, "{}", 100}};
        QVERIFY(QMetaObject::invokeMethod(lyricView, "positionViewAtIndex", Q_ARG(int, 2), Q_ARG(int, 1)));
        QTRY_VERIFY(findLine());
        line = findLine();
        QVERIFY(line->isEnabled());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          line->mapToScene(QPointF(line->width() / 2, line->height() / 2)).toPoint());
        QCOMPARE(sc.state()["current"].toMap()["urn"], pageTrack.toMap()["urn"]);
        QCOMPARE(sc.position(), 20000);
        QCOMPARE(sc.m_pendingSeek, 20000);
        QVERIFY(sc.state()["playbackActive"].toBool());
        sc.stopPlayback();
        sc.set("position", oldPosition); sc.set("playing", wasPlaying); sc.set("current", currentBeforePage);
        QVERIFY(QMetaObject::invokeMethod(detailsPage, "close"));
        QTRY_VERIFY(!detailsPage->property("opened").toBool());
        QVERIFY(!currentBeforePage.value("urn").toString().isEmpty());
        auto *dockTitle = window->findChild<QQuickItem *>("dockTrackTitle"); QVERIFY(dockTitle);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          dockTitle->mapToScene(QPointF(dockTitle->width() / 2, dockTitle->height() / 2)).toPoint());
        QTRY_VERIFY(detailsPage->property("opened").toBool());
        QVERIFY(detailsPage->property("isCurrent").toBool());
        QCOMPARE(detailsPage->property("track").toMap()["urn"], currentBeforePage["urn"]);
        QVERIFY(detailsPage->property("followCurrent").toBool());
        auto nextTrack = pageTrack.toMap(); nextTrack["urn"] = "soundcloud:tracks:next"; nextTrack["title"] = "Next playing track";
        sc.set("current", nextTrack);
        QTRY_COMPARE(detailsPage->property("track").toMap()["urn"], nextTrack["urn"]);
        QVERIFY(!window->findChild<QObject *>("lyricsQuery"));
        QVERIFY(detailsPage->property("isCurrent").toBool());
        QVERIFY(QMetaObject::invokeMethod(window, "openTrack", Q_ARG(QVariant, pageTrack), Q_ARG(QVariant, false)));
        QVERIFY(!detailsPage->property("followCurrent").toBool());
        sc.set("current", currentBeforePage);
        QTRY_COMPARE(detailsPage->property("track").toMap()["urn"], pageTrack.toMap()["urn"]);
        QVERIFY(QMetaObject::invokeMethod(detailsPage, "close"));
        const QVariantMap stationSeed{{"urn", "soundcloud:tracks:station-seed"}, {"title", "Station seed"},
            {"user", QVariantMap{{"urn", "soundcloud:users:artist"}, {"username", "Station artist"}}}};
        sc.set("home", QVariantList{QVariantMap{{"title", "Recommendations"}, {"tracks", QVariantList{stationSeed}}}});
        sc.m_homeLoaded = true;
        window->setProperty("section", 5);
        const auto findStationCard = [&]() -> QQuickItem * {
            std::function<QQuickItem *(QQuickItem *)> visit = [&](QQuickItem *item) -> QQuickItem * {
                if (item->objectName() == "artistStationCard0") return item;
                for (auto *child : item->childItems()) if (auto *found = visit(child)) return found;
                return nullptr;
            };
            return visit(window->contentItem());
        };
        QTRY_VERIFY(findStationCard());
        const auto currentBeforeStation = sc.state()["current"];
        const auto queueBeforeStation = sc.state()["queue"];
        net.responses = {{200, QJsonDocument(QJsonArray{QJsonObject{{"urn", "soundcloud:tracks:related"}, {"title", "Related track"}}}).toJson()}};
        QVERIFY(QMetaObject::invokeMethod(findStationCard(), "clicked"));
        QTRY_VERIFY(!sc.state()["playlistBusy"].toBool());
        QCOMPARE(sc.state()["playlistTracks"].toList().size(), 2);
        QCOMPARE(sc.state()["current"], currentBeforeStation);
        QCOMPARE(sc.state()["queue"], queueBeforeStation);
        QTest::qWait(250);
        QVERIFY(window->grabWindow().save("ui-artist-station.png"));
        for (const auto &warning : warnings) QVERIFY2(!warning.contains("ReferenceError") && !warning.contains("TypeError") && !warning.contains("Binding loop"), qPrintable(warning));
    }
    void logoutInvalidatesPendingResponses() {
        Network net; net.responses = {guest(), {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"}]}", 100}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool()); sc.browse("tracks", "test"); sc.logout();
        QTest::qWait(150); QVERIFY(!sc.state()["connected"].toBool()); QVERIFY(sc.state()["tracks"].toList().isEmpty());
        QVERIFY(!QFile::exists(config + "/session.json"));
    }
};
QTEST_MAIN(SoundCloudTest)
#include "tst_soundcloud.moc"
