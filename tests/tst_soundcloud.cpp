#include "soundcloud.h"
#include <QtTest>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <cstring>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QDesktopServices>
#include <QSettings>
#include <QTcpSocket>
#include <QWheelEvent>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

// Транспорт подменяется только в тестах. Рабочий клиент использует фиксированные HTTPS-хосты.
#ifndef Q_MOC_RUN
struct Response { int status = 200; QByteArray body; int delay = 0; QNetworkReply::NetworkError error = QNetworkReply::NoError; int chunkSize = 0; qint64 contentLength = -2; bool lateSignals = false; };
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
            if (!metadataEmitted) { metadataEmitted = true; emit metaDataChanged(); }
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
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoing) override {
        Q_UNUSED(op)
        requests.append(request); bodies.append(outgoing ? outgoing->readAll() : QByteArray{});
        methods.append(request.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray());
        Response r = responses.isEmpty() ? Response{500, "{}"} : responses.takeFirst();
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
    bool read(const QString &key, QByteArray &value) override {
        if (!readable) return false;
        value = records.value(key); return true;
    }
    bool write(const QString &key, const QByteArray &value) override {
        if (!writable || writesBeforeFailure == 0) return false;
        if (writesBeforeFailure > 0) --writesBeforeFailure;
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
        secrets->writable = false;
        QTRY_VERIFY(!sc.state()["error"].toString().isEmpty());
        QVERIFY(!sc.m_storageReady); QVERIFY(!sc.state()["connected"].toBool());
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
        secrets->removable = true;
        sc.logout(); QVERIFY(!secrets->records.contains("session"));
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
    void independentRequestErrorsKeepCatalogBusy() {
        for (const auto &response : {Response{500, "{}"}, Response{200, "{invalid"}}) {
            Network net; net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"}]}", 100}, response};
            SoundCloud sc(nullptr, &net, secrets);
            sc.set("configured", false); sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
            sc.browse("tracks", "loading"); sc.profile();
            QTRY_VERIFY(!sc.state()["error"].toString().isEmpty());
            QVERIFY(sc.state()["busy"].toBool());
            QTRY_VERIFY(!sc.state()["busy"].toBool());
            QCOMPARE(sc.state()["tracks"].toList().size(), 1);
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
        QVERIFY(QFileInfo::exists(verifiedPath));
        const QString nextName = QFileInfo::exists("/etc/arch-release") ? "scrip-1000.0-x86_64.pkg.tar.zst" : "SCrip-1000.0-amd64.deb";
        const QJsonObject nextAsset{{"name", nextName}, {"digest", "sha256:" + QString(64, '1')}, {"size", 4}, {"browser_download_url", "https://github.com/sykia/SCrip/releases/download/v1000.0/" + nextName}};
        net.responses = {{200, QJsonDocument(QJsonObject{{"tag_name", "v1000.0"}, {"assets", QJsonArray{nextAsset}}}).toJson()}};
        sc.checkForUpdates(); QTRY_VERIFY(!sc.state()["updateChecking"].toBool());
        QVERIFY(!sc.state()["updateReady"].toBool()); QVERIFY(sc.state()["updateAvailable"].toBool());
        QCoreApplication::setApplicationVersion(previousVersion);
    }
    void updateDownloadStreamsChunksAndRejectsInvalidLengthAndOversizedBody() {
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

        Network badNet; badNet.responses = {guest()}; SoundCloud bad(nullptr, &badNet, secrets); QTRY_VERIFY(bad.state()["connected"].toBool());
        QCoreApplication::setApplicationVersion("1.0.0"); badNet.responses = {{200, metadata}}; bad.checkForUpdates(); QTRY_VERIFY(!bad.state()["updateChecking"].toBool());
        badNet.responses = {{200, payload + "overflow", 0, QNetworkReply::NoError, 2}}; bad.downloadUpdate(); QTRY_VERIFY(!bad.state()["updateDownloading"].toBool());
        QVERIFY(!bad.state()["updateReady"].toBool()); QCoreApplication::setApplicationVersion(previousVersion);

        Network lengthNet; lengthNet.responses = {guest()}; SoundCloud invalidLength(nullptr, &lengthNet, secrets); QTRY_VERIFY(invalidLength.state()["connected"].toBool());
        QCoreApplication::setApplicationVersion("1.0.0"); lengthNet.responses = {{200, metadata}}; invalidLength.checkForUpdates(); QTRY_VERIFY(!invalidLength.state()["updateChecking"].toBool());
        lengthNet.responses = {{200, payload, 0, QNetworkReply::NoError, 2, -1}}; invalidLength.downloadUpdate(); QTRY_VERIFY(!invalidLength.state()["updateDownloading"].toBool());
        QVERIFY(!invalidLength.state()["updateReady"].toBool()); QCoreApplication::setApplicationVersion(previousVersion);
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
    void oauthDoesNotConfirmLoginWhenFinalSessionSaveFails() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        connect(&sc, &SoundCloud::authorizationRequested, this, &SoundCloudTest::captureUrl);
        bool confirmed = false;
        connect(&sc, &SoundCloud::signedInChanged, this, [&] { confirmed |= sc.signedIn(); });
        sc.login(); QVERIFY(sc.state()["authBusy"].toBool());
        // Обмен кода сохраняет токены, но фиксация пользовательской сессии отказывает.
        secrets->writesBeforeFailure = 1;
        net.responses = {guest()};
        const int requestsBeforeCallback = net.requests.size();
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost, callbackPort);
        QVERIFY(socket.waitForConnected());
        socket.write("GET /callback?code=valid-code&state=" + QUrlQuery(authorizationUrl).queryItemValue("state").toUtf8()
                     + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"); socket.flush();
        QTRY_VERIFY(socket.bytesAvailable() > 0);
        QVERIFY(socket.readAll().startsWith("HTTP/1.1 400"));
        QVERIFY(!confirmed); QVERIFY(!sc.signedIn());
        QVERIFY(!sc.state()["connected"].toBool());
        QVERIFY(!sc.state()["authBusy"].toBool());
        QVERIFY(sc.state()["authStep"].toString().isEmpty());
        QCOMPARE(sc.state()["error"].toString(), AuthStorage::errorMessage());
        QVERIFY(!sc.m_userSession); QVERIFY(!sc.m_storageReady);
        QVERIFY(sc.state()["profile"].toMap().isEmpty());
        QCOMPARE(net.requests.size(), requestsBeforeCallback + 1);
        QVERIFY(!QFile::exists(config + "/session.json"));
    }
    void oauthCancellationBeforeCallbackAndDuringExchange() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        connect(&sc, &SoundCloud::authorizationRequested, this, &SoundCloudTest::captureUrl);

        sc.login(); QVERIFY(sc.state()["authBusy"].toBool());
        sc.cancelLogin();
        QTest::qWait(20);
        QVERIFY(!sc.state()["authBusy"].toBool());
        QCOMPARE(net.requests.size(), 1);

        sc.login(); QVERIFY(sc.state()["authBusy"].toBool());
        const QByteArray callback = "GET /callback?code=cancelled&state=" + QUrlQuery(authorizationUrl).queryItemValue("state").toUtf8()
            + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
        net.responses = {{200, "{\"access_token\":\"late-access\",\"refresh_token\":\"late-refresh\",\"expires_in\":3600}", 150}};
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost, callbackPort); QVERIFY(socket.waitForConnected());
        socket.write(callback); socket.flush();
        QTRY_COMPARE(net.requests.size(), 2);
        QCOMPARE(sc.state()["authStep"].toString(), QString("exchange"));
        sc.cancelLogin();
        QTest::qWait(220);
        QVERIFY(!sc.signedIn()); QVERIFY(!sc.state()["authBusy"].toBool());
        QVERIFY(!sc.m_userSession); QVERIFY(!QFile::exists(config + "/session.json"));

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
        // Щелчки по центрам зелёной, жёлтой и красной кнопок в реальном QML.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(65, 28));
        QTRY_COMPARE(window->visibility(), QWindow::Maximized);
