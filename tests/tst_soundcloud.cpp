#include "soundcloud.h"
#include "playercommand.h"
#include "desktopmedia.h"
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusArgument>
#include <QDBusObjectPath>
#endif
#include "recommendations.h"
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
#include <QQmlProperty>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QDesktopServices>
#include <QSettings>
#include <QScopeGuard>
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
struct Response { int status = 200; QByteArray body; int delay = 0; QNetworkReply::NetworkError error = QNetworkReply::NoError; int chunkSize = 0; qint64 contentLength = -2; bool lateSignals = false; bool redirectBeforeBody = false; QUrl redirect; QList<QPair<QByteArray, QByteArray>> headers; };
class Reply : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, Response response, QObject *parent) : QNetworkReply(parent), bytes(response.body), chunkSize(response.chunkSize > 0 ? response.chunkSize : qMax(1, response.body.size())) {
        for (const auto &header : response.headers) setRawHeader(header.first, header.second);
        setRequest(request); setUrl(request.url()); setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        if (!response.redirect.isEmpty()) setAttribute(QNetworkRequest::RedirectionTargetAttribute, response.redirect);
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
#ifdef Q_OS_LINUX
    QVariantMap mprisChanges;
#endif
public slots:
#ifdef Q_OS_LINUX
    void captureMpris(const QString &interface, const QVariantMap &changes, const QStringList &) {
        if (interface == "org.mpris.MediaPlayer2.Player") mprisChanges = changes;
    }
#endif
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
    void undoCurrentRemovalAndEmptyQueueDoesNotRestartAudio() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false); sc.setPreloadEnabled(false);
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}, {"access", "blocked"}}, b{{"urn", "soundcloud:tracks:2"}, {"access", "blocked"}};
        sc.m_queue = {a,b,a}; sc.m_queueIndex = 0; sc.resetQueueOrder();
        sc.removeQueue(0); const auto current = sc.state()["current"].toMap();
        auto *active = sc.m_activePlayer; const auto generation = sc.m_playGeneration;
        sc.undoQueueEdit(); QCOMPARE(sc.m_queue, QVariantList({a,b,a}));
        QCOMPARE(sc.state()["current"].toMap(), current); QCOMPARE(sc.m_queueIndex, 1);
        QCOMPARE(sc.m_activePlayer, active); QCOMPARE(sc.m_playGeneration, generation);
        sc.m_queue = {a}; sc.m_queueIndex = 0; sc.resetQueueOrder();
        sc.removeQueue(0); QVERIFY(sc.m_queue.isEmpty()); const auto stopped = sc.m_playGeneration;
        sc.undoQueueEdit(); QCOMPARE(sc.m_queue, QVariantList({a})); QCOMPARE(sc.m_queueIndex, -1);
        QVERIFY(!sc.state()["playing"].toBool()); QCOMPARE(sc.m_playGeneration, stopped);
    }
    void previewAccessDoesNotRequestFullAudioOrChangeQueue() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        const QVariantMap preview{{"urn", "soundcloud:tracks:1"}, {"access", "preview"}}, next{{"urn", "soundcloud:tracks:2"}, {"access", "blocked"}};
        sc.playMix({preview, next}, 0);
        QCOMPARE(net.requests.size(), 0); QCOMPARE(sc.m_queue.size(), 2); QCOMPARE(sc.m_queueIndex, 0);
        QCOMPARE(sc.state()["playbackAccess"].toString(), QString("preview"));
        QVERIFY(!sc.state()["buffering"].toBool()); QVERIFY(!sc.state()["playbackActive"].toBool());
        sc.next(); QCOMPARE(sc.m_queueIndex, 1); QCOMPARE(sc.state()["current"].toMap(), next);
    }
    void wholePlaylistSearchRetainsDuplicateOccurrences() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        const QVariantMap playlist{{"urn", "soundcloud:playlists:2"}};
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"}],\"next_href\":\"https://api.soundcloud.com/playlists/2/tracks?cursor=2\"}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"},{\"urn\":\"soundcloud:tracks:2\"}]}"}};
        sc.loadPlaylist(playlist); QTRY_VERIFY(!sc.state()["playlistBusy"].toBool());
        sc.searchWholeCollection(true); QTRY_VERIFY(!sc.state()["collectionSearching"].toBool());
        QCOMPARE(sc.state()["collectionSearchStatus"].toString(), QString("complete"));
        const auto rows = sc.state()["playlistTracks"].toList(); QCOMPARE(rows.size(), 3); QCOMPARE(rows[0], rows[1]);
        QVERIFY(sc.state()["tracks"].toList().isEmpty());
    }
    void commentPolicyHidesLateRepliesAndAllowsUnknownFields() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600; sc.set("signedIn", true);
        QVariantMap track{{"urn", "soundcloud:tracks:1"}, {"commentable", false}};
        sc.setCommentTrack(track);
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:comments:1\"}]}", 50}};
        sc.loadComments(track["urn"].toString());
        sc.postComment(track["urn"].toString(), "blocked");
        QCOMPARE(net.requests.size(), 1);
        track["reveal_comments"] = false; sc.setCommentTrack(track);
        QTest::qWait(80);
        QVERIFY(sc.commentsState()["hidden"].toBool()); QVERIFY(sc.commentsState()["items"].toList().isEmpty());
        sc.loadComments(track["urn"].toString(), false, true); QCOMPARE(net.requests.size(), 1);
        track.remove("commentable"); track["reveal_comments"] = true; sc.setCommentTrack(track);
        net.responses = {{200, "{\"collection\":[]}"}, {201, "{\"urn\":\"soundcloud:comments:2\"}"}};
        sc.loadComments(track["urn"].toString()); QTRY_VERIFY(!sc.commentsState()["busy"].toBool());
        sc.postComment(track["urn"].toString(), "allowed"); QTRY_VERIFY(!sc.commentsState()["sending"].toBool());
        QCOMPARE(net.requests.size(), 3);
    }
    void queueUndoPreservesOccurrenceAndInvalidatesAfterManualChanges() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false); sc.setPreloadEnabled(false);
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}}, b{{"urn", "soundcloud:tracks:2"}};
        sc.m_queue = {a,b,a,b}; sc.m_queueIndex = 2; sc.resetQueueOrder();
        const auto originalIds = sc.m_queueIds;
        sc.set("current", a); sc.set("position", 5432); auto *player = sc.m_activePlayer;
        sc.setShuffle(true); sc.setRepeatMode(2); sc.recordQueueEntry(); sc.m_forceNext = true;
        sc.deduplicateQueue(); QVERIFY(sc.state()["queueUndoAvailable"].toBool());
        sc.undoQueueEdit(); QCOMPARE(sc.m_queue, QVariantList({a,b,a,b})); QCOMPARE(sc.m_queueIds, originalIds);
        QCOMPARE(sc.m_queueIndex, 2); QCOMPARE(sc.position(), 5432); QCOMPARE(sc.m_activePlayer, player); QVERIFY(sc.m_forceNext);
        sc.clearQueueAfterCurrent(); sc.undoQueueEdit(); QCOMPARE(sc.m_queueIds, originalIds);
        sc.removeQueue(0); sc.undoQueueEdit(); QCOMPARE(sc.m_queueIds, originalIds); QCOMPARE(sc.m_queueIndex, 2);
        sc.removeQueue(0); sc.enqueue(b); const auto edited = sc.m_queue;
        QVERIFY(!sc.state()["queueUndoAvailable"].toBool()); sc.undoQueueEdit(); QCOMPARE(sc.m_queue, edited);
        sc.clearQueueAfterCurrent(); sc.set("profile", QVariantMap{{"urn", "soundcloud:users:9"}});
        QVERIFY(!sc.state()["queueUndoAvailable"].toBool());
        sc.resetQueueOrder(); sc.undoQueueEdit(); QCOMPARE(sc.m_queue, edited.mid(0, sc.m_queueIndex + 1));
    }
    void wholeCollectionSearchProgressCancellationAndStaleAccount() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_lastPath = "/me/likes/tracks";
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"}],\"next_href\":\"https://api.soundcloud.com/me/likes/tracks?cursor=2\"}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:2\"}],\"next_href\":\"https://api.soundcloud.com/me/likes/tracks?cursor=3\"}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:3\"}]}"}};
        sc.collection(sc.m_lastPath); QTRY_VERIFY(!sc.state()["busy"].toBool());
        sc.searchWholeCollection(); QTRY_COMPARE(sc.state()["tracks"].toList().size(), 2);
        sc.cancelCollectionSearch(); QTest::qWait(400); QCOMPARE(net.requests.size(), 2);
        sc.searchWholeCollection(); QTRY_COMPARE(sc.state()["collectionSearchStatus"].toString(), QString("complete"));
        QCOMPARE(sc.state()["tracks"].toList().size(), 3);
        sc.m_nextHref = "https://api.soundcloud.com/me/likes/tracks?cursor=4";
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:4\"}]}", 80}};
        sc.searchWholeCollection(); sc.clearSession(); QTest::qWait(120);
        QVERIFY(sc.state()["tracks"].toList().isEmpty()); QVERIFY(!sc.state()["collectionSearching"].toBool());
    }
    void wholeCollectionSearchRejectsPageLoops() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_lastPath = "/me/likes/tracks"; sc.m_nextHref = "https://api.soundcloud.com/me/likes/tracks?cursor=loop";
        net.responses = {{200, "{\"collection\":[],\"next_href\":\"https://api.soundcloud.com/me/likes/tracks?cursor=loop\"}"}};
        sc.searchWholeCollection(); QTRY_VERIFY(!sc.state()["busy"].toBool());
        QCOMPARE(sc.state()["collectionSearchStatus"].toString(), QString("error")); QVERIFY(!sc.state()["collectionSearching"].toBool());
        QTest::qWait(400); QCOMPARE(net.requests.size(), 1);
    }
    void logoutReportsRevocationWithoutRestoringTokens() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_userSession = true; sc.set("signedIn", true);
        net.responses = {{503, "{}"}};
        sc.logout(); QVERIFY(!sc.signedIn()); QVERIFY(sc.m_access.isEmpty());
        QTRY_COMPARE(sc.state()["disconnectStatus"].toString(), QString("unconfirmed"));
        QVERIFY(sc.state()["error"].toString().contains("на этом устройстве")); QVERIFY(sc.m_refresh.isEmpty());
        sc.m_access = "test-access"; sc.m_userSession = true; sc.set("signedIn", true);
        net.responses = {{204, {}}}; sc.logout();
        QTRY_COMPARE(sc.state()["disconnectStatus"].toString(), QString("confirmed"));
    }
    void stationFailureOffersRelatedCollection() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        QSignalSpy resolved(&sc, &SoundCloud::resourceResolved);
        net.responses = {{404, "{}"}};
        sc.openStation("soundcloud:system-playlists:track-stations:42", {{"urn", "soundcloud:tracks:7"}, {"title", "Seed"}});
        QTRY_COMPARE(resolved.size(), 1);
        const auto collection = resolved.first().first().toMap();
        QCOMPARE(collection["preview_type"].toString(), QString("station"));
        QVERIFY(collection["title"].toString().startsWith("Подборка SCrip"));
        QVERIFY(sc.m_queue.isEmpty());
    }

    void qualitySegmentsMouseAndKeyboard() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        QQmlEngine engine; engine.rootContext()->setContextProperty("sc", &sc);
        QQmlComponent component(&engine);
        component.setData("import QtQuick; PlaybackQuality { value: sc.catalogState.playbackQuality || 0; onEdited: value => sc.setPlaybackQuality(value) }",
                          QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/QualityFixture.qml"));
        QScopedPointer<QObject> root(component.create()); QVERIFY2(root, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(root.data()); QVERIFY(item);
        QQuickWindow window; window.resize(284, 44); item->setWidth(284); item->setHeight(44); item->setParentItem(window.contentItem());
        window.show(); QTest::qWait(50);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(213, 22));
        QTRY_COMPARE(sc.state().value("playbackQuality").toInt(), 1);
        std::function<QQuickItem *(QQuickItem *)> findStandard = [&](QQuickItem *parent) -> QQuickItem * {
            if (parent->objectName() == "qualityOption0") return parent;
            for (auto *child : parent->childItems()) if (auto *found = findStandard(child)) return found;
            return nullptr;
        };
        auto *standard = findStandard(item); QVERIFY(standard);
        standard->forceActiveFocus(); QTest::keyClick(&window, Qt::Key_Space);
        QTRY_COMPARE(sc.state().value("playbackQuality").toInt(), 0);
        QVERIFY(standard->property("checked").toBool());
        QCOMPARE(QQmlProperty(standard, "Accessible.name", QQmlEngine::contextForObject(standard)).read().toString(), QString("Стандартное"));
        item->setParentItem(nullptr);
    }
    void playbackPreferencesAndFullStreamSelection() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        const QJsonObject streams{{"hls_aac_160_url", "https://cdn.example/full160.m3u8"},
            {"hls_aac_96_url", "https://cdn.example/full96.m3u8"}, {"hls_mp3_128_url", "https://cdn.example/full128.m3u8"},
            {"preview_mp3_128_url", "https://cdn.example/preview.mp3"}};
        QCOMPARE(sc.fullStreams(streams).first().first, QString("hls_aac_160_url"));
        QVERIFY(sc.state().value("preloadEnabled").toBool());
        sc.setPlaybackQuality(1);
        QCOMPARE(sc.fullStreams(streams).first().first, QString("hls_aac_96_url"));
        auto fallback = streams; fallback.remove("hls_aac_96_url");
        QCOMPARE(sc.fullStreams(fallback).first().first, QString("hls_mp3_128_url"));
        fallback.remove("hls_mp3_128_url"); fallback.remove("hls_aac_160_url");
        QVERIFY(sc.fullStreams(fallback).isEmpty());
        sc.setPreloadEnabled(false); sc.setPreloadWifiOnly(true);
        Network restartedNet; SoundCloud restarted(nullptr, &restartedNet, secrets);
        QCOMPARE(restarted.state().value("playbackQuality").toInt(), 1);
        QVERIFY(!restarted.state().value("preloadEnabled").toBool());
        QVERIFY(restarted.state().value("preloadWifiOnly").toBool());
        QVERIFY(!restarted.preloadAllowed());
    }
    void disablingPreloadCancelsRequestsAndKeepsCurrent() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_queue = {QVariantMap{{"urn", "soundcloud:tracks:1"}}, QVariantMap{{"urn", "soundcloud:tracks:2"}}};
        sc.m_queueIndex = 0; sc.resetQueueOrder(); sc.recordQueueEntry();
        sc.player().setSource(QUrl::fromLocalFile(directory.filePath("current.wav")));
        const auto current = sc.player().source();
        net.responses = {{200, "{\"hls_aac_160_url\":\"https://cdn.example/next.m3u8\"}", 200}};
        sc.prepareNext(); QVERIFY(sc.m_preloadReply);
        const auto reply = sc.m_preloadReply;
        const auto generation = sc.m_preloadGeneration;
        sc.setPreloadEnabled(false);
        QVERIFY(reply->isFinished()); QCOMPARE(reply->error(), QNetworkReply::OperationCanceledError);
        QVERIFY(sc.m_preloadGeneration > generation); QVERIFY(sc.m_preloadPlayer->source().isEmpty());
        QCOMPARE(sc.player().source(), current);
        const auto requests = net.requests.size();
        sc.setCrossfade(true); sc.prepareNext(); sc.beginCrossfade(); QTest::qWait(250);
        QCOMPARE(net.requests.size(), requests); QVERIFY(!sc.m_crossfadeStarted);
        sc.setPlaybackQuality(1); QCOMPARE(sc.player().source(), current);
        sc.stopPlayback(); QVERIFY(sc.m_preloadPlayer->source().isEmpty());
    }

    void desktopRealPlayerCommandsAndCrossfadePolicy() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        QFile wav(directory.filePath("system-media.wav")); QVERIFY(wav.open(QIODevice::WriteOnly));
        QDataStream data(&wav); data.setByteOrder(QDataStream::LittleEndian);
        constexpr int bytes = 44100 * 2 * 10;
        data.writeRawData("RIFF", 4); data << quint32(bytes + 36); data.writeRawData("WAVEfmt ", 8);
        data << quint32(16) << quint16(1) << quint16(1) << quint32(44100) << quint32(88200) << quint16(2) << quint16(16);
        data.writeRawData("data", 4); data << quint32(bytes); wav.write(QByteArray(bytes, '\0')); wav.close();
        const QUrl source = QUrl::fromLocalFile(wav.fileName());
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}, {"title", "First"}}, b{{"urn", "soundcloud:tracks:2"}, {"title", "Second"}};
        sc.m_queue = {a, b}; sc.m_queueIndex = 0; sc.resetQueueOrder(); sc.recordQueueEntry();
        sc.set("current", a); sc.player().setSource(source);
        QTRY_VERIFY(sc.player().isSeekable());
        DesktopMedia media(&sc, nullptr); QSignalSpy updates(&media, &DesktopMedia::updated); QSignalSpy seeked(&media, &DesktopMedia::seeked);
        const QString first = media.snapshot().value("trackId").toString();
        media.command("seek", 4500, first); QTRY_COMPARE(sc.player().position(), 4500);
        QVERIFY(seeked.size() > 0);
        media.command("previous"); QTRY_COMPARE(sc.player().position(), 0);
        sc.m_preloadUrn = "soundcloud:tracks:2"; sc.m_preloadIndex = 1; sc.m_preloadPlayer->setSource(source);
        QTRY_VERIFY(sc.m_preloadPlayer->isSeekable());
        media.command("next"); QCOMPARE(sc.m_queueIndex, 1);
        QVERIFY(!sc.state().value("playbackActive").toBool());
        QCOMPARE(media.snapshot().value("title").toString(), QString("Second"));
        QVERIFY(media.snapshot().value("trackId").toString() != first);
        media.command("seek", 4000, first); QVERIFY(sc.position() != 4000);
        media.command("play"); QTRY_VERIFY(sc.state().value("playing").toBool());
        media.command("pause"); QTRY_VERIFY(!sc.state().value("playing").toBool());
        // Prepare an incoming real decoder, commit a ramp, then change policy.
        sc.m_queueIndex = 0; sc.recordQueueEntry(); sc.set("current", a);
        sc.m_preloadUrn = "soundcloud:tracks:2"; sc.m_preloadIndex = 1; sc.m_preloadPlayer->setSource(source);
        QTRY_VERIFY(sc.m_preloadPlayer->isSeekable());
        sc.beginCrossfade(); QVERIFY(sc.m_crossfadeStarted);
        auto *incoming = sc.m_activePlayer;
        sc.setPreloadEnabled(false);
        QVERIFY(!sc.m_crossfadeStarted); QCOMPARE(sc.m_activePlayer, incoming); QCOMPARE(sc.player().source(), source);
        QVERIFY(sc.m_preloadPlayer->source().isEmpty()); QVERIFY(!sc.preloadAllowed());
        sc.setPlaybackQuality(1); QCOMPARE(sc.player().source(), source);
        QVERIFY(!sc.m_preloadReply || sc.m_preloadReply->isFinished());
        sc.logout(); QTRY_VERIFY(media.snapshot().value("title").toString().isEmpty());
        QVERIFY(!media.snapshot().value("hasTrack").toBool()); QVERIFY(updates.size() > 0);
    }
    void preloadWaitingForTokenDoesNotEscapeDisabledPolicy() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.set("configured", true); sc.m_refresh = "test-refresh";
        sc.m_access.clear(); sc.m_expires = 0; sc.m_refreshing = true;
        sc.m_queue = {QVariantMap{{"urn", "soundcloud:tracks:1"}}, QVariantMap{{"urn", "soundcloud:tracks:2"}}};
        sc.m_queueIndex = 0; sc.player().setSource(QUrl::fromLocalFile(directory.filePath("missing.wav")));
        sc.prepareNext(); QVERIFY(!sc.m_waiting.isEmpty());
        sc.setPreloadEnabled(false);
        const int requests = net.requests.size();
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600; sc.m_refreshing = false;
        const auto waiting = std::exchange(sc.m_waiting, {});
        for (const auto &operation : waiting) operation.continuation();
        QCOMPARE(net.requests.size(), requests); QVERIFY(sc.m_preloadPlayer->source().isEmpty());
    }
    void desktopCommandsMetadataAndPrivacy() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        DesktopMedia media(&sc, nullptr);
        QVERIFY(!media.snapshot().value("hasTrack").toBool());
        sc.set("current", QVariantMap{{"urn", "soundcloud:tracks:1"}, {"title", "Track"},
            {"user", QVariantMap{{"username", "Artist"}}}, {"artwork_url", "https://i1.sndcdn.com/cover.jpg?token=secret"},
            {"waveform_url", "https://wave.sndcdn.com/a.png?oauth_token=secret"},
            {"stream_url", "https://cdn.example/secret?oauth_token=secret"}});
        sc.set("duration", 10000); sc.set("position", 4000);
        QCOMPARE(media.snapshot().value("title").toString(), QString("Track"));
        QCOMPARE(media.snapshot().value("artist").toString(), QString("Artist"));
        QVERIFY(media.snapshot().value("artwork").toString().isEmpty());
        QVERIFY(!QJsonDocument::fromVariant(media.snapshot()).toJson().contains("secret"));
        const auto track = media.snapshot().value("trackId");
        media.command("pause"); QVERIFY(!sc.state().value("playbackActive").toBool());
        media.command("seek", 3000, "/stale"); QCOMPARE(sc.position(), 4000);
        media.command("next"); QCOMPARE(media.snapshot().value("trackId"), track);
        media.command("stop"); QCOMPARE(media.snapshot().value("status").toString(), QString("Stopped"));
        QVERIFY(media.snapshot().value("title").toString().isEmpty());
    }
#ifdef Q_OS_LINUX
    void mprisSessionBusProtocol() {
        if (!QDBusConnection::sessionBus().isConnected()) QSKIP("Session bus unavailable");
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        DesktopMedia media(&sc, nullptr);
        sc.set("current", QVariantMap{{"urn", "soundcloud:tracks:1"}, {"title", "D-Bus track"}});
        sc.set("duration", 12345);
        const QString service = "org.mpris.MediaPlayer2.scrip.instance" + QString::number(QCoreApplication::applicationPid());
        // A second bus connection exercises native serialization and method routing.
        auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "mpris-test-client");
        mprisChanges.clear();
        QVERIFY(bus.connect(service, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "PropertiesChanged",
                            this, SLOT(captureMpris(QString,QVariantMap,QStringList))));
        auto msg = QDBusMessage::createMethodCall(service, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "GetAll");
        msg << QString("org.mpris.MediaPlayer2.Player");
        QDBusPendingCallWatcher get(bus.asyncCall(msg)); QSignalSpy got(&get, &QDBusPendingCallWatcher::finished);
        QTRY_VERIFY(get.isFinished());
        QDBusPendingReply<QVariantMap> reply = get; QVERIFY2(!reply.isError(), qPrintable(reply.error().message()));
        const auto properties = reply.value();
        QCOMPARE(properties.value("PlaybackStatus").toString(), QString("Paused"));
        const auto metadata = qdbus_cast<QVariantMap>(properties.value("Metadata"));
        QCOMPARE(metadata.value("xesam:title").toString(), QString("D-Bus track"));
        QCOMPARE(metadata.value("mpris:length").toLongLong(), 12345000);
        QTest::qWait(30); mprisChanges.clear();
        auto changedTrack = sc.state().value("current").toMap(); changedTrack.insert("title", "Updated over D-Bus"); sc.set("current", changedTrack);
        QTRY_VERIFY(mprisChanges.contains("Metadata"));
        QCOMPARE(qdbus_cast<QVariantMap>(mprisChanges.value("Metadata")).value("xesam:title").toString(), QString("Updated over D-Bus"));
        QVERIFY(!mprisChanges.contains("Position"));
        msg = QDBusMessage::createMethodCall(service, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", "Stop");
        QDBusPendingCallWatcher stop(bus.asyncCall(msg)); QTRY_VERIFY(stop.isFinished());
        QVERIFY(sc.state().value("current").toMap().isEmpty());
        QDBusConnection::disconnectFromBus("mpris-test-client");
    }