#ifdef Q_OS_WIN
        if (native) QTRY_VERIFY(IsZoomed(hwnd));
#endif
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(65, 28));
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(45, 28));
        QTRY_COMPARE(window->visibility(), QWindow::Minimized);
#ifdef Q_OS_WIN
        if (native) QTRY_VERIFY(IsIconic(hwnd));
#endif
        window->showNormal();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(25, 28));
        QTRY_VERIFY(!window->isVisible());
    }
    void qmlScreensRender() {
        QFile session(config + "/session.json"); QVERIFY(session.open(QIODevice::WriteOnly));
        session.write(QJsonDocument(QJsonObject{{"client_id", "test-client"}, {"access_token", "test-access"}, {"refresh_token", "test-refresh"}, {"expires_at", QDateTime::currentSecsSinceEpoch()+3600}, {"user_session", true}}).toJson()); session.close();
        Network net; net.responses = {{200, "{\"username\":\"Тестовый аккаунт\"}"}, {200, "[]"}}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        QTRY_VERIFY(!sc.state()["busy"].toBool());
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("sc", &sc);
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
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\",\"title\":\"Тестовый трек с длинным названием для проверки вёрстки\",\"duration\":185000,\"user\":{\"username\":\"Исполнитель\"}},{\"urn\":\"soundcloud:tracks:2\",\"title\":\"Второй трек\",\"duration\":220000,\"access\":\"preview\"}]}"}};
        sc.browse("tracks", "Тест интерфейса"); QTRY_COMPARE(sc.state()["tracks"].toList().size(), 2);
        QCOMPARE(window->property("api").toMap()["tracks"].toList(), sc.state()["tracks"].toList());
        QTest::qWait(450); QVERIFY(window->grabWindow().save("ui-catalog.png"));
        QVERIFY(window->property("dockedInspector").toBool());
        QJsonArray scrollTracks;
        for (int i=0; i<32; ++i) scrollTracks.append(QJsonObject{{"urn", QString("soundcloud:tracks:%1").arg(i+10)}, {"title", "Проверка прокрутки"}});
        net.responses = {{200, QJsonDocument(QJsonObject{{"collection", scrollTracks}, {"next_href","https://api.soundcloud.com/tracks?cursor=next"}}).toJson()}};
        sc.browse("tracks", "scroll"); QTRY_COMPARE(sc.state()["tracks"].toList().size(),32);
        QTest::qWait(100);
        auto *scroll = window->findChild<QObject *>("libraryScroll"); QVERIFY(scroll);
        QWheelEvent wheel(QPointF(600,400),QPointF(600,400),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(window,&wheel);
        QTRY_VERIFY(scroll->property("contentY").toReal()>0);
        QVERIFY(scroll->property("contentY").toReal()<=scroll->property("contentHeight").toReal()-scroll->property("height").toReal());
        net.responses = {{200, "[]"}};
        net.responses = {{200, R"([{"urn":"soundcloud:tracks:99","title":"Следующая страница"}])"}};
        QTest::qWait(550); // Дождаться завершения анимации колеса перед переходом в конец.
        scroll->setProperty("contentY", scroll->property("contentHeight").toReal() - scroll->property("height").toReal());
        QTRY_COMPARE(sc.state()["tracks"].toList().size(), 33);
        net.responses = {{200, "[]"}};
        window->setProperty("section", 3); QTest::qWait(400);
        QVERIFY(window->grabWindow().save("ui-profile.png"));
        window->setProperty("section", 4); window->setProperty("settingGroup", 3); QTest::qWait(100);
        QVERIFY(window->grabWindow().save("ui-connection.png"));
        window->setWidth(760); window->setHeight(560); QTest::qWait(350);
        QVERIFY(!window->property("dockedInspector").toBool());
        QVERIFY(window->grabWindow().save("ui-narrow.png"));
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