#endif

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
    void advancedTrackSearchValidation() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        for (const auto &filters : QList<QVariantMap>{
                 {{"durationFrom", "-1"}}, {{"durationTo", "1.5"}}, {{"durationTo", "2147484"}},
                 {{"bpmFrom", "140"}, {"bpmTo", "120"}}, {{"durationFrom", "20"}, {"durationTo", "10"}},
                 {{"createdFrom", "2025-02-29"}}, {{"createdTo", "2024-2-01"}},
                 {{"createdFrom", "2025-01-01"}, {"createdTo", "2024-01-01"}}}) {
            QVERIFY(sc.validateTrackFilters(filters).contains("error"));
            QVERIFY(!sc.applyTrackFilters(filters));
            QVERIFY(net.requests.isEmpty());
        }
        QVERIFY(!sc.validateTrackFilters({{"bpmFrom", "0"}, {"bpmTo", "0"}, {"createdFrom", "2024-02-29"}}).contains("error"));
        QVERIFY(sc.applyTrackFilters({{"genres", " House, Pop "}, {"tags", " piano, chill "},
            {"durationFrom", "60"}, {"durationTo", "300"}, {"bpmFrom", "120"}, {"bpmTo", "140"},
            {"createdFrom", "2024-02-29"}, {"createdTo", "2024-03-01"}, {"fullOnly", true}}));
        QCOMPARE(sc.m_trackFilters.queryItemValue("genres"), QString("House,Pop"));
        QCOMPARE(sc.m_trackFilters.queryItemValue("tags"), QString("piano,chill"));
        QCOMPARE(sc.m_trackFilters.queryItemValue("duration[from]"), QString("60000"));
        QCOMPARE(sc.m_trackFilters.queryItemValue("duration[to]"), QString("300000"));
        QCOMPARE(sc.m_trackFilters.queryItemValue("bpm[to]"), QString("140"));
        QCOMPARE(sc.m_trackFilters.queryItemValue("created_at[to]"), QString("2024-03-01 23:59:59"));
        QCOMPARE(sc.m_trackFilters.queryItemValue("access"), QString("playable"));
        QVERIFY(!sc.applyTrackFilters({{"durationFrom", "bad"}}));
        QVERIFY(!sc.m_trackFilters.isEmpty());
        QVERIFY(sc.applyTrackFilters({}));
        QVERIFY(sc.m_trackFilters.isEmpty());
    }
    void advancedTrackSearchPaginationAndStaleResponses() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false); sc.m_access = "test-access";
        sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        net.responses = {
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"}]}", 150},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:2\"}],\"next_href\":\"https://api.soundcloud.com/tracks?cursor=next\"}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:3\"}]}"},
            {200, "{\"collection\":[]}"}, {200, "{\"collection\":[]}"}, {200, "{\"collection\":[]}"}};
        QVERIFY(sc.applyTrackFilters({{"genres", "Rock"}})); sc.browse("tracks", "a & b");
        QVERIFY(sc.applyTrackFilters({{"genres", "House"}, {"durationFrom", "60"}, {"fullOnly", true}}));
        sc.browse("tracks", "a & b");
        sc.browse("tracks", "a & b"); // Repeated submissions share the active listing.
        QCOMPARE(net.requests.size(), 2);
        QTRY_VERIFY(!sc.state()["busy"].toBool());
        QTest::qWait(180);
        QCOMPARE(sc.state()["tracks"].toList().first().toMap()["urn"].toString(), QString("soundcloud:tracks:2"));
        // Editing the next draft cannot change the submitted search's pagination.
        QVERIFY(sc.applyTrackFilters({{"genres", "Jazz"}})); sc.more();
        QTRY_COMPARE(net.requests.size(), 3);
        QTRY_VERIFY(!sc.state()["busy"].toBool());
        const QUrlQuery next(net.requests[2].url());
        QCOMPARE(next.queryItemValue("genres"), QString("House"));
        QCOMPARE(next.queryItemValue("q"), QString("a & b"));
        QCOMPARE(next.queryItemValue("duration[from]"), QString("60000"));
        QCOMPARE(next.queryItemValue("access"), QString("playable"));
        QCOMPARE(next.queryItemValue("cursor"), QString("next"));
        QCOMPARE(sc.state()["tracks"].toList().size(), 2);
        sc.browse("users", "person"); QTRY_VERIFY(!sc.state()["busy"].toBool());
        QVERIFY(!QUrlQuery(net.requests.last().url()).hasQueryItem("genres"));
        sc.browse("sets", "mix"); QTRY_VERIFY(!sc.state()["busy"].toBool());
        QVERIFY(!QUrlQuery(net.requests.last().url()).hasQueryItem("genres"));
        QVERIFY(sc.applyTrackFilters({})); sc.browse("tracks", "reset");
        QTRY_VERIFY(!sc.state()["busy"].toBool());
        QVERIFY(!QUrlQuery(net.requests.last().url()).hasQueryItem("genres"));
        QVERIFY(!QUrlQuery(net.requests.last().url()).hasQueryItem("access"));
    }
    void advancedTrackSearchRejectsStaleFailureAndSupportsEmptyQuery() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false); sc.m_access = "test-access";
        sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        net.responses = {{404, "{}", 100},
                         {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:2\"}]}"}};
        QVERIFY(sc.applyTrackFilters({{"bpmFrom", "120"}})); sc.browse("tracks", "old");
        QVERIFY(sc.applyTrackFilters({{"tags", "piano & voice, ambient"}})); sc.browse("tracks", "");
        QTRY_VERIFY(!sc.state()["busy"].toBool()); QTest::qWait(150);
        QCOMPARE(net.requests.size(), 2);
        const QUrlQuery query(net.requests.last().url());
        QCOMPARE(query.queryItemValue("tags"), QString("piano & voice,ambient"));
        QVERIFY(!query.hasQueryItem("q")); QVERIFY(!query.hasQueryItem("bpm[from]"));
        QVERIFY(sc.state()["error"].toString().isEmpty());
        QCOMPARE(sc.state()["tracks"].toList().first().toMap()["urn"].toString(), QString("soundcloud:tracks:2"));
        QVERIFY(sc.applyTrackFilters({})); sc.browse("tracks", "");
        QVERIFY(sc.state()["tracks"].toList().isEmpty());
        QVERIFY(!sc.state()["hasMore"].toBool()); sc.more();
        QCOMPARE(net.requests.size(), 2);
    }
    void trackCommentsRejectStalePagesAndPublishSafely() {
        Network net;
        SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false); sc.m_access = "test-access";
        sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.set("signedIn", true);
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:comments:1\",\"body\":\"old\"}]}", 80},
                         {200, "{\"collection\":[{\"urn\":\"soundcloud:comments:2\",\"body\":\"new\",\"timestamp\":\"12000\"},{\"urn\":\"soundcloud:comments:3\",\"body\":\"untimed\",\"timestamp\":null}],\"next_href\":\"https://api.soundcloud.com/tracks/soundcloud%3Atracks%3A2/comments?cursor=next\"}"}};
        sc.loadComments("soundcloud:tracks:1"); sc.loadComments("soundcloud:tracks:2");
        QTRY_VERIFY(!sc.commentsState()["busy"].toBool());
        QTest::qWait(100);
        auto entries = sc.commentsState()["items"].toList();
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].toMap()["body"].toString(), QString("new"));
        QCOMPARE(entries[0].toMap()["timestamp"].toLongLong(), 12000);
        QCOMPARE(entries[1].toMap()["timestamp"].toLongLong(), -1);
        // Derive the pagination URL from the actual canonical endpoint.
        auto next = net.requests.last().url(); QUrlQuery query; query.addQueryItem("cursor", "next"); next.setQuery(query);
        sc.m_commentsNext = next.toString();
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:comments:2\"},{\"urn\":\"soundcloud:comments:4\",\"body\":\"next\",\"timestamp\":0}],\"next_href\":\"https://evil.example/comments\"}"}};
        sc.loadComments("soundcloud:tracks:2", true);
        QTRY_VERIFY(!sc.commentsState()["busy"].toBool());
        QCOMPARE(sc.commentsState()["items"].toList().size(), 3);
        QVERIFY(!sc.commentsState()["hasMore"].toBool());
        net.responses = {{201, "{\"urn\":\"soundcloud:comments:5\",\"body\":\"hello\",\"timestamp\":15000}"}};
        QSignalSpy posted(&sc, &SoundCloud::commentPosted);
        sc.postComment("soundcloud:tracks:2", " hello ", 15000);
        sc.postComment("soundcloud:tracks:2", "duplicate", 15000);
        QTRY_COMPARE(posted.size(), 1);
        QCOMPARE(net.methods.last(), QByteArray("POST"));
        const auto body = QJsonDocument::fromJson(net.bodies.last()).object()["comment"].toObject();
        QCOMPARE(body["body"].toString(), QString("hello")); QCOMPARE(body["timestamp"].toInt(), 15000);
        net.responses = {{429, "{}"}};
        sc.postComment("soundcloud:tracks:2", "retry later");
        QTRY_VERIFY(!sc.commentsState()["sending"].toBool());
        QVERIFY(!sc.commentsState()["postError"].toString().isEmpty());
        QCOMPARE(posted.size(), 1);
        const auto requestCount = net.requests.size();
        sc.postComment("soundcloud:tracks:99", "wrong track");
        sc.postComment("soundcloud:tracks:2", QString(1001, 'x'));
        QCOMPARE(net.requests.size(), requestCount);
        sc.loadComments("soundcloud:tracks:3");
        sc.logout(); QTest::qWait(30);
        QVERIFY(sc.commentsState().isEmpty());
    }

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
        QCOMPARE(sc.state()["playlistTracks"].toList().size(), 3);
        QCOMPARE(sc.state()["playlistTracks"].toList()[0].toMap()["urn"], sc.state()["playlistTracks"].toList()[1].toMap()["urn"]);
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
    void init() { QSettings().remove("player/playbackQuality"); QSettings().remove("player/preloadEnabled"); QSettings().remove("player/preloadWifiOnly"); qputenv("SCRIP_AUTH_MODE", "local"); QSettings().remove("player/repeatMode"); QSettings().remove("player/repeat"); QSettings().remove("player/shuffle"); prepare(); }
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
        Network net; net.responses = {guest(), {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"}]}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:2\"}]}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:3\"}]}"}};
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
    void collectionSwitchKeepsVisibleDelegates() {
        TrackModel model;
        const QVariantMap first{{"urn", "soundcloud:tracks:1"}, {"title", "Liked"}};
        const QVariantMap second{{"urn", "soundcloud:tracks:2"}, {"title", "Recent"}};
        model.update({first});
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("tracks", &model);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            ListView {
                width: 400; height: 400
                model: tracks
                delegate: Item {
                    required property var modelData
                    width: 400; height: 58
                    property string title: modelData.title
                }
            }
        )", QUrl());
        QScopedPointer<QObject> view(component.create());
        QVERIFY2(view, qPrintable(component.errorString()));
        QVERIFY(QMetaObject::invokeMethod(view.data(), "forceLayout"));
        QPointer<QObject> delegate = view->property("currentItem").value<QObject *>();
        QVERIFY(delegate);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        for (int i = 0; i < 10; ++i) {
            model.update({second, first});
            QVERIFY(QMetaObject::invokeMethod(view.data(), "forceLayout"));
            QCOMPARE(view->property("currentItem").value<QObject *>(), delegate.data());
            QCOMPARE(delegate->property("title").toString(), QString("Recent"));
            model.update({first});
            QVERIFY(QMetaObject::invokeMethod(view.data(), "forceLayout"));
            QCOMPARE(view->property("currentItem").value<QObject *>(), delegate.data());
            QCOMPARE(delegate->property("title").toString(), QString("Liked"));
        }
        QCOMPARE(resets.size(), 0);
        model.update({});
        QCOMPARE(model.rowCount(), 0);
    }
    void shuffleVisitsEveryRowOnceAndStops_data() {
        QTest::addColumn<QList<int>>("order");
        QTest::newRow("forward") << QList<int>{1, 2, 3};
        QTest::newRow("reverse") << QList<int>{3, 2, 1};
        QTest::newRow("mixed") << QList<int>{2, 1, 3};
        QTest::newRow("132") << QList<int>{1, 3, 2};
        QTest::newRow("231") << QList<int>{2, 3, 1};
        QTest::newRow("312") << QList<int>{3, 1, 2};
    }
    void shuffleVisitsEveryRowOnceAndStops() {
        QFETCH(QList<int>, order);
        Network net; SoundCloud sc(nullptr, &net, secrets);
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}, {"access", "blocked"}};
        const QVariantMap b{{"urn", "soundcloud:tracks:2"}, {"access", "blocked"}};
        sc.playMix({a, a, b, a}, 0); sc.setShuffle(true);
        QSet<int> heard{sc.m_queueIds[0]};
        for (int index : order) {
            sc.m_plannedId = sc.m_queueIds[index];
            QCOMPARE(sc.nextQueueIndex(), index);
            QCOMPARE(sc.nextQueueIndex(true), index); // Peeking cannot consume the plan.
            sc.next(); QCOMPARE(sc.m_queueIndex, index);
            QVERIFY(!heard.contains(sc.m_queueIds[index])); heard.insert(sc.m_queueIds[index]);
        }
        QCOMPARE(heard.size(), 4); QCOMPARE(sc.nextQueueIndex(), -1);
        const auto current = sc.state()["current"];
        sc.next(); QVERIFY(!sc.state()["playbackActive"].toBool());
        QCOMPARE(sc.state()["current"], current); QCOMPARE(sc.nextQueueIndex(), -1);
        sc.setRepeatMode(2); sc.m_plannedId = sc.m_queueIds[0]; sc.next();
        QCOMPARE(sc.m_queueIndex, 0); QCOMPARE(sc.m_visited.size(), 1);
    }
    void queueMutationsPreservePlannedRowAndRemainingTraversal() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}, {"access", "blocked"}};
        const QVariantMap b{{"urn", "soundcloud:tracks:2"}, {"access", "blocked"}};
        sc.playMix({a, b, a, b}); sc.setShuffle(true);
        const int planned = sc.m_queueIds[3]; sc.m_plannedId = planned;
        sc.moveQueue(3, 1); QCOMPARE(sc.m_queueIds[sc.nextQueueIndex()], planned);
        sc.removeQueue(3); QCOMPARE(sc.m_queueIds[sc.nextQueueIndex()], planned);
        sc.removeQueue(sc.nextQueueIndex()); QVERIFY(!sc.m_queueIds.contains(planned));
        QCOMPARE(sc.nextQueueIndex(), 1); sc.next();
        QCOMPARE(sc.nextQueueIndex(), -1);
        sc.enqueueTracks({a, a});
        const int firstAdded = sc.m_queueIds[sc.m_queueIndex + 1]; sc.next();
        QCOMPARE(sc.m_queueIds[sc.m_queueIndex], firstAdded);
        const int lastAdded = sc.nextQueueIndex(); QVERIFY(lastAdded >= 0);
        QVERIFY(!sc.m_visited.contains(sc.m_queueIds[lastAdded])); sc.next();
        QCOMPARE(sc.nextQueueIndex(), -1);
        sc.setRepeatMode(2);
        sc.m_plannedId = sc.m_history[sc.m_historyCursor - 1];
        const auto visited = sc.m_visited;
        sc.previous(); QCOMPARE(sc.m_visited, visited); sc.next(); QCOMPARE(sc.m_visited, visited);
        const int expected = sc.nextQueueIndex();
        const auto snapshot = sc.queueOrderSnapshot();
        Network otherNet; SoundCloud restored(nullptr, &otherNet, secrets);
        restored.m_queue = sc.m_queue; restored.m_queueIndex = sc.m_queueIndex;
        restored.restoreQueueOrder(snapshot);
        QCOMPARE(restored.queueOrderSnapshot(), snapshot);
        restored.startTrack(restored.m_queue[restored.m_queueIndex].toMap());
        QCOMPARE(restored.nextQueueIndex(), expected);
    }
    void repeatDistinguishesNaturalAndManualTransitions() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}, {"access", "blocked"}};
        const QVariantMap b{{"urn", "soundcloud:tracks:2"}, {"access", "blocked"}};
        sc.playMix({a, b}); sc.setRepeat(true);
        QCOMPARE(sc.state()["repeatMode"].toInt(), 1);
        QCOMPARE(sc.nextQueueIndex(true), 0); QCOMPARE(sc.nextQueueIndex(), 1);
        sc.advancePlayback(true); QCOMPARE(sc.m_queueIndex, 0);
        sc.next(); QCOMPARE(sc.m_queueIndex, 1);
        sc.advancePlayback(true); QCOMPARE(sc.m_queueIndex, 1);
        sc.next(); QVERIFY(!sc.state()["playbackActive"].toBool());
        sc.setRepeatMode(2); QVERIFY(!sc.state()["repeat"].toBool());
        sc.advancePlayback(true); QCOMPARE(sc.m_queueIndex, 0);
        sc.setRepeat(false); QCOMPARE(sc.state()["repeatMode"].toInt(), 0);
        sc.playMix({a}); QCOMPARE(sc.nextQueueIndex(), -1);
        sc.setRepeatMode(2); QCOMPARE(sc.nextQueueIndex(), 0);
        sc.setShuffle(true); QCOMPARE(sc.nextQueueIndex(), 0);
        sc.removeQueue(0); QCOMPARE(sc.nextQueueIndex(), -1);
    }
    void queueHistorySurvivesBackForwardAndEdits() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}, {"access", "blocked"}};
        const QVariantMap b{{"urn", "soundcloud:tracks:2"}, {"access", "blocked"}};
        sc.playMix({a, a, b}); sc.setShuffle(true);
        sc.m_plannedId = sc.m_queueIds[2]; sc.next();
        sc.m_plannedId = sc.m_queueIds[1]; sc.next();
        const auto history = sc.m_history;
        sc.previous(); QCOMPARE(sc.m_queueIndex, 2);
        sc.previous(); QCOMPARE(sc.m_queueIndex, 0);
        QCOMPARE(sc.m_visited.size(), 3); // Going back does not start a fresh shuffle cycle.
        sc.moveQueue(2, 0); QCOMPARE(sc.nextQueueIndex(), 0);
        sc.next(); QCOMPARE(sc.m_queueIndex, 0);
        sc.next(); QCOMPARE(sc.m_queueIndex, 2); QCOMPARE(sc.m_history, history);
        sc.previous(); sc.removeQueue(2);
        QCOMPARE(sc.nextQueueIndex(), -1);
        sc.enqueue(a); const int added = sc.m_queueIds[sc.m_queueIndex + 1];
        sc.next(); QCOMPARE(sc.m_queueIds[sc.m_queueIndex], added);
        sc.clearQueueAfterCurrent();
        QVERIFY(sc.m_queueIds.size() == sc.m_queue.size());
        sc.deduplicateQueue();
        QVERIFY(sc.m_queueIds.size() == sc.m_queue.size());
        for (int id : sc.m_history) QVERIFY(sc.m_queueIds.contains(id));
    }
    void queuePlanMatchesPreloadAndForcedNext() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}, {"access", "blocked"}};
        const QVariantMap b{{"urn", "soundcloud:tracks:2"}, {"access", "blocked"}};
        sc.playMix({a, a, b}); sc.setShuffle(true);
        sc.m_plannedId = sc.m_queueIds[2];
        const int planned = sc.nextQueueIndex(true);
        sc.m_preloadIndex = planned; sc.m_preloadUrn = "soundcloud:tracks:2";
        sc.next(); QCOMPARE(sc.m_queueIndex, planned);
        sc.previous(); sc.queueNext(1);
        QCOMPARE(sc.nextQueueIndex(true), sc.m_queueIndex + 1);
        sc.next(); QCOMPARE(sc.state()["current"].toMap(), a);
        // A stale prepared index must never override the current order.
        sc.setShuffle(false); sc.setRepeatMode(2);
        sc.m_preloadIndex = 0; sc.m_preloadUrn = "soundcloud:tracks:1";
        const int expected = sc.nextQueueIndex(); sc.next(); QCOMPARE(sc.m_queueIndex, expected);
        sc.m_waveMode = "library"; sc.setRepeatMode(1); sc.setShuffle(true);
        QCOMPARE(sc.nextQueueIndex(true), sc.m_queueIndex + 1 < sc.m_queue.size() ? sc.m_queueIndex + 1 : -1);
    }
    void preparedAudioMatchesTransitionAndModeChangesKeepSound_data() {
        QTest::addColumn<bool>("exhausted");
        QTest::newRow("manual-handoff") << false;
        QTest::newRow("manual-end-during-ramp") << true;
    }
    void preparedAudioMatchesTransitionAndModeChangesKeepSound() {
        QFETCH(bool, exhausted);
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        QTemporaryDir files; QFile wav(files.filePath("order.wav")); QVERIFY(wav.open(QIODevice::WriteOnly));
        QDataStream data(&wav); data.setByteOrder(QDataStream::LittleEndian);
        constexpr int bytes = 44100 * 2 * 10;
        data.writeRawData("RIFF", 4); data << quint32(bytes + 36);
        data.writeRawData("WAVEfmt ", 8); data << quint32(16) << quint16(1) << quint16(1)
            << quint32(44100) << quint32(88200) << quint16(2) << quint16(16);
        data.writeRawData("data", 4); data << quint32(bytes); wav.write(QByteArray(bytes, '\0')); wav.close();
        const QUrl source = QUrl::fromLocalFile(wav.fileName());
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}}, b{{"urn", "soundcloud:tracks:2"}};
        sc.m_queue = {a, a, b}; sc.m_queueIndex = 0; sc.resetQueueOrder(); sc.recordQueueEntry();
        sc.set("current", a); sc.player().setSource(source);
        QTRY_VERIFY(sc.player().isSeekable());
        sc.player().setPosition(4500);
        QTRY_VERIFY(sc.position() > 3000);
        QVERIFY(playerCommandHandler(sc)("player.previous", {}).isEmpty());
        QCOMPARE(sc.m_queueIndex, 0); QTRY_COMPARE(sc.player().position(), 0);
        auto *original = sc.m_activePlayer;
        sc.setShuffle(true); sc.m_plannedId = sc.m_queueIds[2];
        sc.prepareNext(); QCOMPARE(sc.m_preloadIndex, 2);
        QTRY_VERIFY(net.responses.isEmpty()); QTest::qWait(20);
        sc.m_preloadPlayer->setSource(source);
        QTRY_VERIFY(sc.m_preloadPlayer->mediaStatus() == QMediaPlayer::LoadedMedia || sc.m_preloadPlayer->mediaStatus() == QMediaPlayer::BufferedMedia);
        sc.next(); QCOMPARE(sc.m_queueIndex, 2); QVERIFY(sc.m_activePlayer != original);
        QVERIFY(sc.m_startupUsedPreload); QCOMPARE(sc.player().source(), source);
        sc.pausePlayback();
        // Prepare the only unvisited row, then use the same plan for crossfade.
        sc.prepareNext(); QCOMPARE(sc.m_preloadIndex, 1);
        QTest::qWait(20); sc.m_preloadPlayer->setSource(source);
        QTRY_VERIFY(sc.m_preloadPlayer->mediaStatus() == QMediaPlayer::LoadedMedia || sc.m_preloadPlayer->mediaStatus() == QMediaPlayer::BufferedMedia);
        sc.beginCrossfade(); QVERIFY(sc.m_crossfadeStarted); QCOMPARE(sc.m_queueIndex, 1);
        const auto history = sc.m_history;
        auto *incoming = sc.m_activePlayer; auto *outgoing = sc.m_preloadPlayer;
        sc.setRepeatMode(1); sc.setShuffle(false);
        QVERIFY(sc.m_crossfadeStarted); QCOMPARE(sc.m_activePlayer, incoming);
        QCOMPARE(sc.player().source(), source); QCOMPARE(outgoing->source(), source);
        QCOMPARE(sc.m_history, history);
        if (exhausted) sc.moveQueue(sc.m_queueIndex, int(sc.m_queue.size()) - 1);
        const auto selected = sc.state()["current"];
        const auto transitions = sc.m_history;
        sc.next(); // Manual advancement also releases the outgoing side of a ramp.
        QVERIFY(!sc.m_crossfadeStarted); QVERIFY(outgoing->source().isEmpty());
        QCOMPARE(outgoing->playbackState(), QMediaPlayer::StoppedState);
        if (exhausted) {
            QVERIFY(!sc.state()["playbackActive"].toBool());
            QCOMPARE(sc.state()["current"], selected);
            QCOMPARE(sc.m_history, transitions);
            QCOMPARE(sc.m_audio.volume(), float(sc.state()["volume"].toReal()));
            sc.resumePlayback();
            QCOMPARE(outgoing->playbackState(), QMediaPlayer::StoppedState);
            QVERIFY(!sc.m_crossfadeStarted);
        }
        sc.pausePlayback();
    }
    void queueOrderAndModesRestoreWithSession() {
        QSettings settings; settings.remove("playback");
        const QVariantMap profile{{"urn", "soundcloud:users:order"}};
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}, {"access", "blocked"}};
        const QVariantMap b{{"urn", "soundcloud:tracks:2"}, {"access", "blocked"}};
        QVariantMap order;
        {
            Network net; SoundCloud sc(nullptr, &net, secrets);
            sc.playMix({a, a, b}); sc.setShuffle(true); sc.setRepeatMode(2);
            sc.m_plannedId = sc.m_queueIds[2]; sc.next(); sc.previous();
            sc.set("profile", profile); sc.set("signedIn", true); sc.m_restoreChecked = true;
            sc.savePlayback(); order = sc.queueOrderSnapshot();
        }
        {
            Network net; SoundCloud sc(nullptr, &net, secrets);
            sc.set("signedIn", true); sc.restorePlayback(profile);
            QCOMPARE(sc.state()["repeatMode"].toInt(), 2); QVERIFY(sc.state()["shuffle"].toBool());
            QCOMPARE(sc.queueOrderSnapshot(), order);
            QCOMPARE(sc.nextQueueIndex(), 2); sc.next(); QCOMPARE(sc.m_queueIndex, 2);
        }
        settings.remove("playback"); settings.remove("player/repeatMode");
        settings.setValue("player/repeat", true);
        Network net; SoundCloud legacy(nullptr, &net, secrets);
        QCOMPARE(legacy.state()["repeatMode"].toInt(), 1); QVERIFY(legacy.state()["repeat"].toBool());
        legacy.m_queue = {a, b}; legacy.m_queueIndex = 0;
        legacy.restoreQueueOrder({{"ids", QVariantList{7, 7}}});
        QCOMPARE(QSet<int>(legacy.m_queueIds.begin(), legacy.m_queueIds.end()).size(), 2);
        QCOMPARE(legacy.m_history.size(), 1);
        settings.remove("player/repeat"); settings.remove("player/repeatMode"); settings.remove("player/shuffle");
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
    void isolatedSocialLayouts_data() {
        QTest::addColumn<QString>("file"); QTest::addColumn<bool>("mobile"); QTest::addColumn<int>("viewportWidth"); QTest::addColumn<bool>("dark");
        QTest::newRow("queue-phone") << QString("QueueActions") << true << 360 << true;
        QTest::newRow("queue-small") << QString("QueueActions") << true << 320 << false;
        QTest::newRow("queue-desktop") << QString("QueueActions") << false << 1000 << true;
        QTest::newRow("people-desktop") << QString("PeoplePopup") << false << 1000 << true;
        QTest::newRow("people-android") << QString("PeoplePopup") << true << 360 << true;
        QTest::newRow("editor-desktop") << QString("PlaylistEditor") << false << 1000 << true;
        QTest::newRow("editor-android") << QString("PlaylistEditor") << true << 360 << true;
        QTest::newRow("people-small-light") << QString("PeoplePopup") << true << 320 << false;
        QTest::newRow("editor-small-light") << QString("PlaylistEditor") << true << 320 << false;
        QTest::newRow("editor-compact-desktop") << QString("PlaylistEditor") << false << 760 << false;
    }
    void isolatedSocialLayouts() {
        QFETCH(QString, file); QFETCH(bool, mobile); QFETCH(int, viewportWidth); QFETCH(bool, dark);
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("signedIn", true); sc.set("profile", QVariantMap{{"urn", "soundcloud:users:1"}}); sc.set("followingsReady", true);
        const QVariantMap user{{"urn", "soundcloud:users:1"}, {"username", "SoundCloud"}};
        QJsonArray tracks, people;
        for (int i = 0; i < 12; ++i) {
            tracks.append(QJsonObject{{"urn", QString("soundcloud:tracks:%1").arg(i)}, {"title", QString("Track %1 — long track title for layout checking").arg(i + 1)}, {"user", QJsonObject{{"username", "Artist"}}}});
            people.append(QJsonObject{{"urn", QString("soundcloud:users:%1").arg(i + 2)}, {"username", QString("Artist %1").arg(i + 1)}, {"followers_count", 12500 + i}});
        }
        const QVariantMap playlist{{"urn", "soundcloud:playlists:2"}, {"title", "Evening collection"}, {"description", "A selection for the evening."}, {"sharing", "private"}, {"track_count", tracks.size()}, {"user", user}};
        if (file == "PlaylistEditor") net.responses = {{200, QJsonDocument::fromVariant(playlist).toJson()}, {200, QJsonDocument(QJsonObject{{"collection", tracks}}).toJson()}};
        else net.responses = {{200, QJsonDocument(QJsonObject{{"collection", people}}).toJson()}};
        QQmlEngine engine; engine.rootContext()->setContextProperty("sc", &sc);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) { for (const auto &error : errors) warnings.append(error.toString()); });
        QQmlComponent hostComponent(&engine); hostComponent.setData("import QtQuick\nWindow {\n    width: 900; height: 900; visible: false; color: \"#181818\"\n    property color panel: \"#242424\"\n    property color surface: \"#202020\"\n    property color raised: \"#343434\"\n    property color ink: \"#eeeeee\"\n    property color muted: \"#999999\"\n    property color accent: \"#f43756\"\n    property bool dark: true\n    property int motion: 0\n    property real coverRadius: 16\n    property real artworkRadius: 12\n    function foreground(color) { return \"white\" }\n    function safeAreaBottom(item) { return 0 }\n    function cover(item) { return \"\" }\n    function time(ms) { return \"0:00\" }\n    function openTrackActions(track) {}\n    function openProfile(user) {}\n    function refreshPage(force) {}\n}", QUrl());
        std::unique_ptr<QObject> host(hostComponent.create()); QVERIFY2(host, qPrintable(hostComponent.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(host.get()); QVERIFY(window); host->setProperty("motion", 180); window->resize(viewportWidth, mobile ? (viewportWidth == 320 ? 680 : 800) : 940);
        if (!dark) {
            host->setProperty("dark", false); host->setProperty("panel", QColor("#fafafa")); host->setProperty("raised", QColor("#ededed"));
            host->setProperty("ink", QColor("#242424")); host->setProperty("muted", QColor("#737373")); window->setColor(QColor("#e5e5e5"));
        }
        window->show();
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/" + file + ".qml"));
        std::unique_ptr<QObject> surface(component.createWithInitialProperties({{"host", QVariant::fromValue(host.get())}, {"mobile", mobile}, {"parent", QVariant::fromValue(window->contentItem())}}));
        QVERIFY2(surface, qPrintable(component.errorString()));
        if (file == "PlaylistEditor") {
            QVERIFY(QMetaObject::invokeMethod(surface.get(), "begin", Q_ARG(QVariant, playlist)));
            QTRY_VERIFY(surface->property("initialized").toBool());
            auto *title = surface->findChild<QQuickItem *>("playlistTitleField"); QVERIFY(title);
            QCOMPARE(title->property("text").toString(), playlist.value("title").toString());
            QVERIFY(title->width() > 200);
            title->setProperty("text", "Edited title");
            QVERIFY(surface->property("dirty").toBool());
            QTest::qWait(220);
            std::function<QQuickItem *(QQuickItem *, const QString &)> findItem = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
                if (item->objectName() == name) return item;
                for (auto *child : item->childItems()) if (auto *found = findItem(child, name)) return found;
                return nullptr;
            };
            auto *list = findItem(window->contentItem(), "playlistEditorTracks"); QVERIFY(list);
            auto *handle = findItem(window->contentItem(), "playlistDragHandle0"); QVERIFY(handle);
            const auto start = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
            const auto end = start + QPoint(0, 128);
            QSignalSpy moved(sc.playlistEditModel(), &QAbstractItemModel::rowsMoved);
            if (mobile) {
                auto *device = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
                auto touch = QTest::touchEvent(window, device, false);
                touch.press(0, start, window).commit();
                touch.move(0, start + QPoint(0, 32), window).commit();
                QCoreApplication::processEvents();
                touch.move(0, end, window).commit();
                QTRY_COMPARE(list->property("dragTo").toInt(), 2);
                touch.release(0, end, window).commit();
            } else {
                QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
                QTest::mouseMove(window, start + QPoint(0, 32), 20);
                QTest::mouseMove(window, end, 20);
                QTRY_COMPARE(list->property("dragTo").toInt(), 2);
                QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
            }
            QTRY_COMPARE(moved.count(), 1);
            QCOMPARE(sc.state()["playlistDraft"].toMap()["tracks"].toList()[2].toMap()["urn"].toString(), QString("soundcloud:tracks:0"));
            QVERIFY(!list->property("settling").toBool());
            // Keep dragging near the bottom: the viewport scrolls while the
            // original delegate can leave its cache, without losing the grab.
            handle = findItem(window->contentItem(), "playlistDragHandle2"); QVERIFY(handle);
            const auto edgeStart = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
            const auto edge = list->mapToScene(QPointF(list->width() - 30, list->height() - 8)).toPoint();
            const auto beforeScroll = list->property("contentY").toReal();
            if (mobile) {
                auto *device = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
                auto touch = QTest::touchEvent(window, device, false);
                touch.press(0, edgeStart, window).commit();
                touch.move(0, edgeStart + QPoint(0, 24), window).commit();
                touch.move(0, edge, window).commit();
                QTRY_VERIFY(list->property("contentY").toReal() > beforeScroll);
                touch.release(0, edge, window).commit();
            } else {
                QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, edgeStart);
                QTest::mouseMove(window, edgeStart + QPoint(0, 24), 20);
                QTest::mouseMove(window, edge, 30);
                QTRY_VERIFY(list->property("contentY").toReal() > beforeScroll);
                QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, edge);
            }
            QTRY_COMPARE(moved.count(), 2);
            QVERIFY(list->property("contentY").toReal() > beforeScroll);
            list->setProperty("contentY", 0);
        } else if (file == "QueueActions") {
            sc.set("queue", QVariantList{QVariantMap{{"urn", "soundcloud:tracks:1"}}});
            QVERIFY(QMetaObject::invokeMethod(surface.get(), "begin"));
            QTest::qWait(200);
            QVERIFY(surface->property("width").toReal() <= viewportWidth);
            auto *field = surface->findChild<QQuickItem *>("queuePlaylistTitle"); QVERIFY(field);
            field->setProperty("text", "Queue title");
            sc.set("queuePlaylistError", "API error");
            QCOMPARE(field->property("text").toString(), QString("Queue title"));
            QTRY_VERIFY(field->width() >= 200); QVERIFY(field->height() >= 44);
        } else {
            QVERIFY(QMetaObject::invokeMethod(surface.get(), "begin", Q_ARG(QVariant, user), Q_ARG(QVariant, QString("followers"))));
            QTRY_COMPARE(sc.state()["people"].toList().size(), 12);
        }
        QTest::qWait(100);
        const auto shot = window->grabWindow(); QVERIFY(!shot.isNull());
        QVERIFY(shot.save(QString(SCRIP_SOURCE_DIR) + "/build/social-" + file + (mobile ? "-android" : "-desktop") + (dark ? "" : "-light-" + QString::number(viewportWidth)) + ".png"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    void socialComponentsCreateWithoutBindingErrors_data() {
        QTest::addColumn<QString>("file"); QTest::addColumn<bool>("mobile");
        for (const auto &file : {"SurfaceButton", "EngagementAction", "PeoplePopup", "ProfileExtras", "PlaylistEditor", "PlaylistPopup"}) {
            QTest::newRow(qPrintable(QString(file) + "-desktop")) << QString(file) << false;
            QTest::newRow(qPrintable(QString(file) + "-android")) << QString(file) << true;
        }
    }
    void socialComponentsCreateWithoutBindingErrors() {
        QFETCH(QString, file); QFETCH(bool, mobile);
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        QQmlEngine engine; engine.rootContext()->setContextProperty("sc", &sc);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) { for (const auto &error : errors) warnings.append(error.toString()); });
        QQmlComponent hostComponent(&engine);
        hostComponent.setData("import QtQuick\nItem {\n    width: 900; height: 900\n    property color panel: \"#242424\"\n    property color surface: \"#202020\"\n    property color raised: \"#343434\"\n    property color ink: \"#eeeeee\"\n    property color muted: \"#999999\"\n    property color accent: \"#f43756\"\n    property bool dark: true\n    property int motion: 0\n    property real coverRadius: 16\n    property real artworkRadius: 12\n    function foreground(color) { return \"white\" }\n    function safeAreaBottom(item) { return 0 }\n    function cover(item) { return \"\" }\n    function time(ms) { return \"0:00\" }\n    function openTrackActions(track) {}\n    function openProfile(user) {}\n    function refreshPage(force) {}\n}", QUrl());
        std::unique_ptr<QObject> host(hostComponent.create());
        QVERIFY2(host, qPrintable(hostComponent.errorString()));
        auto *hostItem = qobject_cast<QQuickItem *>(host.get()); QVERIFY(hostItem);
        hostItem->setWidth(mobile ? 360 : 900);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/" + file + ".qml"));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"host", QVariant::fromValue(host.get())}, {"mobile", mobile}, {"parent", QVariant::fromValue(hostItem)}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        QCoreApplication::processEvents();
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    void engagementPeoplePaginationErrorsAndClose() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        const QVariantMap track{{"urn", "soundcloud:tracks:1"}}, playlist{{"urn", "soundcloud:playlists:1"}};
        const int before = net.requests.size();
        sc.loadPeople(playlist, "favoriters"); QCOMPARE(net.requests.size(), before);
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:users:1\"}],\"next_href\":\"https://api.soundcloud.com/tracks/1/favoriters?cursor=2\"}"}};
        sc.loadPeople(track, "favoriters");
        QTRY_VERIFY(!sc.state()["peopleBusy"].toBool());
        QCOMPARE(net.requests.size(), before + 1); QVERIFY(sc.state()["peopleHasMore"].toBool());
        QCOMPARE(sc.state()["people"].toList().size(), 1);
        net.responses = {{400, "{}"}};
        sc.loadPeople(track, "favoriters", true);
        QTRY_VERIFY(!sc.state()["peopleBusy"].toBool());
        QVERIFY(!sc.state()["peopleError"].toString().isEmpty());
        QCOMPARE(sc.state()["people"].toList().size(), 1);
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:users:1\"},{\"urn\":\"soundcloud:users:2\"}]}"}};
        sc.loadPeople(track, "favoriters", true);
        QTRY_COMPARE(sc.state()["people"].toList().size(), 2);
        QVERIFY(!sc.state()["peopleHasMore"].toBool());
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:users:9\"}]}", 80}};
        sc.loadPeople(playlist, "reposters"); sc.closePeople();
        QTest::qWait(120); QVERIFY(sc.state()["people"].toList().isEmpty());
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:users:9\"}]}", 80}};
        sc.loadPeople(track, "reposters"); sc.set("signedIn", false);
        QTest::qWait(120); QVERIFY(sc.state()["people"].toList().isEmpty());
    }
    void pluginNavigationCommandRoute() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}, {"access", "blocked"}};
        const QVariantMap b{{"urn", "soundcloud:tracks:2"}, {"access", "blocked"}};
        const auto dataHome = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_DATA_HOME", directory.path().toUtf8());
        const auto restoreDataHome = qScopeGuard([&] {
            if (dataHome.isNull()) qunsetenv("XDG_DATA_HOME"); else qputenv("XDG_DATA_HOME", dataHome);
        });
        Plugins host;
        host.setPlayerCommand(playerCommandHandler(sc));
        QJsonArray buttons;
        for (const auto &name : {"player.next", "player.previous"})
            buttons.append(QJsonObject{{"label", name}, {"action", QJsonObject{{"command", name}}}});
        const QJsonObject manifest{{"api", 2}, {"kind", "declarative"}, {"id", "navigation-regression"},
            {"name", "Navigation regression"}, {"version", "1.0.0"},
            {"requiresHost", QJsonObject{{"min", "1.4.1"}, {"maxExclusive", "2.0.0"}}},
            {"permissions", QJsonArray{"player.control", "ui.panels"}}, {"settings", QJsonArray{}}, {"rules", QJsonArray{}},
            {"contributions", QJsonArray{QJsonObject{{"id", "navigation"}, {"type", "panel"},
                {"slot", "home.panels"}, {"title", "Navigation"}, {"buttons", buttons}}}}};
        QCoreApplication::setApplicationVersion("1.4.1");
        QFile file(directory.filePath("navigation-plugin.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(manifest).toJson()); file.close();
        QSignalSpy prepared(&host, &Plugins::installPrepared);
        QVERIFY(host.prepareInstall(QUrl::fromLocalFile(file.fileName()))["error"].toString().isEmpty());
        QVERIFY(prepared.wait(3000));
        const auto preview = prepared.first()[0].toMap();
        QVERIFY2(preview["error"].toString().isEmpty(), qPrintable(preview["error"].toString()));
        QVERIFY(host.commitInstall(preview["operationId"].toString(), {"player.control", "ui.panels"}).isEmpty());
        QVERIFY(host.enable("navigation-regression", true).isEmpty());
        int commands = 0;
        auto command = [&](const QString &name, const QJsonObject &) {
            if (++commands % 8 == 0) QTest::qWait(1001);
            return host.invokeButton("navigation-regression", "navigation", name == "player.next" ? 0 : 1);
        };
        QCOMPARE(command("player.next", {}), QString("Нет текущего трека."));
        sc.playMix({a, b}, 1);
        QCOMPARE(command("player.previous", {}), QString("Нет предыдущего трека."));
        QCOMPARE(command("player.next", {}), QString("Нет следующего трека."));
        sc.setRepeatMode(2);
        QVERIFY(command("player.next", {}).isEmpty()); QCOMPARE(sc.m_queueIndex, 0);
        QVERIFY(command("player.previous", {}).isEmpty()); QCOMPARE(sc.m_queueIndex, 1);
        QVERIFY(command("player.next", {}).isEmpty()); QCOMPARE(sc.m_queueIndex, 0);
        sc.setRepeatMode(0); sc.setShuffle(true);
        sc.m_visited = QSet<int>(sc.m_queueIds.begin(), sc.m_queueIds.end());
        QCOMPARE(command("player.next", {}), QString("Нет следующего трека."));
        sc.playMix({a}); sc.setRepeatMode(2);
        QVERIFY(command("player.next", {}).isEmpty()); QCOMPARE(sc.m_queueIndex, 0);
        sc.m_waveMode = "test";
        QVERIFY(sc.canGoNext());
        // A wave with a buffered successor uses the same command handler.
        sc.playMix({a, b}); sc.m_waveMode = "test";
        QVERIFY(command("player.next", {}).isEmpty()); QCOMPARE(sc.m_queueIndex, 1);
        QVERIFY(command("player.previous", {}).isEmpty()); QCOMPARE(sc.m_queueIndex, 0);
    }

    void engagementResponseProvenance() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("signedIn", true);
        const QVariantMap track{{"urn", "soundcloud:tracks:1"}, {"favoritings_count", 10}};
        sc.set("current", track); sc.set("tracks", QVariantList{track, track});
        sc.m_queue = {track}; sc.set("queue", sc.m_queue);
        sc.m_listings["/regression/cache"] = {QVariantList{track}, {}};
        auto fetch = [&](int count, int delay = 0) {
            net.responses.append({200, QJsonDocument(QJsonObject{{"urn", "soundcloud:tracks:1"},
                {"favoritings_count", count}, {"user_favorite", false}}).toJson(), delay});
            sc.request("/tracks/1", [&](QJsonDocument doc) { sc.set("current", doc.object().toVariantMap()); });
        };
        fetch(10, 100);
        net.responses.append({204, ""}); sc.like(track);
        QTRY_VERIFY(sc.state()["likePending"].toMap().isEmpty());
        QCOMPARE(sc.state()["current"].toMap()["favoritings_count"].toInt(), 11);
        // The fresh GET must not join the pre-write request still in flight.
        fetch(25); QTRY_COMPARE(sc.state()["current"].toMap()["favoritings_count"].toInt(), 25);
        QTest::qWait(120);
        QCOMPARE(sc.state()["current"].toMap()["favoritings_count"].toInt(), 25);
        fetch(7); QTRY_COMPARE(sc.state()["current"].toMap()["favoritings_count"].toInt(), 7);
        QVERIFY(sc.state()["current"].toMap()["user_favorite"].toBool());
        QCOMPARE(sc.m_listings["/regression/cache"].tracks[0].toMap()["favoritings_count"].toInt(), 7);
        sc.set("tracks", QVariantList{track, track}); // Rendering cached objects is not a new observation.
        QCOMPARE(sc.state()["tracks"].toList()[1].toMap()["favoritings_count"].toInt(), 7);
        QCOMPARE(sc.state()["queue"].toList()[0].toMap()["favoritings_count"].toInt(), 7);
        net.responses.append({204, "", 50}); sc.like(track);
        fetch(50, 1); QTest::qWait(15);
        QCOMPARE(sc.state()["current"].toMap()["favoritings_count"].toInt(), 7);
        QTRY_VERIFY(sc.state()["likePending"].toMap().isEmpty()); QTest::qWait(120);
        QCOMPARE(sc.state()["current"].toMap()["favoritings_count"].toInt(), 6);
        net.responses.append({400, "{}", 40}); sc.like(track); fetch(99, 80);
        QTRY_VERIFY(sc.state()["likePending"].toMap().isEmpty()); QTest::qWait(100);
        QCOMPARE(sc.state()["current"].toMap()["favoritings_count"].toInt(), 6);
        fetch(4); QTRY_COMPARE(sc.state()["current"].toMap()["favoritings_count"].toInt(), 4);
        fetch(80, 60); sc.set("profile", QVariantMap{{"urn", "soundcloud:users:other"}});
        QTest::qWait(100); QVERIFY(sc.state()["engagementCounts"].toMap().isEmpty());
        QVERIFY(sc.state()["engagementStates"].toMap().isEmpty());
    }

    void freshCollectionEngagementCounts_data() {
        QTest::addColumn<bool>("repost"); QTest::addColumn<bool>("playlist");
        QTest::newRow("track-repost") << true << false;
        QTest::newRow("playlist-repost") << true << true;
        QTest::newRow("playlist-like") << false << true;
    }
    void freshCollectionEngagementCounts() {
        QFETCH(bool, repost); QFETCH(bool, playlist);
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("signedIn", true);
        const QString id = playlist ? "soundcloud:playlists:1" : "soundcloud:tracks:1";
        const QString field = repost ? "reposts_count" : "likes_count";
        const QString kind = repost ? (playlist ? "playlistReposts" : "trackReposts") : "playlistLikes";
        const QVariantMap item{{"urn", id}, {field, 10}};
        sc.set("engagementReady", QVariantMap{{kind, true}});
        net.responses.append({204, ""});
        if (repost) sc.toggleRepost(item); else sc.like(item);
        QTRY_VERIFY(sc.state()["engagementPending"].toMap().isEmpty());
        auto count = [&] { return sc.state()["engagementCounts"].toMap()[id].toMap()[field].toInt(); };
        QCOMPARE(count(), 11);
        int callbacks = 0;
        auto fetch = [&](const QJsonObject &object) {
            net.responses.append({200, QJsonDocument(QJsonArray{object}).toJson(), 20});
            sc.request("/regression/counts", [&](QJsonDocument) { ++callbacks; });
            sc.request("/regression/counts", [&](QJsonDocument) { ++callbacks; });
        };
        int before = net.requests.size();
        fetch({{"urn", id}, {field, 30}}); QTRY_COMPARE(callbacks, 2);
        QCOMPARE(net.requests.size(), before + 1); QCOMPARE(count(), 30);
        fetch({{"urn", id}, {field, 2}}); QTRY_COMPARE(callbacks, 4); QCOMPARE(count(), 2);
        fetch({{"urn", id}, {field, 99}, {"reveal_stats", false}});
        QTRY_COMPARE(callbacks, 6); QCOMPARE(count(), 2);
        fetch({{"urn", id}, {field, QJsonValue::Null}});
        QTRY_COMPARE(callbacks, 8); QCOMPARE(count(), 2);
        QVERIFY(sc.state()["engagement"].toMap()[kind].toMap()[id].toBool());
        sc.set("signedIn", false); QVERIFY(sc.state()["engagementCounts"].toMap().isEmpty());
    }

    void engagementCountsConfirmedSerializedAndStale() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("signedIn", true);
        const QVariantMap track{{"urn", "soundcloud:tracks:1"}, {"favoritings_count", 0}, {"user_favorite", false}};
        sc.set("current", track); sc.set("tracks", QVariantList{track}); sc.set("queue", QVariantList{track});
        sc.set("home", QVariantList{QVariantMap{{"tracks", QVariantList{track}}}});
        net.responses = {{204, "", 40}};
        const int before = net.requests.size();
        sc.like(track); sc.like(track);
        QTRY_VERIFY(sc.state()["likePending"].toMap().isEmpty());
        QCOMPARE(net.requests.size(), before + 1);
        QCOMPARE(sc.state()["current"].toMap()["favoritings_count"].toInt(), 1);
        QCOMPARE(sc.state()["queue"].toList()[0].toMap()["favoritings_count"].toInt(), 1);
        QCOMPARE(sc.state()["tracks"].toList()[0].toMap()["favoritings_count"].toInt(), 1);
        QCOMPARE(sc.state()["home"].toList()[0].toMap()["tracks"].toList()[0].toMap()["favoritings_count"].toInt(), 1);
        sc.set("current", track); // Late detail response cannot undo a confirmed count.
        QCOMPARE(sc.state()["current"].toMap()["favoritings_count"].toInt(), 1);
        net.responses = {{400, "{}"}}; sc.like(track);
        QTRY_VERIFY(sc.state()["likePending"].toMap().isEmpty());
        QCOMPARE(sc.state()["current"].toMap()["favoritings_count"].toInt(), 1);
        QVERIFY(sc.state()["current"].toMap()["user_favorite"].toBool());
        net.responses = {{204, ""}}; sc.like(track);
        QTRY_VERIFY(sc.state()["likePending"].toMap().isEmpty());
        QCOMPARE(sc.state()["current"].toMap()["favoritings_count"].toInt(), 0);
        net.responses = {{204, "", 80}}; sc.like(track); sc.set("signedIn", false);
        QTest::qWait(120); QVERIFY(sc.state()["engagementCounts"].toMap().isEmpty());
    }

    void repostCountsAndUnknownCounts() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("signedIn", true);
        sc.set("engagementReady", QVariantMap{{"trackReposts", true}, {"playlistLikes", true}});
        const QVariantMap track{{"urn", "soundcloud:tracks:1"}, {"reposts_count", 0}};
        sc.set("current", track);
        net.responses = {{204, "", 30}};
        const int before = net.requests.size(); sc.toggleRepost(track); sc.toggleRepost(track);
        QTRY_VERIFY(sc.state()["engagementPending"].toMap().isEmpty());
        QCOMPARE(net.requests.size(), before + 1);
        QCOMPARE(sc.state()["current"].toMap()["reposts_count"].toInt(), 1);
        net.responses = {{400, "{}"}}; sc.toggleRepost(track);
        QTRY_VERIFY(sc.state()["engagementPending"].toMap().isEmpty());
        QCOMPARE(sc.state()["current"].toMap()["reposts_count"].toInt(), 1);
        QVERIFY(sc.state()["engagement"].toMap()["trackReposts"].toMap()["soundcloud:tracks:1"].toBool());
        const QVariantMap playlist{{"urn", "soundcloud:playlists:1"}, {"likes_count", 17}};
        net.responses = {{204, ""}}; sc.like(playlist);
        QTRY_VERIFY(sc.state()["engagementPending"].toMap().isEmpty());
        QCOMPARE(sc.state()["engagementCounts"].toMap()["soundcloud:playlists:1"].toMap()["likes_count"].toInt(), 18);
        sc.confirmEngagementCount({{"urn", "soundcloud:tracks:missing"}}, false, false);
        QVERIFY(!sc.state()["engagementCounts"].toMap().contains("soundcloud:tracks:missing"));
        sc.confirmEngagementCount({{"urn", "soundcloud:tracks:hidden"}, {"favoritings_count", 42}, {"reveal_stats", false}}, false, false);
        QVERIFY(!sc.state()["engagementCounts"].toMap().contains("soundcloud:tracks:hidden"));
        sc.confirmEngagementCount({{"urn", "soundcloud:tracks:zero"}, {"favoritings_count", 0}}, false, true);
        QCOMPARE(sc.state()["engagementCounts"].toMap()["soundcloud:tracks:zero"].toMap()["favoritings_count"].toInt(), 0);
    }

    void peopleListsIgnoreStaleResponsesAndPaginate() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        const QVariantMap first{{"urn", "soundcloud:users:1"}}, second{{"urn", "soundcloud:users:2"}};
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:users:10\"}]}", 60},
                         {200, "{\"collection\":[{\"urn\":\"soundcloud:users:20\"}],\"next_href\":\"https://api.soundcloud.com/users/2/followers?cursor=2\"}"}};
        sc.loadPeople(first, "followers"); sc.loadPeople(second, "followings");
        QTRY_COMPARE(sc.state()["people"].toList().size(), 1);
        QTest::qWait(80);
        QCOMPARE(sc.state()["people"].toList()[0].toMap().value("urn").toString(), QString("soundcloud:users:20"));
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:users:20\"},{\"urn\":\"soundcloud:users:21\"}]}"}};
        sc.loadPeople(second, "followings", true);
        QTRY_COMPARE(sc.state()["people"].toList().size(), 2);
        QVERIFY(!sc.state()["peopleHasMore"].toBool());
    }
    void profileLinksRejectUnsafeSchemesAndStaleResponses() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        net.responses = {{200, "[{\"url\":\"https://example.com\",\"title\":\"Website\"},{\"url\":\"javascript:alert(1)\"},{\"url\":\"https://user:password@example.com\"},{\"url\":\"file:///tmp/a\"},{\"url\":\"https://example.com\"}]"}};
        sc.loadProfileLinks({{"urn", "soundcloud:users:1"}});
        QTRY_VERIFY(!sc.state()["profileLinksBusy"].toBool());
        QCOMPARE(sc.state()["profileLinks"].toList().size(), 1);
        QCOMPARE(sc.state()["profileLinks"].toList()[0].toMap().value("title").toString(), QString("Website"));
    }
    void collectionLikesAndRepostsSerializeAndPreserveStateOnFailure() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("signedIn", true);
        net.responses = {{200, "{\"collection\":[{\"playlist\":{\"urn\":\"soundcloud:playlists:2\"}}],\"next_href\":\"https://api.soundcloud.com/me/likes/playlists?cursor=2\"}"}, {200, "{\"collection\":[]}"}};
        sc.loadEngagement("playlistLikes"); sc.loadEngagement("playlistLikes");
        QTRY_VERIFY(sc.state()["engagementReady"].toMap().value("playlistLikes").toBool());
        const QVariantMap playlist{{"urn", "soundcloud:playlists:2"}};
        net.responses = {{204, "", 30}};
        const auto count = net.requests.size(); sc.like(playlist); sc.like(playlist);
        QTRY_VERIFY(sc.state()["engagementPending"].toMap().isEmpty());
        QCOMPARE(net.requests.size(), count + 1); QCOMPARE(net.methods.last(), QByteArray("DELETE"));
        QVERIFY(!sc.state()["engagement"].toMap()["playlistLikes"].toMap()["soundcloud:playlists:2"].toBool());
        net.responses = {{500, "{}"}}; sc.like(playlist);
        QTRY_VERIFY(sc.state()["engagementPending"].toMap().isEmpty());
        QVERIFY(!sc.state()["engagement"].toMap()["playlistLikes"].toMap()["soundcloud:playlists:2"].toBool());
        net.responses = {{200, "{\"collection\":[]}"}}; sc.loadEngagement("stationLikes");
        QTRY_VERIFY(sc.state()["engagementReady"].toMap()["stationLikes"].toBool());
        net.responses = {{204, ""}}; sc.like({{"urn", "soundcloud:system-playlists:track-stations:9"}});
        QTRY_VERIFY(sc.state()["engagementPending"].toMap().isEmpty());
        QCOMPARE(net.methods.last(), QByteArray("POST"));
        QVERIFY(net.requests.last().url().path().contains("/likes/playlists/soundcloud:system-playlists:track-stations:9"));
        net.responses = {{200, "{\"collection\":[]}"}}; sc.loadEngagement("trackReposts");
        QTRY_VERIFY(sc.state()["engagementReady"].toMap()["trackReposts"].toBool());
        const QVariantMap track{{"urn", "soundcloud:tracks:7"}};
        net.responses = {{201, ""}}; sc.toggleRepost(track);
        QTRY_VERIFY(sc.state()["engagementPending"].toMap().isEmpty());
        QVERIFY(sc.state()["engagement"].toMap()["trackReposts"].toMap()["soundcloud:tracks:7"].toBool());
        net.responses = {{200, ""}}; sc.toggleRepost(track);
        QTRY_VERIFY(sc.state()["engagementPending"].toMap().isEmpty());
        QCOMPARE(net.methods.last(), QByteArray("DELETE"));
        sc.set("signedIn", false); QVERIFY(sc.state()["engagement"].toMap().isEmpty());
    }
    void playlistAppendRejectsUnsafeSnapshots_data() {
        QTest::addColumn<int>("scenario");
        QTest::newRow("foreign-owner") << 0;
        QTest::newRow("missing-collection") << 1;
        QTest::newRow("missing-track") << 2;
        QTest::newRow("pagination-error") << 3;
        QTest::newRow("pagination-loop") << 4;
        QTest::newRow("remote-conflict") << 5;
        QTest::newRow("logout") << 6;
        QTest::newRow("account-switch") << 7;
    }
    void playlistAppendRejectsUnsafeSnapshots() {
        QFETCH(int, scenario);
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool()); sc.set("signedIn", true);
        sc.set("profile", QVariantMap{{"urn", "soundcloud:users:1"}});
        const QByteArray metadata = "{\"urn\":\"soundcloud:playlists:2\",\"user_urn\":\"soundcloud:users:1\",\"track_count\":1}";
        const QByteArray page = "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"}]}";
        net.responses = {{200, metadata}, {200, page}};
        if (scenario == 0) net.responses = {{200, "{\"urn\":\"soundcloud:playlists:2\",\"user_urn\":\"soundcloud:users:9\",\"track_count\":1}"}};
        if (scenario == 1) net.responses = {{200, metadata}, {200, "{}"}};
        if (scenario == 2) net.responses = {{200, metadata}, {200, "{\"collection\":[]}"}};
        if (scenario == 3 || scenario == 4) {
            const QByteArray next = "{\"collection\":[],\"next_href\":\"https://api.soundcloud.com/playlists/2/tracks?cursor=next\"}";
            net.responses = {{200, metadata}, {200, next}, {scenario == 3 ? 500 : 200, next}};
        }
        if (scenario == 5) net.responses = {{200, metadata}, {200, page}, {200, metadata}, {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:2\"}]}"}};
        if (scenario >= 6) net.responses = {{200, metadata, 100}};
        QSignalSpy saved(&sc, &SoundCloud::trackSaved);
        sc.saveToPlaylist("soundcloud:tracks:999", "soundcloud:playlists:2", "");
        QVERIFY(sc.state()["savingTrack"].toBool());
        if (scenario == 6) sc.logout();
        if (scenario == 7) sc.set("profile", QVariantMap{{"urn", "soundcloud:users:2"}});
        QTRY_VERIFY(!sc.state()["savingTrack"].toBool());
        QTest::qWait(150);
        QCOMPARE(saved.size(), 0);
        if (scenario < 6) QVERIFY(!sc.state()["error"].toString().isEmpty());
        for (const auto &method : net.methods) QVERIFY(method != "PUT");
    }
    void playlistWriteIgnoresOldAccountCompletion() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool()); sc.set("signedIn", true);
        sc.set("profile", QVariantMap{{"urn", "soundcloud:users:1"}});
        net.responses = {{200, "{}", 40}, {200, "{}", 180}, {500, "{}"}};
        QSignalSpy saved(&sc, &SoundCloud::trackSaved);
        sc.saveToPlaylist("soundcloud:tracks:1", "", "Old account");
        sc.set("profile", QVariantMap{{"urn", "soundcloud:users:2"}});
        sc.saveToPlaylist("soundcloud:tracks:2", "", "New account");
        sc.editPlaylist({{"urn", "soundcloud:playlists:2"}});
        QTRY_VERIFY(!sc.state()["playlistEditBusy"].toBool());
        QVERIFY(!sc.state()["playlistEditError"].toString().isEmpty());
        QTest::qWait(60);
        QCOMPARE(saved.size(), 0);
        QVERIFY(sc.state()["savingTrack"].toBool());
        const auto requests = net.requests.size();
        sc.saveToPlaylist("soundcloud:tracks:3", "", "Parallel");
        QCOMPARE(net.requests.size(), requests);
        QTRY_COMPARE(saved.size(), 1);
        QVERIFY(!sc.state()["savingTrack"].toBool());
    }
    void playlistAppendLoadsAllPagesAndSerializesWrites() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool()); sc.set("signedIn", true);
        sc.set("profile", QVariantMap{{"urn", "soundcloud:users:1"}});
        QJsonArray first;
        for (int i = 0; i < 200; ++i) first.append(QJsonObject{{"urn", QString("soundcloud:tracks:%1").arg(i)}});
        const QByteArray metadata = "{\"urn\":\"soundcloud:playlists:2\",\"user_urn\":\"soundcloud:users:1\",\"track_count\":202}";
        const auto page = QJsonDocument(QJsonObject{{"collection", first}, {"next_href", "https://api.soundcloud.com/playlists/2/tracks?cursor=2"}}).toJson();
        const QByteArray tail = "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"},{\"urn\":\"soundcloud:tracks:0\"}]}";
        net.responses = {{200, metadata}, {200, page}, {200, tail}, {200, metadata}, {200, page}, {200, tail}, {200, "{}"}};
        const int requestStart = net.requests.size();
        QSignalSpy saved(&sc, &SoundCloud::trackSaved);
        sc.saveToPlaylist("soundcloud:tracks:999", "soundcloud:playlists:2", "");
        sc.saveToPlaylist("soundcloud:tracks:998", "soundcloud:playlists:2", "");
        QTRY_VERIFY2(saved.size() == 1, qPrintable(sc.state()["error"].toString()));
        QCOMPARE(net.requests.size() - requestStart, 7);
        const auto tracks = QJsonDocument::fromJson(net.bodies.last()).object()["playlist"].toObject()["tracks"].toArray();
        QCOMPARE(tracks.size(), 203);
        QCOMPARE(tracks[200].toObject()["urn"].toString(), QString("soundcloud:tracks:1"));
        QCOMPARE(tracks[201].toObject()["urn"].toString(), QString("soundcloud:tracks:0"));
        QCOMPARE(tracks[202].toObject()["urn"].toString(), QString("soundcloud:tracks:999"));
    }
    void playlistEditorPreservesDuplicatesAndSavesWholeDraft() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("signedIn", true); sc.set("profile", QVariantMap{{"urn", "soundcloud:users:1"}});
        const QByteArray metadata = "{\"urn\":\"soundcloud:playlists:2\",\"user\":{\"urn\":\"soundcloud:users:1\"},\"title\":\"Original\",\"description\":\"About\",\"sharing\":\"private\",\"track_count\":3}";
        const QByteArray tracks = "{\"collection\":[{\"urn\":\"soundcloud:tracks:10\"},{\"urn\":\"soundcloud:tracks:20\"},{\"urn\":\"soundcloud:tracks:10\"}]}";
        net.responses = {{200, metadata}, {200, tracks}};
        sc.editPlaylist({{"urn", "soundcloud:playlists:2"}});
        QTRY_VERIFY(!sc.state()["playlistEditBusy"].toBool());
        QCOMPARE(sc.state()["playlistDraft"].toMap()["tracks"].toList().size(), 3);
        sc.movePlaylistTrack(1, 0); sc.removePlaylistTrack(2);
        net.responses = {{200, metadata}, {200, tracks}, {200, "{}"}};
        QSignalSpy saved(&sc, &SoundCloud::playlistUpdated);
        sc.savePlaylist("New name", "New description", false);
        sc.savePlaylist("Duplicate request", "", false);
        sc.saveToPlaylist("soundcloud:tracks:999", "soundcloud:playlists:2", "");
        sc.deletePlaylist();
        QTRY_COMPARE(saved.count(), 1);
        QCOMPARE(net.methods.last(), QByteArray("PUT"));
        const auto body = QJsonDocument::fromJson(net.bodies.last()).object()["playlist"].toObject();
        QCOMPARE(body["title"].toString(), QString("New name"));
        QCOMPARE(body["description"].toString(), QString("New description"));
        QCOMPARE(body["sharing"].toString(), QString("public"));
        const auto ids = body["tracks"].toArray(); QCOMPARE(ids.size(), 2);
        QCOMPARE(ids[0].toObject()["urn"].toString(), QString("soundcloud:tracks:20"));
        QCOMPARE(ids[1].toObject()["urn"].toString(), QString("soundcloud:tracks:10"));
    }
    void playlistEditorRejectsForeignIncompleteAndConcurrentChanges() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("signedIn", true); sc.set("profile", QVariantMap{{"urn", "soundcloud:users:1"}});
        net.responses = {{200, "{\"urn\":\"soundcloud:playlists:2\",\"user\":{\"urn\":\"soundcloud:users:9\"},\"track_count\":0}"}};
        sc.editPlaylist({{"urn", "soundcloud:playlists:2"}});
        QTRY_VERIFY(!sc.state()["playlistEditBusy"].toBool());
        QVERIFY(sc.state()["playlistDraft"].toMap().isEmpty());
        const QByteArray metadata = "{\"urn\":\"soundcloud:playlists:2\",\"user\":{\"urn\":\"soundcloud:users:1\"},\"title\":\"Before\",\"sharing\":\"public\",\"track_count\":1}";
        net.responses = {{200, metadata}, {200, "{\"collection\":[]}"}};
        sc.editPlaylist({{"urn", "soundcloud:playlists:2"}});
        QTRY_VERIFY(!sc.state()["playlistEditBusy"].toBool());
        QVERIFY(sc.state()["playlistDraft"].toMap().isEmpty());
        const QByteArray tracks = "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"}]}";
        net.responses = {{200, metadata}, {200, tracks}};
        sc.editPlaylist({{"urn", "soundcloud:playlists:2"}});
        QTRY_VERIFY(!sc.state()["playlistEditBusy"].toBool());
        auto changed = QJsonDocument::fromJson(metadata).object(); changed["title"] = "Changed elsewhere";
        net.responses = {{200, QJsonDocument(changed).toJson()}, {200, tracks}};
        const auto count = net.requests.size(); sc.savePlaylist("My draft", "", false);
        QTRY_VERIFY(!sc.state()["playlistEditBusy"].toBool());
        QCOMPARE(net.requests.size(), count + 2); // GET validation only; no PUT.
        QVERIFY(sc.state()["playlistEditError"].toString().contains("изменился"));
        QCOMPARE(sc.state()["playlistDraft"].toMap()["title"].toString(), QString("Before"));
    }
    void playlistDeleteValidatesOwnershipAndRefreshesState() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("signedIn", true); sc.set("profile", QVariantMap{{"urn", "soundcloud:users:1"}});
        const QByteArray metadata = "{\"urn\":\"soundcloud:playlists:2\",\"user\":{\"urn\":\"soundcloud:users:1\"},\"track_count\":0}";
        net.responses = {{200, metadata}, {200, "{\"collection\":[]}"}};
        sc.editPlaylist({{"urn", "soundcloud:playlists:2"}});
        QTRY_VERIFY(!sc.state()["playlistEditBusy"].toBool());
        net.responses = {{200, metadata}, {200, "{\"collection\":[]}"}, {204, ""}};
        QSignalSpy deleted(&sc, &SoundCloud::playlistDeleted); sc.deletePlaylist();
        QTRY_COMPARE(deleted.count(), 1); QCOMPARE(net.methods.last(), QByteArray("DELETE"));
        QVERIFY(sc.state()["playlistDraft"].toMap().isEmpty()); QVERIFY(!sc.state()["playlistEditBusy"].toBool());
    }
    void soundcloudResourceLinksCoverProfilesAlbumsAndStations() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        QSignalSpy resolved(&sc, &SoundCloud::resourceResolved);
        const QList<QPair<QString, QString>> cases{
            {"https://soundcloud.com/example-artist", "soundcloud:users:1"},
            {"https://soundcloud.com/example-artist/song", "soundcloud:tracks:2"},
            {"https://soundcloud.com/example-artist/sets/album", "soundcloud:playlists:3"},
            {"https://soundcloud.com/example-artist/sets/playlist", "soundcloud:playlists:4"},
            {"https://soundcloud.com/stations/track/example-artist/song", "soundcloud:system-playlists:track-stations:2"},
            {"https://on.soundcloud.com/example", "soundcloud:playlists:3"}};
        for (const auto &[url, urn] : cases) {
            const bool album = url.endsWith("/album");
            net.responses = {{200, QJsonDocument(QJsonObject{{"urn", urn}, {"is_album", album}}).toJson()}};
            const auto count = resolved.count(); sc.openLink(url);
            QTRY_COMPARE(resolved.count(), count + 1);
            QCOMPARE(resolved.last()[0].toMap()["urn"].toString(), urn);
            QCOMPARE(resolved.last()[0].toMap()["is_album"].toBool(), album);
        }
    }

    void soundcloudLinksResolveWithoutLeakingOAuthToRedirects() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        QSignalSpy resolved(&sc, &SoundCloud::resourceResolved);
        Response redirect{302, ""}; redirect.redirect = QUrl("https://api.soundcloud.com/tracks/soundcloud%3Atracks%3A7");
        net.responses = {redirect, {200, "{\"urn\":\"soundcloud:tracks:7\",\"kind\":\"track\"}"}};
        sc.openLink("scrip://open?url=https%3A%2F%2Fon.soundcloud.com%2Fexample");
        QTRY_COMPARE(resolved.count(), 1);
        QCOMPARE(resolved[0][0].toMap()["urn"].toString(), QString("soundcloud:tracks:7"));
        const auto count = net.requests.size();
        redirect.redirect = QUrl("https://untrusted.example/steal-token"); net.responses = {redirect};
        sc.openLink("https://soundcloud.com/person/song");
        QTRY_VERIFY(!sc.state()["linkBusy"].toBool());
        QCOMPARE(net.requests.size(), count + 1);
        sc.openLink("https://soundcloud.com.untrusted.example/a");
        sc.openLink("https://user:pass@soundcloud.com/a");
        QCOMPARE(net.requests.size(), count + 1);
        QCOMPARE(resolved.count(), 1);
    }
    void savedStationsLoadTheirVisibleTracks() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        net.responses = {{200, "{\"urn\":\"soundcloud:system-playlists:track-stations:9\",\"tracks\":[{\"urn\":\"soundcloud:tracks:1\"},{\"urn\":\"soundcloud:tracks:2\"}]}"}};
        sc.loadPlaylist({{"urn", "soundcloud:system-playlists:track-stations:9"}});
        QTRY_VERIFY(!sc.state()["playlistBusy"].toBool());
        QCOMPARE(sc.state()["playlistTracks"].toList().size(), 2);
        QVERIFY(net.requests.last().url().path().startsWith("/system-playlists/"));
    }

    void followingsPaginateAndMutationsAreSerialized() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("signedIn", true);
        sc.set("profile", QVariantMap{{"urn", "soundcloud:users:1"}, {"followings_count", 2}});
        const QVariantMap artist{{"urn", "soundcloud:users:2"}};
        net.responses = {
            {200, "{\"collection\":[{\"urn\":\"soundcloud:users:3\"}],\"next_href\":\"https://api.soundcloud.com/me/followings?cursor=2\"}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:users:2\"}]}"}};
        sc.loadFollowings(); sc.loadFollowings();
        QTRY_VERIFY(sc.state()["followingsReady"].toBool());
        QCOMPARE(sc.state()["followings"].toMap().size(), 2);
        const auto count = net.requests.size();
        net.responses = {{204, "", 20}};
        sc.toggleFollow(artist); sc.toggleFollow(artist);
        QTRY_VERIFY(!sc.state()["followingPending"].toMap().contains("soundcloud:users:2"));
        QCOMPARE(net.requests.size(), count + 1);
        QCOMPARE(net.methods.last(), QByteArray("DELETE"));
        QVERIFY(!sc.state()["followings"].toMap().value("soundcloud:users:2").toBool());
        net.responses = {{500, "{}"}};
        sc.toggleFollow(artist);
        QTRY_VERIFY(!sc.state()["followingPending"].toMap().contains("soundcloud:users:2"));
        QVERIFY(!sc.state()["followings"].toMap().value("soundcloud:users:2").toBool());
        net.responses = {{204, ""}};
        sc.toggleFollow(artist);
        QTRY_VERIFY(sc.state()["followings"].toMap().value("soundcloud:users:2").toBool());
        QCOMPARE(net.methods.last(), QByteArray("PUT"));
        const auto afterFollow = net.requests.size();
        sc.toggleFollow(sc.state()["profile"].toMap());
        QCOMPARE(net.requests.size(), afterFollow);
        sc.set("signedIn", false);
        QVERIFY(sc.state()["followings"].toMap().isEmpty());
        QVERIFY(!sc.state()["followingsReady"].toBool());
    }

    void publicCollectionRoutes_data() {
        QTest::addColumn<QString>("section"); QTest::addColumn<QString>("suffix");
        QTest::newRow("tracks") << "userTracks" << "/tracks";
        QTest::newRow("playlists") << "userSets" << "/playlists";
        QTest::newRow("liked tracks") << "userLikes" << "/likes/tracks";
        QTest::newRow("liked playlists") << "userLikedPlaylists" << "/likes/playlists";
        QTest::newRow("liked stations") << "userLikedStations" << "/likes/system-playlists";
        QTest::newRow("reposted tracks") << "userReposts" << "/reposts/tracks";
        QTest::newRow("reposted playlists") << "userRepostedPlaylists" << "/reposts/playlists";
    }
    void publicCollectionRoutes() {
        QFETCH(QString, section); QFETCH(QString, suffix);
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("visitedProfile", QVariantMap{{"urn", "soundcloud:users:other"}});
        const auto before = net.requests.size(); net.responses = {{200, "{\"collection\":[]}"}};
        sc.browse(section, "soundcloud:users:other"); QTRY_VERIFY(!sc.state()["busy"].toBool());
        QCOMPARE(net.requests.size(), before + 1);
        QCOMPARE(net.requests.last().url().path(), "/users/soundcloud:users:other" + suffix);
        QCOMPARE(QUrlQuery(net.requests.last().url()).queryItemValue("limit"), "30");
        QCOMPARE(sc.state()["collectionStatus"].toString(), "empty");
        sc.browse(section, "soundcloud:users:other"); QCOMPARE(net.requests.size(), before + 1);
    }
    void publicCollectionsPaginationAndOwnStates() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("signedIn", true); sc.m_likesComplete = true;
        sc.m_likedTracks.insert("soundcloud:tracks:mine");
        sc.set("engagement", QVariantMap{{"trackReposts", QVariantMap{{"soundcloud:tracks:mine", true}}}});
        const auto engagement = sc.state()["engagement"];
        sc.set("visitedProfile", QVariantMap{{"urn", "soundcloud:users:other"}});
        const QByteArray next = "https://api.soundcloud.com/users/soundcloud%3Ausers%3Aother/likes/tracks?cursor=2";
        net.responses = {{200, "{\"collection\":[{\"track\":{\"urn\":\"soundcloud:tracks:other\",\"user_favorite\":true}}],\"next_href\":\"" + next + "\"}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:other\"},{\"urn\":\"soundcloud:tracks:mine\"}]}"}};
        sc.browse("userLikes", "soundcloud:users:other"); QTRY_VERIFY(sc.state()["hasMore"].toBool());
        QVERIFY(!sc.state()["tracks"].toList().first().toMap()["user_favorite"].toBool());
        sc.more(); QTRY_COMPARE(sc.state()["tracks"].toList().size(), 2);
        QVERIFY(sc.state()["tracks"].toList().last().toMap()["user_favorite"].toBool());
        QVERIFY(!sc.state()["hasMore"].toBool());
        QCOMPARE(net.requests.last().url(), QUrl(next));
        QCOMPARE(sc.m_likedTracks, QSet<QString>{"soundcloud:tracks:mine"});
        QCOMPARE(sc.state()["engagement"], engagement);
    }
    void publicCollectionPageRetryAndCycle() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("visitedProfile", QVariantMap{{"urn", "soundcloud:users:a"}});
        const QByteArray next = "https://api.soundcloud.com/users/soundcloud%3Ausers%3Aa/reposts/tracks?cursor=2";
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:one\"}],\"next_href\":\"" + next + "\"}"}, {400, "{}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:two\"}],\"next_href\":\"" + next + "\"}"}};
        sc.browse("userReposts", "soundcloud:users:a"); QTRY_VERIFY(sc.state()["hasMore"].toBool());
        sc.more(); QTRY_COMPARE(sc.state()["collectionStatus"].toString(), "error");
        QCOMPARE(sc.state()["tracks"].toList().size(), 1); QVERIFY(sc.state()["hasMore"].toBool());
        sc.clearError(); sc.more(); QTRY_COMPARE(sc.state()["tracks"].toList().size(), 2);
        QVERIFY(!sc.state()["hasMore"].toBool());
    }
    void publicCollectionQuotaUsesExistingScheduler() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("visitedProfile", QVariantMap{{"urn", "soundcloud:users:a"}});
        qint64 now = 1700000000000LL; QList<std::function<void()>> pending;
        sc.m_requestNow = [&] { return now; };
        sc.m_requestLater = [&](qint64, std::function<void()> task) { pending.append(task); };
        Response limited{429, "{}"}; limited.headers = {{"Retry-After", "60"}};
        net.responses = {limited, {200, "[]"}};
        sc.browse("userLikes", "soundcloud:users:a");
        QTRY_COMPARE(pending.size(), 1);
        QCOMPARE(sc.state()["collectionStatus"].toString(), "loading");
        const auto count = net.requests.size();
        sc.browse("userLikes", "soundcloud:users:a");
        QCOMPARE(net.requests.size(), count); QCOMPARE(pending.size(), 1);
        now += 60000; pending.takeFirst()(); QTRY_COMPARE(sc.state()["collectionStatus"].toString(), "empty");
    }
    void publicCollectionFailures_data() {
        QTest::addColumn<int>("http"); QTest::addColumn<QByteArray>("body"); QTest::addColumn<QString>("state");
        QTest::newRow("empty") << 200 << QByteArray("[]") << "empty";
        QTest::newRow("private") << 403 << QByteArray("{}") << "unavailable";
        QTest::newRow("missing") << 404 << QByteArray("{}") << "unavailable";
        QTest::newRow("bad request") << 400 << QByteArray("{}") << "error";
        QTest::newRow("malformed") << 200 << QByteArray("{bad") << "error";
        QTest::newRow("incomplete") << 200 << QByteArray("{}") << "error";
    }
    void publicCollectionFailures() {
        QFETCH(int, http); QFETCH(QByteArray, body); QFETCH(QString, state);
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("visitedProfile", QVariantMap{{"urn", "soundcloud:users:other"}});
        net.responses = {{http, body}}; sc.browse("userLikedStations", "soundcloud:users:other");
        QTRY_VERIFY(!sc.state()["busy"].toBool());
        QCOMPARE(sc.state()["collectionStatus"].toString(), state);
        QCOMPARE(sc.state()["error"].toString().isEmpty(), state == "empty");
        QVERIFY(sc.state()["tracks"].toList().isEmpty());
    }
    void publicCollectionStaleResponses() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("visitedProfile", QVariantMap{{"urn", "soundcloud:users:a"}});
        net.responses = {{200, "[{\"urn\":\"soundcloud:tracks:old\"}]", 100},
            {200, "{\"urn\":\"soundcloud:users:b\"}", 1},
            {200, "[{\"urn\":\"soundcloud:tracks:new\"}]", 1}};
        sc.browse("userLikes", "soundcloud:users:a"); sc.browse("userReposts", "soundcloud:users:b");
        QTRY_COMPARE(sc.state()["tracks"].toList().size(), 1); QTest::qWait(150);
        QCOMPARE(sc.state()["tracks"].toList().first().toMap()["urn"].toString(), "soundcloud:tracks:new");
        QCOMPARE(sc.state()["visitedProfile"].toMap()["urn"].toString(), "soundcloud:users:b");
        net.responses = {{403, "{}", 100}, {200, "[]", 1}};
        sc.browse("userLikedStations", "soundcloud:users:b"); sc.browse("userLikedPlaylists", "soundcloud:users:b");
        QTRY_VERIFY(!sc.state()["busy"].toBool()); QTest::qWait(150);
        QCOMPARE(sc.state()["collectionStatus"].toString(), "empty"); QVERIFY(sc.state()["error"].toString().isEmpty());
        sc.set("profile", QVariantMap{{"urn", "soundcloud:users:old-account"}});
        net.responses = {{200, "[{\"urn\":\"soundcloud:tracks:old-account\"}]", 100}};
        sc.browse("userLikes", "soundcloud:users:b", true);
        sc.set("profile", QVariantMap{{"urn", "soundcloud:users:new-account"}});
        sc.set("tracks", QVariantList{}); QTest::qWait(150);
        QVERIFY(sc.state()["tracks"].toList().isEmpty());
    }
    void publicProfileLookupCannotReplaceNewProfile() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        net.responses = {{200, "{\"urn\":\"soundcloud:users:a\"}", 100}, {200, "[]", 100},
            {200, "{\"urn\":\"soundcloud:users:b\"}", 1}, {200, "[]", 1}};
        sc.browse("userLikes", "soundcloud:users:a"); sc.browse("userLikes", "soundcloud:users:b");
        QTRY_COMPARE(sc.state()["visitedProfile"].toMap()["urn"].toString(), "soundcloud:users:b");
        QTest::qWait(150); QCOMPARE(sc.state()["visitedProfile"].toMap()["urn"].toString(), "soundcloud:users:b");
    }
    void trackArtistFallbackAndRefresh() {
        const QVariantMap uploader{{"urn", "soundcloud:users:label"}, {"username", "Label"}, {"permalink_url", "https://soundcloud.com/label"}};
        QVariantMap track{{"urn", "soundcloud:tracks:credit"}, {"user", uploader}};
        QCOMPARE(TrackMetadata::displayArtist(track), "Label");
        for (const auto &value : {QVariant(), QVariant(""), QVariant("  "), QVariant(42), QVariant(QVariantMap{}), QVariant("bad\nname")}) {
            track["metadata_artist"] = value; QCOMPARE(TrackMetadata::displayArtist(track), "Label");
        }
        track["metadata_artist"] = "  Музыкант  "; QCOMPARE(TrackMetadata::displayArtist(track), "Музыкант");
        QVERIFY(TrackMetadata::hasSeparateArtist(track)); QCOMPARE(track["user"].toMap(), uploader);
        const auto restored = QJsonDocument::fromJson(QJsonDocument::fromVariant(track).toJson()).toVariant().toMap();
        QCOMPARE(TrackMetadata::displayArtist(restored), "Музыкант");
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        auto legacy = track; legacy.remove("metadata_artist");
        sc.set("current", legacy); sc.set("queue", QVariantList{legacy});
        sc.set("tracks", QVariantList{track});
        QCOMPARE(sc.displayArtist(sc.state()["current"].toMap()), "Музыкант");
        QCOMPARE(sc.displayArtist(sc.state()["queue"].toList().first().toMap()), "Музыкант");
        QCOMPARE(sc.state()["current"].toMap()["user"].toMap(), uploader);
        track["metadata_artist"] = ""; sc.set("tracks", QVariantList{track});
        QCOMPARE(sc.displayArtist(sc.state()["current"].toMap()), "Label");
        QCOMPARE(sc.state()["queue"].toList().first().toMap()["user"].toMap(), uploader);
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
        QTRY_VERIFY(sc.state()["likePending"].toMap().isEmpty());
        QVERIFY(sc.m_likeOverrides.contains("soundcloud:tracks:2"));
        QCOMPARE(net.methods.last(), QByteArray("DELETE"));
        sc.m_likedTracks.insert("soundcloud:tracks:2"); sc.refreshLikeStates();
        QVERIFY(!sc.m_queue[0].toMap()["user_favorite"].toBool());
        sc.set("current", track); QVERIFY(!sc.state()["current"].toMap()["user_favorite"].toBool());
        sc.set("signedIn", false);
        QVERIFY(sc.m_likedTracks.isEmpty()); QVERIFY(sc.m_likeOverrides.isEmpty());
    }
    void progressSliderClickAndDrag() {
        for (const auto &path : {"/qml/Main.qml"}) {
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
        sc.browse("tracks", "test"); QTRY_VERIFY(!sc.state()["busy"].toBool()); sc.more();
        QCOMPARE(net.requests.size(), 2); QVERIFY(!sc.state()["hasMore"].toBool());
        QCOMPARE(sc.state()["collectionSearchStatus"].toString(), QString("error"));
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
        Network net; net.responses = {guest(), {429,"{}"}, {429,"{}"}, {429,"{}"}};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        sc.m_requestNow = [&] { return now; };
        sc.m_requestLater = [&](qint64 delay, std::function<void()> task) { now += delay; QTimer::singleShot(0, &sc, task); };
        sc.browse("tracks", "test");
        QTRY_VERIFY(!sc.state()["error"].toString().isEmpty()); QCOMPARE(net.requests.size(), 4); QVERIFY(!sc.state()["busy"].toBool());
    }
    void quotaHeadersAndGroupsUseVirtualTime() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        qint64 now = 1700000000000LL;
        QList<QPair<qint64, std::function<void()>>> pending;
        sc.m_requestNow = [&] { return now; };
        sc.m_requestLater = [&](qint64 delay, std::function<void()> task) { pending.append({delay, task}); };
        Response limited{429, "{}"};
        limited.headers = {{"rEtRy-AfTeR", "3"}, {"X-Ratelimit-Limit", "19"},
                           {"X-Ratelimit-Remaining", "0"}, {"X-Ratelimit-Reset", "1700000005"}};
        net.responses = {limited, {200, "{}"}, {200, "{}"}};
        int done = 0;
        sc.request("/tracks", [&](QJsonDocument) { ++done; });
        QTRY_COMPARE(pending.size(), 1);
        QCOMPARE(pending.first().first, 5000);
        sc.request("/tracks/1/streams", [&](QJsonDocument) { ++done; });
        QTRY_COMPARE(done, 1); // Separate stream budget remains usable.
        sc.request("/users", [&](QJsonDocument) { ++done; });
        QCOMPARE(net.requests.size(), 2); QCOMPARE(pending.size(), 2);
        now += 5000;
        auto first = pending.takeFirst(); first.second();
        QTRY_COMPARE(done, 2);
        QCOMPARE(net.requests.size(), 3);
    }
    void quotaSuccessDateAndInvalidHeaders() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        qint64 now = 1700000000000LL; sc.m_requestNow = [&] { return now; };
        Response response{200, "{}"};
        response.headers = {{"X-Ratelimit-Remaining", "0"}, {"X-Ratelimit-Reset", "1700000009"},
            {"Retry-After", QDateTime::fromMSecsSinceEpoch(now + 12000, QTimeZone::UTC).toString(Qt::RFC2822Date).toLatin1()}};
        Reply reply(QNetworkRequest(QUrl("https://api.soundcloud.com/me")), response, &net);
        sc.observeRate(&reply); QCOMPARE(sc.m_rateUntil.value("api"), now + 12000);
        response.headers = {{"Retry-After", "bad"}, {"X-Ratelimit-Reset", "-1"}};
        Reply malformed(QNetworkRequest(QUrl("https://api.soundcloud.com/tracks/1/streams")), response, &net);
        sc.observeRate(&malformed); QCOMPARE(sc.m_rateUntil.value("streams"), 0);
    }
    void endpointQuotasDoNotBlockUnrelatedActions() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        qint64 now = 1700000000000LL;
        QList<std::function<void()>> pending;
        sc.m_requestNow = [&] { return now; };
        sc.m_requestLater = [&](qint64, std::function<void()> task) { pending.append(task); };
        Response limited{429, "{}"}; limited.headers = {{"Retry-After", "86400"}};
        net.responses = {limited, {200, "{}"}, {200, "{}"}, {200, "{}"}};
        int failed = 0, done = 0;
        sc.request("/likes/tracks/1", [](QJsonDocument) {}, "POST", {}, true, [&](QString) { ++failed; });
        QTRY_COMPARE(failed, 1);
        QCOMPARE(sc.m_rateUntil.value("likes"), now + 86400000);
        sc.request("/users/1", [&](QJsonDocument) { ++done; });
        sc.request("/playlists/1", [&](QJsonDocument) { ++done; });
        sc.request("/tracks/1/streams", [&](QJsonDocument) { ++done; });
        QTRY_COMPARE(done, 3);
        sc.request("/me/likes/tracks", [&](QJsonDocument) { ++done; });
        QCOMPARE(net.requests.size(), 4); QCOMPARE(pending.size(), 1);
        QCOMPARE(sc.rateGroup(QUrl("https://api.soundcloud.com/playlists"), "POST"), QString("api"));
        QCOMPARE(sc.rateGroup(QUrl("https://api.soundcloud.com/playlists")), QString("search"));
        QCOMPARE(sc.rateGroup(QUrl("https://api.soundcloud.com/reposts/tracks/1"), "DELETE"), QString("reposts"));
        QCOMPARE(sc.rateGroup(QUrl("https://api.soundcloud.com/me/reposts/tracks")), QString("api"));
        QCOMPARE(sc.rateGroup(QUrl("https://api.soundcloud.com/users/1/likes/playlists")), QString("likes"));
        sc.logout(); pending.takeFirst()(); QCOMPARE(net.requests.size(), 4);
    }
    void generalQuotaAlsoDelaysStreamsAndPreloading() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        qint64 now = 1700000000000LL;
        QList<std::function<void()>> pending;
        sc.m_requestNow = [&] { return now; };
        sc.m_requestLater = [&](qint64 delay, std::function<void()> task) { QCOMPARE(delay, 5000); pending.append(task); };
        Response limited{200, "{}"};
        limited.headers = {{"X-Ratelimit-Remaining", "0"}, {"X-Ratelimit-Reset", "1700000005"}};
        net.responses = {limited, {200, "{}"}};
        int done = 0, failed = 0;
        sc.request("/me", [&](QJsonDocument) { ++done; });
        QTRY_COMPARE(done, 1);
        sc.request("/tracks/1/streams", [&](QJsonDocument) { ++done; });
        sc.request("/tracks/2/streams", [](QJsonDocument) {}, "GET", {}, true,
                   [&](QString) { ++failed; }, {}, 0, true);
        QCOMPARE(failed, 1); QCOMPARE(net.requests.size(), 1); QCOMPARE(pending.size(), 1);
        sc.resolveStream(QUrl("https://api.soundcloud.com/tracks/1/stream"), sc.m_playGeneration);
        QCOMPARE(net.requests.size(), 1); QCOMPARE(pending.size(), 2);
        now += 5000; pending.takeFirst()();
        QTRY_COMPARE(done, 2); QCOMPARE(net.requests.size(), 2);
        sc.logout(); pending.takeFirst()(); QCOMPARE(net.requests.size(), 2);
    }
    void readsCoalesceButParametersAndAccountStaySeparate() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        net.responses = {{200, "{}", 10}, {200, "{}", 10}, {200, "{}", 10}};
        int oldDone = 0, newDone = 0;
        sc.request("/tracks?limit=1", [&](QJsonDocument) { ++oldDone; });
        sc.request("https://api.soundcloud.com/tracks?limit=1", [&](QJsonDocument) { ++oldDone; });
        QCOMPARE(net.requests.size(), 1);
        sc.request("/tracks?limit=2", [&](QJsonDocument) { ++oldDone; });
        QCOMPARE(net.requests.size(), 2);
        ++sc.m_epoch;
        sc.request("/tracks?limit=1", [&](QJsonDocument) { ++newDone; });
        QTRY_COMPARE(newDone, 1); QCOMPARE(oldDone, 0); QCOMPARE(net.requests.size(), 3);
        net.responses = {{200, "{}", 10}};
        sc.request("/shared", [&](QJsonDocument) { ++newDone; });
        sc.request("/shared", [&](QJsonDocument) { ++newDone; });
        QTRY_COMPARE(newDone, 3); QCOMPARE(net.requests.size(), 4);
    }
    void retriesAreBoundedAndWritesAreNeverReplayed() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        qint64 now = 1700000000000LL; QList<qint64> delays;
        sc.m_requestNow = [&] { return now; };
        sc.m_requestLater = [&](qint64 delay, std::function<void()> task) {
            delays.append(delay); now += delay; QTimer::singleShot(0, &sc, task);
        };
        net.responses = {{503, "{}"}, {0, "", 0, QNetworkReply::TimeoutError}, {503, "{}"}};
        int failures = 0;
        sc.request("/tracks", [](QJsonDocument) {}, "GET", {}, true, [&](QString) { ++failures; });
        QTRY_COMPARE(failures, 1); QCOMPARE(net.requests.size(), 3);
        QCOMPARE(delays, QList<qint64>({500, 1000}));
        for (const QByteArray method : {QByteArray("POST"), QByteArray("PUT"), QByteArray("DELETE")}) {
            net.responses = {{503, "{}"}};
            sc.request("/likes/tracks/1", [](QJsonDocument) {}, method, {}, true, [&](QString) { ++failures; });
            QTRY_COMPARE(failures, net.requests.size() - 2);
        }
        QCOMPARE(net.requests.size(), 6); QCOMPARE(delays.size(), 2);
    }
    void delayedOperationsCancelOnLogoutAndDestruction() {
        Network net; auto sc = std::make_unique<SoundCloud>(nullptr, &net, secrets);
        sc->m_access = "test"; sc->m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc->m_requestNow = [] { return 1000; };
        QList<std::function<void()>> pending;
        sc->m_requestLater = [&](qint64, std::function<void()> task) { pending.append(task); };
        sc->m_rateUntil["api"] = 5000;
        int called = 0;
        sc->request("/tracks", [&](QJsonDocument) { ++called; }, "GET", {}, true, [&](QString) { ++called; });
        QCOMPARE(pending.size(), 1);
        sc->logout(); pending.takeFirst()();
        QCOMPARE(net.requests.size(), 0); QCOMPARE(called, 0);
        sc->deferRequest(1000, [&] { ++called; });
        sc.reset(); pending.takeFirst()(); QCOMPARE(called, 0);
    }
    void likesShareFirstPageAndPartialIsNotComplete() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.set("signedIn", true);
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"}],\"next_href\":\"https://api.soundcloud.com/me/likes/tracks?cursor=2\"}", 10}, {403, "{}"}};
        int firstDone = 0;
        sc.syncLikes(); sc.readLikes([&](QJsonDocument) { ++firstDone; });
        QTRY_COMPARE(firstDone, 1); QTRY_VERIFY(sc.m_reads.isEmpty());
        QCOMPARE(net.requests.size(), 2); QVERIFY(!sc.m_likesComplete);
        QVERIFY(sc.m_likedTracks.contains("soundcloud:tracks:1"));
        QVERIFY(!sc.withLikeState({{"urn", "soundcloud:tracks:2"}}).contains("user_favorite"));
        sc.readLikes([&](QJsonDocument) { ++firstDone; });
        QCOMPARE(firstDone, 2); QCOMPARE(net.requests.size(), 2);
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:2\"}]}"}};
        sc.syncLikes(); QTRY_VERIFY(sc.m_likesComplete); QCOMPARE(net.requests.size(), 3);
    }
    void backgroundWorkYieldsAndHasFiniteRetries() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_startupPending = true;
        int failures = 0;
        sc.request("/tracks/2/streams", [](QJsonDocument) {}, "GET", {}, true,
            [&](QString) { ++failures; }, {}, 0, true);
        QCOMPARE(failures, 1); QCOMPARE(net.requests.size(), 0);
        net.responses = {{200, "{}"}};
        int done = 0; sc.request("/tracks/1/streams", [&](QJsonDocument) { ++done; });
        QTRY_COMPARE(done, 1); QCOMPARE(net.requests.first().priority(), QNetworkRequest::HighPriority);
        sc.m_waveMode = "discovery"; sc.m_waveRetryAttempts = 3;
        sc.scheduleWaveRefill(); QVERIFY(!sc.m_waveRetryTimer.isActive());
        QVERIFY(!sc.state()["waveError"].toString().isEmpty());
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
    void preloadRejectsPreviewAndFallsBackBetweenFullStreams() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        net.requests.clear();
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.player().setSource(QUrl("https://cdn.example.test/current.m3u8"));
        sc.set("queue", QVariantList{QVariantMap{{"urn", "soundcloud:tracks:1"}}, QVariantMap{{"urn", "soundcloud:tracks:2"}}});
        sc.m_queueIndex = 0;
        net.responses = {{200, "{\"preview_mp3_128_url\":\"https://cdn.example.test/preview.mp3\"}"}};
        sc.prepareNext();
        QTest::qWait(60);
        QVERIFY(sc.m_preloadPlayer->source().isEmpty());
        sc.beginCrossfade(); QVERIFY(!sc.m_crossfadeStarted);
        net.responses = {{200, "{\"hls_aac_160_url\":\"http://invalid/160\",\"hls_aac_96_url\":\"https://api.soundcloud.com/stream/96\",\"hls_mp3_128_url\":\"https://cdn.example.test/full.m3u8\"}"}, {403, "{}"}};
        sc.prepareNext();
        QTRY_VERIFY(!sc.m_preloadPlayer->source().isEmpty());
        QVERIFY(!sc.m_preloadPlayer->source().toString().contains("preview"));
        QVERIFY(sc.m_preloadCandidates.isEmpty());
        net.responses = {{200, "{\"hls_aac_160_url\":\"https://api.soundcloud.com/stream/old\"}"},
                         {200, "{\"url\":\"https://cdn.example.test/old.m3u8\"}", 100},
                         {200, "{\"preview_mp3_128_url\":\"https://cdn.example.test/preview.mp3\"}"}};
        sc.prepareNext();
        QTRY_COMPARE(net.responses.size(), 1);
        sc.prepareNext();
        QTest::qWait(160);
        QVERIFY(sc.m_preloadPlayer->source().isEmpty());
    }
    void trackLikesSerializeFailuresAndIgnoreOldAccounts() {
        Network net; net.responses = {guest()};
        SoundCloud sc(nullptr, &net, secrets); QTRY_VERIFY(sc.state()["connected"].toBool());
        net.requests.clear();
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.set("signedIn", true);
        const QVariantMap track{{"urn", "soundcloud:tracks:2"}, {"user_favorite", true}};
        sc.set("current", track); sc.set("tracks", QVariantList{track}); sc.set("queue", QVariantList{track});
        net.responses = {{500, "{}", 70}};
        sc.like(track); sc.like(track); sc.like(track);
        QCOMPARE(net.requests.size(), 1);
        QVERIFY(sc.state()["likePending"].toMap().value("soundcloud:tracks:2").toBool());
        QTRY_VERIFY(sc.state()["likePending"].toMap().isEmpty());
        QVERIFY(sc.state()["current"].toMap()["user_favorite"].toBool());
        QVERIFY(sc.m_queue[0].toMap()["user_favorite"].toBool());
        net.responses = {{204, "", 70}};
        sc.like(track); sc.like(track);
        QCOMPARE(net.requests.size(), 2);
        QTRY_VERIFY(sc.state()["likePending"].toMap().isEmpty());
        QVERIFY(!sc.state()["current"].toMap()["user_favorite"].toBool());
        for (bool logout : {false, true}) {
            const auto error = sc.state()["error"];
            net.responses = {{logout ? 500 : 204, "", 70}};
            sc.like(track);
            if (logout) sc.set("signedIn", false);
            else sc.set("profile", QVariantMap{{"urn", "soundcloud:users:other"}});
            QVERIFY(sc.state()["likePending"].toMap().isEmpty());
            QTest::qWait(120);
            QVERIFY(sc.m_likeOverrides.isEmpty());
            QVERIFY(sc.m_likedTracks.isEmpty());
            QCOMPARE(sc.state()["error"], error);
        }
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
            Network net; net.responses = {failure, failure, failure};
            SoundCloud sc(nullptr, &net, secrets);
            sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            sc.m_requestNow = [&] { return now; };
            sc.m_requestLater = [&](qint64 delay, std::function<void()> task) { now += delay; QTimer::singleShot(0, &sc, task); };
            sc.startTrack({{"urn", "soundcloud:tracks:failure"}});
            QTRY_VERIFY(!sc.state()["error"].toString().isEmpty());
            QVERIFY(!sc.state()["buffering"].toBool());
            QVERIFY(!sc.state()["playbackActive"].toBool());
            QCOMPARE(net.requests.size(), 3);
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
        for (int mode = 0; mode < 3; ++mode) {
            sc.set("recommendationPreference", mode);
            auto *selector = object->findChild<QObject *>("wavePreferenceSelector"); QVERIFY(selector);
            QCOMPARE(selector->property("value").toInt(), mode);
            QVERIFY(!object->findChild<QObject *>("recommendationTuningButton"));
            for (bool dark : {false, true}) {
                object->setProperty("dark", dark);
                for (int width : {284, 320, 720, 1920}) {
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
    void inlineWavePreferenceChangesTrackAndKeepsManualEntries() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.set("signedIn", true); sc.set("profile", QVariantMap{{"urn", "soundcloud:users:802"}});
        const QVariantMap track{{"urn", "soundcloud:tracks:42"}};
        const QVariantMap manual{{"urn", "soundcloud:tracks:43"}, {"scripManualQueue", true}};
        sc.m_queue = {track, manual}; sc.m_queueIndex = 0; sc.set("current", track);
        sc.m_waveMode = "discovery"; sc.set("waveMode", "discovery");
        sc.set("playbackActive", true); sc.set("position", 12000);
        for (int i = 0; i < 40; ++i) {
            sc.m_familiarTracks.append(QVariantMap{{"urn", QString("soundcloud:tracks:%1").arg(100 + i)}});
            sc.m_waveCandidates.append(QVariantMap{{"urn", QString("soundcloud:tracks:%1").arg(200 + i)}});
            net.responses.append({403, "{}"});
        }
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/WavePanel.qml"));
        QScopedPointer<QObject> object(component.createWithInitialProperties({{"backend", QVariant::fromValue(&sc)}, {"motion", false}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *panel = qobject_cast<QQuickItem *>(object.data()); QVERIFY(panel);
        QQuickWindow window; window.resize(320, 330); window.show();
        panel->setParentItem(window.contentItem()); panel->setWidth(320); panel->setHeight(330);
        QTest::qWait(50);
        auto *selector = object->findChild<QQuickItem *>("wavePreferenceSelector"); QVERIFY(selector);
        QSet<QString> played{"soundcloud:tracks:42"};
        QList<int> familiarCounts;
        for (int mode = 0; mode < 3; ++mode) {
            const auto point = selector->mapToScene(QPointF(4 + (mode + 0.5) * (selector->width() - 8) / 3, 26)).toPoint();
            QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point);
            QCOMPARE(sc.m_recommendationPreference, mode);
            const auto current = Recommendations::id(sc.state()["current"]);
            QVERIFY(!played.contains(current)); played.insert(current);
            QCOMPARE(sc.m_queue[sc.m_queueIndex + 1].toMap(), manual);
            QCOMPARE(Recommendations::id(sc.m_queue.first()), QString("soundcloud:tracks:42"));
            QVERIFY(sc.state()["playbackActive"].toBool());
            int count = 0;
            for (const auto &entry : sc.m_queue.mid(sc.m_queueIndex))
                if (sc.m_familiarTracks.contains(QVariantMap{{"urn", Recommendations::id(entry)}})) ++count;
            familiarCounts.append(count);
        }
        QVERIFY(familiarCounts[0] > familiarCounts[1]); QVERIFY(familiarCounts[1] > familiarCounts[2]);
        for (const auto &request : net.requests) QVERIFY(!request.url().path().endsWith("/related"));
        panel->setParentItem(nullptr); object.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QSettings().remove("recommendations/soundcloud:users:802");
    }
    void wavePreferenceWaitsForAutomaticReplyAndRespectsPause() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.set("signedIn", true); sc.set("profile", QVariantMap{{"urn", "soundcloud:users:803"}});
        const QVariantMap current{{"urn", "soundcloud:tracks:1"}}, manual{{"urn", "soundcloud:tracks:2"}, {"scripManualQueue", true}};
        sc.m_queue = {current, manual}; sc.m_queueIndex = 0; sc.set("current", current);
        sc.m_waveMode = "discovery"; sc.m_waveSeeds = {current};
        QJsonArray candidates;
        for (int i = 100; i < 120; ++i) candidates.append(QJsonObject{{"urn", QString("soundcloud:tracks:%1").arg(i)}});
        net.responses = {{200, QJsonDocument(candidates).toJson(), 40}, {403, "{}"}};
        sc.chooseWavePreference(2);
        QCOMPARE(sc.state()["current"].toMap(), current);
        QVERIFY(sc.m_wavePreferenceWaiting);
        QTRY_VERIFY(!sc.m_wavePreferenceWaiting);
        QCOMPARE(sc.m_queueIndex, 1); QCOMPARE(Recommendations::id(sc.state()["current"]), QString("soundcloud:tracks:100"));
        QCOMPARE(sc.m_queue[2].toMap(), manual);
        sc.m_waveCandidates.clear(); sc.m_familiarTracks.clear();
        sc.m_waveSeen.clear();
        for (int i = sc.m_queue.size() - 1; i > sc.m_queueIndex; --i)
            if (sc.m_queue[i].toMap().value("scripAutomaticRecommendation").toBool()) sc.m_queue.removeAt(i);
        sc.resetQueueOrder();
        const auto before = sc.state()["current"];
        net.responses = {{200, QJsonDocument(candidates).toJson(), 40}};
        sc.chooseWavePreference(1); sc.pausePlayback();
        QTest::qWait(100);
        QCOMPARE(sc.state()["current"], before); QVERIFY(!sc.state()["playbackActive"].toBool());
        QVERIFY(!sc.m_wavePreferenceWaiting);
        QSettings().remove("recommendations/soundcloud:users:803");
    }
    void wavePanelClickStartsRetriesAndPauses() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false); // Prevent the fixture's deferred guest connection.
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.set("signedIn", true);
        sc.set("profile", QVariantMap{{"urn", "soundcloud:users:801"}});
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
        net.responses = {{200, "{\"collection\":[]}"}};
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_COMPARE(clicked.size(), 1);
        QTRY_COMPARE(sc.m_waveMode, QString("discovery"));
        QTRY_VERIFY(sc.m_waveRetryTimer.isActive());
        sc.m_waveRetryTimer.stop(); sc.set("waveBusy", false); sc.set("waveError", "Retry");
        const int requests = net.requests.size();
        net.responses = {{200, "{\"collection\":[]}"}};
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_COMPARE(net.requests.size(), requests + 1);
        QTRY_VERIFY(sc.m_waveRetryTimer.isActive());
        sc.m_waveRetryTimer.stop(); sc.set("waveBusy", false);
        sc.set("waveError", ""); sc.set("playbackActive", true);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center);
        QVERIFY(!sc.state()["playbackActive"].toBool());
        QCOMPARE(sc.m_waveMode, QString("discovery"));
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
    void recommendationExclusionsUndoAccountsAndCorruption() {
        QSettings().remove("recommendations");
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        const QVariantMap profile{{"urn", "soundcloud:users:501"}};
        const QVariantMap other{{"urn", "soundcloud:users:502"}};
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}, {"title", "One"},
            {"metadata_artist", "Musician"}, {"user", QVariantMap{{"urn", "soundcloud:users:11"}, {"username", "Label"}}}};
        auto b = a; b["urn"] = "soundcloud:tracks:2";
        auto unrelated = a; unrelated["urn"] = "soundcloud:tracks:3"; unrelated["user"] = QVariantMap{{"urn", "soundcloud:users:12"}, {"username", "Label"}};
        sc.excludeRecommendation(a); QVERIFY(!sc.recommendationExcluded(a));
        sc.set("signedIn", true); sc.set("profile", profile);
        sc.excludeRecommendation(a); sc.excludeRecommendation(b);
        sc.undoRecommendationExclusion();
        QVERIFY(sc.recommendationExcluded(a)); QVERIFY(!sc.recommendationExcluded(b));
        sc.excludeRecommendation(a); sc.undoRecommendationExclusion();
        QVERIFY(sc.recommendationExcluded(a));
        sc.excludeRecommendation(b, true);
        QVERIFY(sc.recommendationExcluded(b)); QVERIFY(!sc.recommendationExcluded(unrelated));
        sc.setRecommendationPreference(2);
        sc.set("profile", other);
        QVERIFY(!sc.recommendationExcluded(a)); QCOMPARE(sc.m_recommendationPreference, 1);
        sc.undoRecommendationExclusion();
        sc.set("profile", profile);
        QVERIFY(sc.recommendationExcluded(b)); QCOMPARE(sc.m_recommendationPreference, 2);
        sc.restoreRecommendation("soundcloud:users:11", true);
        QVERIFY(!sc.recommendationExcluded(b)); QVERIFY(sc.recommendationExcluded(a));
        sc.clearRecommendationExclusions(); QVERIFY(!sc.recommendationExcluded(a));
        QSettings().setValue("recommendations/soundcloud:users:502", "{invalid");
        sc.set("profile", other); QVERIFY(sc.m_excludedTracks.isEmpty()); QCOMPARE(sc.m_recommendationPreference, 1);
        QCOMPARE(net.requests.size(), 0);
        QSettings().remove("recommendations");
    }
    void recommendationExclusionsKeepCurrentManualAndCancelPrepared() {
        QSettings().remove("recommendations");
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.set("signedIn", true); sc.set("profile", QVariantMap{{"urn", "soundcloud:users:501"}});
        const QVariantMap current{{"urn", "soundcloud:tracks:1"}, {"scripAutomaticRecommendation", true}};
        const QVariantMap automatic{{"urn", "soundcloud:tracks:2"}, {"scripAutomaticRecommendation", true}};
        sc.m_queue = {current, automatic}; sc.m_queueIndex = 0; sc.set("current", current);
        sc.enqueue(automatic);
        QVERIFY(sc.m_queue[1].toMap().value("scripManualQueue").toBool());
        sc.m_preloadUrn = "soundcloud:tracks:2"; sc.m_preloadIndex = 2;
        const auto generation = sc.m_preloadGeneration;
        sc.excludeRecommendation(automatic);
        QCOMPARE(sc.m_queue.size(), 2);
        QCOMPARE(sc.m_queue[1].toMap().value("urn").toString(), QString("soundcloud:tracks:2"));
        QCOMPARE(sc.m_queueIndex, 0); QCOMPARE(sc.state()["current"].toMap(), current);
        QVERIFY(sc.m_preloadGeneration > generation); QCOMPARE(sc.m_preloadIndex, -1);
        sc.excludeRecommendation(current); QCOMPARE(sc.m_queue.size(), 2);
        const QVariantMap snapshot{{"waveMode", "discovery"}, {"waveFamiliar", QVariantList{automatic}}};
        sc.m_queue.append(automatic); sc.restoreWaveState(snapshot); sc.pruneRecommendations(false);
        QCOMPARE(sc.m_queue.size(), 2);
        QCOMPARE(sc.m_familiarTracks.size(), 1);
        sc.set("home", QVariantList{QVariantMap{{"recommendations", true}, {"tracks", QVariantList{automatic}}},
            QVariantMap{{"tracks", QVariantList{automatic}}}});
        sc.pruneRecommendations(false);
        QVERIFY(sc.state()["home"].toList()[0].toMap()["tracks"].toList().isEmpty());
        QCOMPARE(sc.state()["home"].toList()[1].toMap()["tracks"].toList().size(), 1);
        QSettings().remove("recommendations");
    }
    void recommendationMixIsBoundedAndDifferent() {
        QVariantList familiar, novel;
        for (int i = 1; i <= 20; ++i) {
            familiar.append(QVariantMap{{"urn", QString("soundcloud:tracks:%1").arg(i)}});
            novel.append(QVariantMap{{"urn", QString("soundcloud:tracks:%1").arg(100 + i)}});
        }
        QList<int> counts;
        for (int preference = 0; preference < 3; ++preference) {
            const auto mixed = Recommendations::mix(familiar, novel, preference, 12);
            QCOMPARE(mixed.size(), 12); int known = 0;
            for (const auto &entry : mixed) if (familiar.contains(entry)) ++known;
            counts.append(known);
            QCOMPARE(Recommendations::mix({}, novel.mid(0, 1), preference, 40).size(), 1);
            QCOMPARE(Recommendations::mix(familiar.mid(0, 1), {}, preference, 40).size(), 1);
            QVERIFY(Recommendations::mix(familiar.mid(0, 1), {}, preference, 40, {"soundcloud:tracks:1"}).isEmpty());
        }
        QVERIFY(counts[0] > counts[1]); QVERIFY(counts[1] > counts[2]);
    }
    void recommendationLateReplyUsesCurrentExclusionsAndAccount() {
        QSettings().remove("recommendations");
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.set("signedIn", true); sc.set("profile", QVariantMap{{"urn", "soundcloud:users:501"}});
        const QVariantMap seed{{"urn", "soundcloud:tracks:1"}};
        const QVariantMap excluded{{"urn", "soundcloud:tracks:2"}};
        sc.m_waveMode = "discovery"; sc.m_waveSeeds = {seed}; sc.m_queue = {seed}; sc.m_queueIndex = 0;
        for (int i = 10; i < 16; ++i) sc.m_queue.append(QVariantMap{{"urn", QString("soundcloud:tracks:%1").arg(i)}});
        sc.set("current", seed);
        net.responses = {{200, R"([{"urn":"soundcloud:tracks:2"},{"urn":"soundcloud:tracks:3"}])", 40}};
        sc.refillWave(); sc.excludeRecommendation(excluded);
        QTRY_VERIFY(!sc.state()["waveBusy"].toBool());
        QCOMPARE(sc.m_queue.size(), 8); QCOMPARE(Recommendations::id(sc.m_queue.last()), QString("soundcloud:tracks:3"));
        QCOMPARE(net.requests.size(), 1);
        net.responses = {{200, R"([{"urn":"soundcloud:tracks:4"}])", 40}};
        sc.refillWave(); sc.set("profile", QVariantMap{{"urn", "soundcloud:users:502"}});
        QTest::qWait(80); QCOMPARE(sc.m_queue.size(), 8);
        QSettings().remove("recommendations");
    }
    void recommendationPreferencesChangeBatchWithoutMoreRequests() {
        QSettings().remove("recommendations");
        QList<int> counts;
        for (int preference = 0; preference < 3; ++preference) {
            Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
            sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
            sc.set("signedIn", true); sc.set("profile", QVariantMap{{"urn", "soundcloud:users:501"}});
            sc.setRecommendationPreference(preference);
            sc.m_waveMode = "discovery";
            const QVariantMap seed{{"urn", "soundcloud:tracks:1"}};
            sc.m_waveSeeds = {seed}; sc.m_queue = {seed}; sc.m_queueIndex = 0; sc.set("current", seed);
            for (int i = 10; i < 16; ++i) sc.m_queue.append(QVariantMap{{"urn", QString("soundcloud:tracks:%1").arg(i)}});
            QJsonArray candidates;
            for (int i = 0; i < 40; ++i) {
                sc.m_familiarTracks.append(QVariantMap{{"urn", QString("soundcloud:tracks:%1").arg(100 + i)}});
                candidates.append(QJsonObject{{"urn", QString("soundcloud:tracks:%1").arg(200 + i)}});
            }
            net.responses = {{200, QJsonDocument(candidates).toJson()}};
            sc.refillWave();
            QTRY_COMPARE(sc.m_queue.size(), 27);
            QCOMPARE(net.requests.size(), 1);
            QCOMPARE(sc.m_queueIndex, 0); QCOMPARE(sc.state()["current"].toMap(), seed);
            int familiar = 0;
            for (const auto &track : sc.m_queue.mid(7))
                if (sc.m_familiarTracks.contains(QVariantMap{{"urn", Recommendations::id(track)}})) ++familiar;
            counts.append(familiar);
            const auto queue = sc.m_queue;
            const auto history = sc.m_history;
            sc.setRecommendationPreference((preference + 1) % 3);
            QCOMPARE(sc.m_queue, queue); QCOMPARE(sc.m_history, history);
        }
        QVERIFY(counts[0] > counts[1]); QVERIFY(counts[1] > counts[2]);
        QSettings().remove("recommendations");
    }
    void recommendationPreferencesLayoutsAndKeyboard() {
        QSettings().remove("recommendations");
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.set("signedIn", true); sc.set("profile", QVariantMap{{"urn", "soundcloud:users:501"}});
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        QQmlComponent hostComponent(&engine);
        hostComponent.setData("import QtQuick; import QtQuick.Controls; ApplicationWindow { visible: true; width: 900; height: 800 }", QUrl());
        QScopedPointer<QObject> host(hostComponent.create()); QVERIFY2(host, qPrintable(hostComponent.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(host.data()); QVERIFY(window);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/RecommendationPreferences.qml"));
        for (const int width : {320, 390, 1000}) {
            window->resize(width, width < 500 ? 740 : 900);
            for (const bool management : {true}) {
                sc.excludeRecommendation(QVariantMap{{"urn", "soundcloud:tracks:1"}, {"title", "A long title for recommendation settings"}});
                QScopedPointer<QObject> popup(component.createWithInitialProperties({
                    {"backend", QVariant::fromValue(&sc)}, {"mobile", width < 500},
                    {"parent", QVariant::fromValue(window->contentItem())}}));
                QVERIFY2(popup, qPrintable(component.errorString()));
                QVERIFY(QMetaObject::invokeMethod(popup.data(), "open"));
                QTest::qWait(100);
                QVERIFY(popup->property("width").toReal() <= width);
                QVERIFY(popup->property("height").toReal() <= window->height());
                auto *content = qvariant_cast<QQuickItem *>(popup->property("contentItem")); QVERIFY(content);
                const auto image = window->grabWindow();
                QVERIFY(!image.isNull());
                QVERIFY(image.save(QString("ui-recommendations-%1-%2.png").arg(width).arg(management)));
                QTest::keyClick(window, width < 500 ? Qt::Key_Back : Qt::Key_Escape);
                QTRY_VERIFY(!popup->property("visible").toBool());
            }
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join("\n")));
        QSettings().remove("recommendations");
    }
    void recommendationOrderingAndScarcity() {
        auto track = [](int id, int artist) { return QVariantMap{{"urn", QString("soundcloud:tracks:%1").arg(id)},
            {"user", QVariantMap{{"urn", QString("soundcloud:users:%1").arg(artist)}}}}; };
        const auto a = track(1, 1), b = track(2, 1), c = track(3, 2), d = track(4, 3);
        QCOMPARE(Recommendations::seeds({a, a, b, c, d}), (QVariantList{a, c, d}));
        auto blocked = track(5, 4); blocked["access"] = "blocked";
        auto unavailable = track(6, 5); unavailable["streamable"] = false;
        const QVariantList pool{a, b, a, blocked, unavailable, c, d};
        QCOMPARE(Recommendations::select(pool, 24), (QVariantList{a, c, d, b}));
        QCOMPARE(Recommendations::select({a, b, blocked, unavailable}, 24, {}, "soundcloud:users:1"), (QVariantList{a, b}));
        QCOMPARE(Recommendations::select(pool, 24, {"soundcloud:tracks:1", "soundcloud:tracks:3"}), (QVariantList{b, d}));
        QVERIFY(Recommendations::select({blocked, unavailable}, 24).isEmpty());
    }
    void recommendationSourcesBoundedPartialAndShared() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        QVariantList seeds;
        for (int i = 1; i <= 8; ++i) seeds.append(QVariantMap{{"urn", QString("soundcloud:tracks:%1").arg(i)},
            {"title", QString("Seed %1").arg(i)}, {"user", QVariantMap{{"urn", QString("soundcloud:users:%1").arg(i)}}}});
        net.responses = {{200, "[{\"urn\":\"soundcloud:tracks:11\"},{\"urn\":\"soundcloud:tracks:12\"}]", 30},
            {403, "{}"}, {200, "[{\"urn\":\"soundcloud:tracks:31\"}]"}};
        int completed = 0, failures = 0; QVariantList result;
        auto done = [&](QJsonDocument doc) { result = doc.array().toVariantList(); ++completed; };
        auto failed = [&](QString) { ++failures; };
        sc.relatedSources(seeds, [] { return true; }, done, failed);
        sc.relatedSources(seeds, [] { return true; }, done, failed);
        QTRY_COMPARE(completed, 2);
        QCOMPARE(failures, 0); QCOMPARE(net.requests.size(), 3);
        QCOMPARE(result.size(), 3);
        QCOMPARE(result[0].toMap()["urn"].toString(), QString("soundcloud:tracks:11"));
        QCOMPARE(result[1].toMap()["urn"].toString(), QString("soundcloud:tracks:31"));
        QCOMPARE(result[0].toMap()["recommendationSource"].toString(), QString("Seed 1"));
        for (const auto &request : net.requests) QVERIFY(request.url().path().endsWith("/related"));
    }
    void homeRecommendationsUseDistinctPartialShelves() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.set("signedIn", true);
        net.responses = {{200, "[]"}, {200, "[]"},
            {200, "[{\"urn\":\"soundcloud:tracks:1\",\"user\":{\"urn\":\"soundcloud:users:1\"}},{\"urn\":\"soundcloud:tracks:2\",\"user\":{\"urn\":\"soundcloud:users:2\"}}]"},
            {200, "[{\"urn\":\"soundcloud:tracks:1\"},{\"urn\":\"soundcloud:tracks:10\"},{\"urn\":\"soundcloud:tracks:11\"},{\"urn\":\"soundcloud:tracks:12\"},{\"urn\":\"soundcloud:tracks:13\",\"access\":\"blocked\"}]"},
            {403, "{}"}};
        sc.loadHome(); QTRY_VERIFY(!sc.state()["homeBusy"].toBool());
        QVariantList suggestions, discoveries;
        for (const auto &entry : sc.state()["home"].toList()) {
            const auto shelf = entry.toMap();
            if (shelf["title"] == "Вам может понравиться") suggestions = shelf["tracks"].toList();
            if (shelf["title"] == "Микс открытий") discoveries = shelf["tracks"].toList();
        }
        QCOMPARE(net.requests.size(), 5);
        QCOMPARE(suggestions.size(), 2); QCOMPARE(discoveries.size(), 1);
        QSet<QString> ids;
        for (const auto &track : suggestions + discoveries) {
            QVERIFY(Recommendations::playable(track));
            QVERIFY(!ids.contains(Recommendations::id(track))); ids.insert(Recommendations::id(track));
        }
        QVERIFY(!ids.contains("soundcloud:tracks:1")); QVERIFY(!ids.contains("soundcloud:tracks:13"));
    }
    void multiSourceWaveAppendsWithoutChangingSelection() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_waveMode = "discovery";
        const QVariantMap current{{"urn", "soundcloud:tracks:90"}};
        const QVariantMap manual{{"urn", "soundcloud:tracks:91"}};
        sc.m_queue = {current, manual, QVariantMap{{"urn", "soundcloud:tracks:92"}}}; sc.m_queueIndex = 0; sc.set("current", current);
        for (int i = 1; i <= 3; ++i) {
            sc.m_waveSeeds.append(QVariantMap{{"urn", QString("soundcloud:tracks:%1").arg(i)},
                {"title", QString("Seed %1").arg(i)}, {"user", QVariantMap{{"urn", QString("soundcloud:users:%1").arg(i)}}}});
            const QJsonObject candidate{{"urn", QString("soundcloud:tracks:%1").arg(100 + i)},
                {"user", QJsonObject{{"urn", QString("soundcloud:users:%1").arg(i)}}}};
            net.responses.append({200, QJsonDocument(QJsonArray{candidate, QJsonObject::fromVariantMap(manual)}).toJson(), (4-i)*10});
        }
        sc.refillWave(); QTRY_VERIFY(!sc.state()["waveBusy"].toBool());
        QCOMPARE(net.requests.size(), 3); QCOMPARE(sc.m_queue.size(), 6);
        QCOMPARE(sc.m_queueIndex, 0); QCOMPARE(sc.state()["current"].toMap(), current);
        QCOMPARE(sc.m_queue[1].toMap(), manual);
        for (int i = 1; i <= 3; ++i) {
            QCOMPARE(sc.m_queue[i+2].toMap()["urn"].toString(), QString("soundcloud:tracks:%1").arg(100+i));
            QCOMPARE(sc.m_queue[i+2].toMap()["recommendationSource"].toString(), QString("Seed %1").arg(i));
        }
    }
    void lateWaveRefillRespectsManualHistorySelection() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_waveMode = "discovery"; sc.m_waveWaiting = true;
        sc.m_waveSeeds = {QVariantMap{{"urn", "soundcloud:tracks:1"}}};
        const QVariantMap previous{{"urn", "soundcloud:tracks:90"}};
        const QVariantMap current{{"urn", "soundcloud:tracks:91"}};
        sc.m_queue = {previous, current}; sc.m_queueIndex = 1; sc.set("current", current);
        QJsonArray tracks;
        for (int i = 100; i < 106; ++i) tracks.append(QJsonObject{{"urn", QString("soundcloud:tracks:%1").arg(i)}});
        net.responses = {{200, QJsonDocument(tracks).toJson(), 30}, {403, "{}"}};
        sc.refillWave(); sc.playQueue(0);
        QVERIFY(!sc.m_waveWaiting);
        QTRY_COMPARE(sc.m_queue.size(), 8);
        QCOMPARE(sc.m_queueIndex, 0); QCOMPARE(sc.state()["current"].toMap(), previous);
        QCOMPARE(net.requests.size(), 2);
    }
    void multiSourceWaveExhaustionHasFiniteBudget() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_waveMode = "discovery";
        for (int i = 1; i <= 3; ++i) sc.m_waveSeeds.append(QVariantMap{{"urn", QString("soundcloud:tracks:%1").arg(i)},
            {"user", QVariantMap{{"urn", QString("soundcloud:users:%1").arg(i)}}}});
        for (int i = 0; i < 6; ++i) net.responses.append({200, "[{\"urn\":\"soundcloud:tracks:99\",\"access\":\"blocked\"}]"});
        sc.refillWave();
        QTRY_COMPARE(net.requests.size(), 6); QTRY_VERIFY(!sc.state()["waveBusy"].toBool());
        QVERIFY(sc.m_queue.isEmpty()); QVERIFY(!sc.m_waveRetryTimer.isActive());
        QTest::qWait(40); QCOMPARE(net.requests.size(), 6);
    }
    void multiSourceWaveCancellationKeepsQueueEdits() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_waveMode = "discovery";
        for (int i = 1; i <= 3; ++i) {
            sc.m_waveSeeds.append(QVariantMap{{"urn", QString("soundcloud:tracks:%1").arg(i)},
                {"user", QVariantMap{{"urn", QString("soundcloud:users:%1").arg(i)}}}});
            net.responses.append({200, "[{\"urn\":\"soundcloud:tracks:99\"}]", 30});
        }
        const QVariantMap current{{"urn", "soundcloud:tracks:90"}};
        sc.m_queue = {current}; sc.m_queueIndex = 0; sc.set("current", current);
        sc.refillWave(); sc.clearQueueAfterCurrent();
        QTest::qWait(80);
        QCOMPARE(sc.m_queue, QVariantList{current});
        QCOMPARE(sc.m_queueIndex, 0); QCOMPARE(sc.state()["current"].toMap(), current);
        QCOMPARE(net.requests.size(), 3);
    }
    void waveFiltersUnplayableTracksAndRefillsAtEnd() {
        Network net; SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_waveMode = "library";
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\",\"access\":\"blocked\"},{\"urn\":\"soundcloud:tracks:2\",\"streamable\":false},{\"urn\":\"soundcloud:tracks:3\"}]}"}};
        sc.refillWave();
        QTRY_VERIFY(!sc.state()["waveBusy"].toBool());
        QCOMPARE(sc.m_queue.size(), 1);
        QCOMPARE(sc.m_queue[0].toMap()["urn"].toString(), QString("soundcloud:tracks:3"));
        sc.m_queueIndex = 0;
        net.responses = {{200, "{\"collection\":[]}"}};
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
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:42\"}],\"next_href\":\"/tracks/1/related?cursor=next\"}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:42\"},{\"urn\":\"soundcloud:tracks:9000\"},{\"urn\":\"soundcloud:tracks:9000\"}]}"}
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
            net.responses.append({200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:2\"}]}"});
        sc.next();
        QTRY_VERIFY(sc.m_waveRetryTimer.isActive());
        QCOMPARE(net.requests.size(), 6);
        QCOMPARE(sc.m_queue.size(), 1);
        QVERIFY(sc.state()["waveError"].toString().isEmpty());
        QVERIFY(sc.m_waveLoadSeeds);
        QCOMPARE(sc.m_waveRetryTimer.interval(), 3000);
        net.responses = {
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:20\"}]}"},
            {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:2\"},{\"urn\":\"soundcloud:tracks:3\"},{\"urn\":\"soundcloud:tracks:4\"},{\"urn\":\"soundcloud:tracks:5\"},{\"urn\":\"soundcloud:tracks:6\"},{\"urn\":\"soundcloud:tracks:7\"},{\"urn\":\"soundcloud:tracks:8\"}]}"},
            {403, "{}"} // No real stream request leaves the test transport.
        };
        sc.m_waveRetryTimer.start(1);
        QTRY_COMPARE(sc.m_queueIndex, 1);
        QCOMPARE(sc.m_queue.size(), 8);
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
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:3\"},{\"urn\":\"soundcloud:tracks:4\"},{\"urn\":\"soundcloud:tracks:5\"},{\"urn\":\"soundcloud:tracks:6\"},{\"urn\":\"soundcloud:tracks:7\"},{\"urn\":\"soundcloud:tracks:8\"}]}", 30}};
        sc.next(); sc.pausePlayback();
        QTRY_COMPARE(sc.m_queue.size(), 7);
        QCOMPARE(sc.m_queueIndex, 0);
        QCOMPARE(sc.state()["current"].toMap(), current);
        QVERIFY(!sc.state()["playbackActive"].toBool());
        QCOMPARE(net.requests.size(), 1);
    }
    void waveDeduplicatesRelatedAndCancelsLateReplies() {
        Network net;
        SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_waveMode = "discovery";
        sc.m_waveSeeds = {QVariantMap{{"urn", "soundcloud:tracks:1"}}};
        sc.m_waveSeen.insert("soundcloud:tracks:1");
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"},{\"urn\":\"soundcloud:tracks:2\"},{\"urn\":\"soundcloud:tracks:2\"}]}"}};
        sc.refillWave();
        QTRY_VERIFY(!sc.state()["waveBusy"].toBool());
        QCOMPARE(sc.m_queue.size(), 1);
        QCOMPARE(sc.m_queue.first().toMap()["urn"].toString(), QString("soundcloud:tracks:2"));
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:3\"}]}", 30}};
        sc.refillWave(); sc.stopWave();
        QTest::qWait(60);
        QCOMPARE(sc.m_queue.size(), 1);
        QVERIFY(sc.state()["waveMode"].toString().isEmpty());
    }
    void waveEmptyLibraryAndNetworkFailureAllowRetry() {
        Network net;
        SoundCloud sc(nullptr, &net, secrets); sc.set("configured", false);
        sc.m_access = "test-access"; sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.set("signedIn", true);
        net.responses = {{200, "{\"collection\":[]}"}};
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
        auto manualAdded = added; manualAdded["scripManualQueue"] = true;
        QCOMPARE(sc.state()["queue"].toList(), QVariantList({first, current, manualAdded, last}));
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
    void queueBulkEditsPreserveCurrentAndPosition() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}}, b{{"urn", "soundcloud:tracks:2"}}, c{{"urn", "soundcloud:tracks:3"}};
        sc.m_queue = {a, b, a, c, b}; sc.m_queueIndex = 2;
        sc.set("current", a); sc.set("position", 12345);
        auto *active = sc.m_activePlayer;
        const auto generation = sc.m_preloadGeneration;
        sc.m_waveMode = "discovery"; sc.set("waveMode", "discovery");
        const auto waveGeneration = sc.m_waveGeneration;
        sc.deduplicateQueue();
        QCOMPARE(sc.m_queue, QVariantList({b, a, c})); QCOMPARE(sc.m_queueIndex, 1);
        QCOMPARE(sc.position(), 12345); QCOMPARE(sc.m_activePlayer, active);
        QCOMPARE(sc.state()["current"].toMap(), a);
        QVERIFY(sc.m_preloadGeneration > generation); QVERIFY(sc.m_waveGeneration > waveGeneration);
        QVERIFY(sc.m_waveMode.isEmpty()); QVERIFY(!sc.queueHasDuplicates());
        sc.clearQueueAfterCurrent(); QCOMPARE(sc.m_queue, QVariantList({b, a}));
        QCOMPARE(sc.m_queueIndex, 1); QCOMPARE(sc.position(), 12345);
        sc.clearQueueAfterCurrent(); QCOMPARE(sc.m_queue.size(), 2);
        sc.m_queueIndex = -1; sc.clearQueueAfterCurrent(); QCOMPARE(sc.m_queue.size(), 2);
        sc.m_queue = {a, a, b, b, c}; sc.deduplicateQueue();
        QCOMPARE(sc.m_queue, QVariantList({a, b, c})); QCOMPARE(sc.m_queueIndex, -1);
        sc.m_queue = {}; sc.deduplicateQueue(); sc.clearQueueAfterCurrent(); QVERIFY(sc.m_queue.isEmpty());
    }
    void queueClearRejectsLateWaveAndPersistsEditedSnapshot() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        sc.set("signedIn", true); sc.set("profile", QVariantMap{{"urn", "soundcloud:users:queue-test"}});
        sc.m_restoreChecked = true;
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}}, b{{"urn", "soundcloud:tracks:2"}};
        sc.m_queue = {a, b}; sc.m_queueIndex = 0; sc.set("current", a); sc.set("position", 3210);
        sc.m_waveMode = "discovery"; sc.m_waveSeeds = {a};
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:3\"}]}", 30}};
        sc.refillWave(); sc.clearQueueAfterCurrent(); QTest::qWait(70);
        QCOMPARE(sc.m_queue, QVariantList({a})); QCOMPARE(sc.position(), 3210);
        QVERIFY(sc.m_waveMode.isEmpty()); QVERIFY(!sc.state()["waveBusy"].toBool());
        const auto saved = QJsonDocument::fromJson(QSettings().value("playback/session").toByteArray()).object();
        QCOMPARE(saved["queue"].toArray().size(), 1); QCOMPARE(saved["index"].toInt(), 0);
        QCOMPARE(saved["position"].toInt(), 3210); QVERIFY(saved["waveMode"].toString().isEmpty());
    }
    void playlistCatalogPaginationPreservesRepeatedRows() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"},{\"urn\":\"soundcloud:tracks:1\"}],\"next_href\":\"https://api.soundcloud.com/playlists/2/tracks?cursor=next\"}"},
                         {200, "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"},{\"urn\":\"soundcloud:tracks:2\"}]}"}};
        sc.openPlaylist({{"urn", "soundcloud:playlists:2"}, {"title", "Duplicates"}});
        QTRY_VERIFY(!sc.state()["busy"].toBool());
        QCOMPARE(sc.trackModel()->rowCount(), 2);
        sc.more(); QTRY_VERIFY(!sc.state()["busy"].toBool());
        const auto rows = sc.state()["tracks"].toList(); QCOMPARE(rows.size(), 4);
        QCOMPARE(rows[0], rows[1]); QCOMPARE(rows[1], rows[2]);
    }
    void selectedTracksQueueIsAtomicAndOrdered() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}, {"access", "blocked"}}, b{{"urn", "soundcloud:tracks:2"}, {"access", "blocked"}};
        sc.m_queue = {b}; sc.m_queueIndex = 0;
        QVERIFY(sc.enqueueTracks({a, b, a}).isEmpty());
        auto manualA = a, manualB = b;
        manualA["scripManualQueue"] = true; manualB["scripManualQueue"] = true;
        QCOMPARE(sc.m_queue, QVariantList({b, manualA, manualB, manualA})); QCOMPARE(sc.m_queueIndex, 0);
        QVERIFY(!sc.enqueueTracks({a, QVariantMap{}}).isEmpty());
        QCOMPARE(sc.m_queue.size(), 4);
        sc.m_queue = QVariantList(10000, a);
        QVERIFY(!sc.enqueueTracks({b}).isEmpty()); QCOMPARE(sc.m_queue.size(), 10000);
    }
    void selectedTracksPlaylistBatchAndErrors() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool()); sc.set("signedIn", true);
        sc.set("profile", QVariantMap{{"urn", "soundcloud:users:1"}});
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}}, b{{"urn", "soundcloud:tracks:2"}};
        const QByteArray metadata = "{\"urn\":\"soundcloud:playlists:2\",\"user_urn\":\"soundcloud:users:1\",\"track_count\":2}";
        const QByteArray page = "{\"collection\":[{\"urn\":\"soundcloud:tracks:1\"},{\"urn\":\"soundcloud:tracks:1\"}]}";
        net.responses = {{200, metadata}, {200, page}, {200, metadata}, {200, page}, {200, "{}", 30}};
        QSignalSpy finished(&sc, &SoundCloud::selectionSaved);
        sc.saveTracksToPlaylist({b, a, b}, "soundcloud:playlists:2", "", "batch");
        sc.saveTracksToPlaylist({a}, "", "Parallel", "parallel");
        QCOMPARE(finished.size(), 1); QCOMPARE(finished[0][1].toBool(), false);
        QTRY_COMPARE(finished.size(), 2); QCOMPARE(finished[1][0].toString(), QString("batch")); QVERIFY(finished[1][1].toBool());
        const auto entries = QJsonDocument::fromJson(net.bodies.last()).object()["playlist"].toObject()["tracks"].toArray();
        QCOMPARE(entries.size(), 5);
        for (int i = 0; i < 5; ++i) QCOMPARE(entries[i].toObject()["urn"].toString(), QString("soundcloud:tracks:%1").arg(i == 2 || i == 4 ? 2 : 1));
        QCOMPARE(net.methods.count("PUT"), 1);
        net.responses = {{422, "{}"}};
        sc.saveTracksToPlaylist({a, a}, "", "Retry", "error");
        QTRY_COMPARE(finished.size(), 3); QVERIFY(!finished[2][1].toBool()); QVERIFY(!finished[2][2].toString().isEmpty());
        const auto before = net.requests.size();
        sc.saveTracksToPlaylist(QVariantList(501, a), "", "Too large", "limit");
        QCOMPARE(net.requests.size(), before); QVERIFY(finished.last()[2].toString().contains("500"));
        net.responses = {{201, "{}", 80}};
        sc.saveTracksToPlaylist({a}, "", "Old account", "stale");
        QTRY_COMPARE(net.requests.size(), before + 1);
        sc.set("profile", QVariantMap{{"urn", "soundcloud:users:2"}});
        const auto completions = finished.size(); QTest::qWait(120); QCOMPARE(finished.size(), completions);
    }
    void selectedTracksRejectCombinedPlaylistLimit() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool()); sc.set("signedIn", true);
        sc.set("profile", QVariantMap{{"urn", "soundcloud:users:1"}});
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}};
        QJsonArray tracks;
        for (int i = 0; i < 499; ++i) tracks.append(QJsonObject::fromVariantMap(a));
        const auto page = QJsonDocument(QJsonObject{{"collection", tracks}}).toJson();
        const QByteArray metadata = "{\"urn\":\"soundcloud:playlists:2\",\"user_urn\":\"soundcloud:users:1\",\"track_count\":499}";
        net.responses = {{200, metadata}, {200, page}, {200, metadata}, {200, page}};
        const auto before = net.methods.count("PUT");
        QSignalSpy finished(&sc, &SoundCloud::selectionSaved);
        sc.saveTracksToPlaylist({a, a}, "soundcloud:playlists:2", "", "limit");
        QTRY_COMPARE(finished.size(), 1); QVERIFY(!finished[0][1].toBool());
        QVERIFY(finished[0][2].toString().contains("500")); QCOMPARE(net.methods.count("PUT"), before);
        QVERIFY(!sc.state()["savingTrack"].toBool());
    }
    void playlistDestinationsRejectOldAccountResponse() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool()); sc.set("signedIn", true);
        sc.set("profile", QVariantMap{{"urn", "soundcloud:users:1"}});
        net.responses = {{200, "{\"collection\":[{\"urn\":\"soundcloud:playlists:old\"}]}", 80},
                         {200, "{\"collection\":[{\"urn\":\"soundcloud:playlists:new\"}]}", 140}};
        const auto before = net.requests.size();
        sc.listDestinations(); QTRY_COMPARE(net.requests.size(), before + 1);
        sc.set("profile", QVariantMap{{"urn", "soundcloud:users:2"}});
        QVERIFY(!sc.state()["loadingDestinations"].toBool());
        sc.listDestinations(); QTRY_COMPARE(net.requests.size(), before + 2);
        QTest::qWait(100); QVERIFY(sc.state()["destinations"].toList().isEmpty());
        QVERIFY(sc.state()["loadingDestinations"].toBool());
        QTRY_VERIFY(!sc.state()["loadingDestinations"].toBool());
        QCOMPARE(sc.state()["destinations"].toList().size(), 1);
        QCOMPARE(sc.state()["destinations"].toList()[0].toMap()["urn"].toString(), QString("soundcloud:playlists:new"));
    }
    void queuePlaylistWritesPreserveOrderAndRejectParallelRequests() {
        Network net; net.responses = {guest()}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool()); sc.set("signedIn", true);
        const QVariantMap a{{"urn", "soundcloud:tracks:1"}}, b{{"urn", "soundcloud:tracks:2"}};
        sc.m_queue = {a, b, a};
        net.responses = {{422, "{}", 30}};
        const int before = net.requests.size();
        sc.saveQueueAsPlaylist("Queue title"); sc.saveQueueAsPlaylist("Second title");
        QTRY_COMPARE(net.requests.size(), before + 1);
        const auto playlist = QJsonDocument::fromJson(net.bodies.last()).object()["playlist"].toObject();
        QCOMPARE(playlist["set_type"].toString(), QString("playlist"));
        const auto tracks = playlist["tracks"].toArray(); QCOMPARE(tracks.size(), 3);
        QCOMPARE(tracks[0], tracks[2]); QVERIFY(tracks[0] != tracks[1]);
        QTRY_VERIFY(!sc.state()["queuePlaylistBusy"].toBool());
        QVERIFY(!sc.state()["queuePlaylistError"].toString().isEmpty());
        net.responses = {{201, "{\"urn\":\"soundcloud:playlists:1\"}", 30}};
        QSignalSpy saved(&sc, &SoundCloud::queuePlaylistSaved);
        sc.saveQueueAsPlaylist("Queue title"); QTRY_VERIFY(sc.state()["queuePlaylistBusy"].toBool());
        QTRY_COMPARE(net.requests.size(), before + 2);
        sc.set("profile", QVariantMap{{"urn", "soundcloud:users:new-account"}});
        sc.set("queuePlaylistError", "new account"); QTest::qWait(60);
        QCOMPARE(saved.size(), 0); QCOMPARE(sc.state()["queuePlaylistError"].toString(), QString("new account"));
        sc.m_queue.clear(); for (int i = 0; i < 501; ++i) sc.m_queue.append(a);
        sc.saveQueueAsPlaylist("Too large"); QCOMPARE(net.requests.size(), before + 2);
        QVERIFY(sc.state()["queuePlaylistError"].toString().contains("500"));
        sc.m_queue = {a}; net.responses = {{201, "{}"}};
        sc.saveQueueAsPlaylist("Retry"); QTRY_COMPARE(saved.size(), 1);
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
        sc.set("engagement", QVariantMap{{"playlistLikes", QVariantMap{{"soundcloud:playlists:old", true}}}});
        sc.set("engagementReady", QVariantMap{{"playlistLikes", true}});
        sc.set("followings", QVariantMap{{"soundcloud:users:old", true}}); sc.set("followingsReady", true);
        sc.set("playlistDraft", QVariantMap{{"urn", "soundcloud:playlists:old"}});
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
        QVERIFY(sc.state()["engagement"].toMap().isEmpty()); QVERIFY(sc.state()["engagementReady"].toMap().isEmpty());
        QVERIFY(sc.state()["followings"].toMap().isEmpty()); QVERIFY(!sc.state()["followingsReady"].toBool());
        QVERIFY(sc.state()["playlistDraft"].toMap().isEmpty());
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
    void desktopRepeatModesUseExistingControl() {
        Network net; SoundCloud sc(nullptr, &net, secrets);
        sc.set("signedIn", true);
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("sc", &sc);
        engine.load(QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        std::function<QQuickItem *(QQuickItem *)> findRepeat = [&](QQuickItem *item) -> QQuickItem * {
            if (item->objectName() == "repeatModeButton") return item;
            for (auto *child : item->childItems()) if (auto *found = findRepeat(child)) return found;
            return nullptr;
        };
        for (int width : {1100, 1440}) {
            window->resize(width, 900);
            for (int mode = 0; mode < 3; ++mode) {
                sc.setRepeatMode(mode); QTest::qWait(80);
                auto *button = findRepeat(window->contentItem()); QVERIFY(button); QVERIFY(button->isVisible());
                QCOMPARE(button->property("text").toString(), QStringList({"Повтор выключен", "Повтор трека", "Повтор очереди"})[mode]);
                QCOMPARE(button->property("symbol").toString(), QStringList({"repeatOff", "repeatOne", "repeat"})[mode]);
                QCOMPARE(QQmlProperty(button, "Accessible.name", QQmlEngine::contextForObject(button)).read().toString(), QStringList({"Повтор выключен", "Повтор трека", "Повтор очереди"})[mode]);
                const auto rect = button->mapRectToScene(button->boundingRect());
                QVERIFY(rect.left() >= 0); QVERIFY(rect.right() <= width);
                QVERIFY(rect.top() >= 0); QVERIFY(rect.bottom() <= window->height());
                QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
                QCOMPARE(sc.state()["repeatMode"].toInt(), (mode + 1) % 3);
                sc.setRepeatMode(mode); QTest::qWait(20);
                QVERIFY(window->grabWindow().save(QString("repeat-desktop-%1-%2.png").arg(width).arg(mode)));
            }
        }
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
        QQmlExpression menu(QQmlEngine::contextForObject(window), window, "queueOptions.open()");
        menu.evaluate(); QVERIFY2(!menu.hasError(), qPrintable(menu.error().toString()));
        QTest::qWait(250);
        QVERIFY(window->grabWindow().save(QString(SCRIP_SOURCE_DIR) + "/build/queue-menu-desktop.png"));

    }
    void commentsUiRendersAndFollowsPlayback() {
        Network net;
        SoundCloud sc(nullptr, &net, secrets);
        sc.set("configured", false); sc.m_access = "test-access";
        sc.m_expires = QDateTime::currentSecsSinceEpoch() + 3600;
        sc.m_homeLoaded = true;
        sc.set("signedIn", true);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("sc", &sc);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError> &errors) { for (const auto &error : errors) warnings.append(error.toString()); });
        engine.load(QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->setWidth(1600); window->setHeight(1000);
        auto *prefs = window->findChild<QObject *>("uiPreferences"); QVERIFY(prefs);
        prefs->setProperty("motion", true); prefs->setProperty("showComments", true);
        prefs->setProperty("commentsLeft", false); prefs->setProperty("timedComments", false);
        const QVariantMap track{{"urn", "soundcloud:tracks:12345"}, {"title", "Линии света"}, {"duration", 200000}, {"genre", "Electronic"},
            {"user", QVariantMap{{"username", "Тихий берег"}, {"urn", "soundcloud:users:42"}}}};
        net.responses = {{200, "[]"}};
        sc.set("current", track);
        QTRY_VERIFY(!sc.commentsState()["busy"].toBool());
        QVariantList comments;
        for (int i = 0; i < 12; ++i) comments.append(QVariantMap{{"urn", QString("soundcloud:comments:%1").arg(i)},
            {"body", QStringList{"Красивый переход. Здесь хочется поставить на повтор.", "Бас на этом моменте — отдельная история", "Слушаю в наушниках, очень объёмный звук."}[i % 3]},
            {"timestamp", i == 0 ? -1 : i * 10000}, {"created_at", "2026-11-15T12:00:00Z"},
            {"user", QVariantMap{{"username", QStringList{"anastasia", "slow motion", "Миша"}[i % 3]}, {"urn", "soundcloud:users:42"}}}});
        sc.updateComments("items", comments);
        sc.set("position", 42000);
        auto *floating = window->findChild<QQuickItem *>("floatingCommentsPane"); QVERIFY(floating);
        QTRY_VERIFY(floating->isVisible());
        for (bool dark : {false, true}) {
            prefs->setProperty("darkMode", dark); QTest::qWait(300);
            QVERIFY(window->grabWindow().save(dark ? "ui-comments-right-dark.png" : "ui-comments-right-light.png"));
        }
        prefs->setProperty("commentsLeft", true); QTest::qWait(300);
        QVERIFY(floating->x() < 210);
        QVERIFY(window->grabWindow().save("ui-comments-left.png"));
        prefs->setProperty("commentsLeft", false);
        const QVariant pageTrack = track;
        QVERIFY(QMetaObject::invokeMethod(window, "openTrack", Q_ARG(QVariant, pageTrack), Q_ARG(QVariant, true)));
        auto *page = window->findChild<QObject *>("trackPage"); QVERIFY(page);
        QTRY_VERIFY(page->property("opened").toBool());
        auto *pane = window->findChild<QQuickItem *>("trackCommentsPane"); QVERIFY(pane);
        QTRY_VERIFY(pane->isVisible());
        // Opening details uses the same loaded comments without another network request.
        QCOMPARE(sc.commentsState()["items"].toList().size(), 12);
        prefs->setProperty("timedComments", true);
        QTRY_COMPARE(pane->property("activeIndex").toInt(), 3);
        sc.set("position", 5000); QTRY_COMPARE(pane->property("activeIndex").toInt(), -1);
        sc.set("position", 42000); QTest::qWait(300);
        QVERIFY(window->grabWindow().save("ui-comments-timed.png"));
        prefs->setProperty("timedComments", false); QTest::qWait(300);
        QVERIFY(window->grabWindow().save("ui-comments-details.png"));
        window->setWidth(760); window->setHeight(680); QTest::qWait(300);
        QVERIFY(window->grabWindow().save("ui-comments-narrow.png"));
        for (const auto &warning : warnings)
            QVERIFY2(!warning.contains("ReferenceError") && !warning.contains("TypeError") && !warning.contains("Binding loop") && !warning.contains("Cannot assign"), qPrintable(warning));
    }

    void qmlScreensRender() {
        QFile session(config + "/session.json"); QVERIFY(session.open(QIODevice::WriteOnly));
        session.write(QJsonDocument(QJsonObject{{"client_id", "test-client"}, {"access_token", "test-access"}, {"refresh_token", "test-refresh"}, {"expires_at", QDateTime::currentSecsSinceEpoch()+3600}, {"user_session", true}}).toJson()); session.close();
        Network net; net.responses = {{200, "{\"username\":\"Тестовый аккаунт\"}"}, {200, "[]"}}; SoundCloud sc(nullptr, &net, secrets);
        QTRY_VERIFY(sc.state()["connected"].toBool());
        QTRY_VERIFY(!sc.state()["busy"].toBool());
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("sc", &sc);
        Network lyricsNet; lyricsNet.responseHosts = {"lrclib.net"};
        const QByteArray lyricFixture = "{\"id\":42,\"trackName\":\"A track page\",\"artistName\":\"Test artist\",\"syncedLyrics\":\"[00:00.00]Every sound has a story\\n[00:10.00]Every word finds its way\\n[00:20.00]Stay a little longer\\n[00:30.00]Let the music play\"}";
        lyricsNet.responses = {{200, lyricFixture}, {200, lyricFixture}, {200, lyricFixture}};
        Lyrics lyrics(nullptr, &lyricsNet);
        engine.rootContext()->setContextProperty("lyricsBackend", &lyrics);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError> &errors) { for (const auto &error : errors) warnings.append(error.toString()); });
        net.responses = {{200, "[{\"urn\":\"soundcloud:tracks:10\",\"title\":\"\u0420\u0435\u043b\u0438\u0437\"}]"}, {200, "[]"}, {200, "[{\"urn\":\"soundcloud:tracks:11\",\"title\":\"\u041e\u0441\u043d\u043e\u0432\u0430\"}]"}, {200, "[{\"urn\":\"soundcloud:tracks:12\",\"title\":\"\u041f\u043e\u0445\u043e\u0436\u0438\u0439\"}]"}};
        engine.load(QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty()); auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QCOMPARE(window->property("api").toMap(), sc.state());
        QTRY_COMPARE(sc.state()["home"].toList().size(), 3);
        QTRY_VERIFY(!sc.state()["homeBusy"].toBool());
        QCOMPARE(window->property("section").toInt(), 5);
        for (const auto size : {QSize(1320, 820), QSize(760, 680)}) {
            window->resize(size); QTest::qWait(100);
            QVERIFY(window->grabWindow().save(QString("ui-recommendations-home-%1.png").arg(size.width())));
        }
        window->setProperty("section", 0);
        window->setWidth(1320); window->setHeight(820);
        auto *preferences = window->findChild<QObject *>("uiPreferences"); QVERIFY(preferences);
        preferences->setProperty("gridView", false);
        preferences->setProperty("showComments", false);
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
        net.responses = {{200, "[{\"urn\":\"soundcloud:tracks:99\",\"title\":\"\u0421\u043b\u0435\u0434\u0443\u044e\u0449\u0430\u044f \u0441\u0442\u0440\u0430\u043d\u0438\u0446\u0430\"}]"}};
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
                // Model notifications precede the view's deferred layout and scroll reset.
                QCoreApplication::processEvents();
                QVERIFY(QMetaObject::invokeMethod(trackView, "forceLayout"));
                net.responses = {{200, tracksJson(1, false, 5000)}};
                const qreal end = trackView->property("originY").toReal() + trackView->property("contentHeight").toReal() - trackView->property("height").toReal();
                trackView->setProperty("contentY", end);
                QTRY_VERIFY(trackView->property("contentY").toReal() >= end - 1);
                QTRY_VERIFY(trackView->property("activeDelegateCount").toInt() <= 40);
                QTRY_VERIFY(trackView->property("activeDelegateCount").toInt() > 0);
                // Pagination may append another track before the delegate is instantiated.
                QTRY_VERIFY(([&] {
                    QQuickItem *lastVisible = nullptr;
                    return QMetaObject::invokeMethod(trackView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, lastVisible), Q_ARG(int, 4999))
                        && lastVisible && lastVisible->property("modelData").toMap()["title"].toString() == QString("Виртуальный трек 4999");
                })());
                QTRY_COMPARE(sc.state()["tracks"].toList().size(), 5001);
                QTRY_VERIFY(!sc.state()["hasMore"].toBool());
                const qreal appendedEnd = trackView->property("originY").toReal() + trackView->property("contentHeight").toReal() - trackView->property("height").toReal();
                trackView->setProperty("contentY", appendedEnd);
                QVariant lastTitle;
                QTRY_VERIFY(QMetaObject::invokeMethod(trackView, "lastTrackTitle", Q_RETURN_ARG(QVariant, lastTitle))
                    && lastTitle.toString() == QString("Виртуальный трек 5000"));
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
        for (const auto name : {"playbackQualityControl", "preloadControl"}) {
            auto *control = window->findChild<QQuickItem *>(QString::fromLatin1(name)); QVERIFY(control);
            const auto rect = control->mapRectToScene(control->boundingRect());
            QVERIFY(rect.left() >= 0 && rect.right() <= window->width());
            QVERIFY(!QQmlProperty(control, "Accessible.name", QQmlEngine::contextForObject(control)).read().toString().isEmpty());
        }
        net.responses = {{200, gridTracksJson(3)}};
        window->setProperty("query", "virtual-return");
        window->setProperty("section", 0);
        QTRY_VERIFY(!sc.state()["busy"].toBool());
        QTRY_COMPARE(sc.state()["tracks"].toList().size(), 3);
        QVERIFY(window->findChild<QObject *>("coverGridView"));
        net.responses = {{200, "[]"}};
        window->setProperty("section", 3); QTest::qWait(400);
        QVERIFY(window->grabWindow().save("ui-profile.png"));
        const QVariantMap publicUser{{"urn", "soundcloud:users:public"}, {"username", "North Records"}};
        sc.set("visitedProfile", publicUser);
        window->setProperty("guest", publicUser);
        for (const auto size : {QSize(1100, 820), QSize(1440, 900)}) {
            window->resize(size);
            for (int tab : {0, 1, 2, 3}) {
                net.responses = {{200, "[]"}};
                window->setProperty("profileTab", tab);
                QTest::qWait(400);
                auto *navigation = window->findChild<QQuickItem *>("profileNavigation"); QVERIFY(navigation);
                QCOMPARE(navigation->property("section").toInt(), tab);
                QVERIFY(window->grabWindow().save(QString("public-profile-desktop-%1-%2.png").arg(size.width()).arg(tab)));
            }
        }
        window->setProperty("guest", QVariant());
        window->setProperty("profileTab", 0);

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
        const QVariant pageTrack = QVariantMap{{"urn", "soundcloud:tracks:details"}, {"title", "коза (p. youngslashprod)"}, {"metadata_artist", "Credited musician"},
            {"duration", 185000}, {"description", "Track description with enough detail to wrap across lines."},
            {"user", QVariantMap{{"username", "Test artist"}, {"urn", "soundcloud:users:details"}}}};
        QVERIFY(QMetaObject::invokeMethod(window, "openTrack", Q_ARG(QVariant, pageTrack), Q_ARG(QVariant, false)));
        auto *detailsPage = window->findChild<QObject *>("trackPage"); QVERIFY(detailsPage);
        QTRY_VERIFY(detailsPage->property("opened").toBool());
        QCOMPARE(detailsPage->property("track").toMap()["title"].toString(), QString("коза (p. youngslashprod)"));
        QCOMPARE(sc.state()["current"].toMap(), currentBeforePage);
        QTRY_COMPARE(lyrics.state()["lines"].toList().size(), 4);
        QVERIFY(!lyricsNet.requests.isEmpty());
        QCOMPARE(QUrlQuery(lyricsNet.requests.last().url()).queryItemValue("artist_name"), "Credited musician");
        QCOMPARE(detailsPage->property("track").toMap()["user"].toMap()["urn"].toString(), "soundcloud:users:details");
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
        auto *commentsColumn = window->findChild<QQuickItem *>("trackCommentsPane"); QVERIFY(commentsColumn);
        QVERIFY(commentsColumn->x() > trackColumn->x() + trackColumn->width());
        QVERIFY(lyricsColumn->y() >= trackColumn->y() + trackColumn->height());
        QVERIFY(lyricsColumn->width() > trackColumn->width());
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
        auto *pageScroll = window->findChild<QObject *>("trackDetailsScroll"); QVERIFY(pageScroll);
        pageScroll->setProperty("contentY", lyricsColumn->y());
        QTest::qWait(100);
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
        pageScroll->setProperty("contentY", lyricsColumn->y()); QTest::qWait(100);
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
        QPointer<QQuickItem> stationCard = findStationCard();
        // Like requests publish pending, count and collection changes separately.
        // None of them should rebuild home shelves and reload their artwork.
        net.responses = {{200, "{}"}};
        sc.like(stationSeed);
        QTRY_VERIFY(sc.m_likedTracks.contains(stationSeed.value("urn").toString()));
        QCoreApplication::processEvents();
        QVERIFY(stationCard);
        QCOMPARE(findStationCard(), stationCard.data());
        auto updatedSeed = stationCard->property("modelData").toMap();
        QVERIFY(updatedSeed.value("user_favorite").toBool());
        net.responses = {{200, "{}"}};
        sc.like(updatedSeed);
        QTRY_VERIFY(!sc.m_likedTracks.contains(stationSeed.value("urn").toString()));
        QCoreApplication::processEvents();
        QVERIFY(stationCard);
        QCOMPARE(findStationCard(), stationCard.data());
        QVERIFY(!stationCard->property("modelData").toMap().value("user_favorite").toBool());
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
