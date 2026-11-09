#include "soundcloud.h"
#include <utility>
#include <algorithm>
#include <memory>
#include <QSet>
#include <QBuffer>
#include <QPointer>
#include <QImage>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSettings>
#include <QGuiApplication>
#include <QClipboard>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QVersionNumber>
#include <QProcess>
#ifdef Q_OS_LINUX
#include <mutex>
#include <thread>
#include <QThread>
#endif
#include <QMediaDevices>
#include <QAudioDevice>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

#ifdef Q_OS_LINUX
struct SoundCloudStorageCompletion {
    std::mutex mutex;
    SoundCloud *owner = nullptr;
};
#endif
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
QByteArray callbackPage(bool valid, bool receivedCode) {
    static const QByteArray page = [] {
        QFile file(":/resources/callback.html");
        if (!file.open(QIODevice::ReadOnly)) return QByteArray{};
        auto html = file.readAll();
        QImage logo(":/qt/qml/SCrip/assets/blob.png");
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        if (!logo.isNull())
            logo.scaledToWidth(160, Qt::SmoothTransformation).save(&buffer, "PNG");
        html.replace("LOGO_DATA", buffer.data().toBase64());
        return html;
    }();
    auto html = page;
    if (!valid)
        return html.replace("PAGE_TITLE", "Начните вход в SCrip").replace("PAGE_MESSAGE", "Откройте приложение и нажмите «Войти в SoundCloud».");
    if (!receivedCode)
        return html.replace("PAGE_TITLE", "Вход не завершён").replace("PAGE_MESSAGE", "Вернитесь в приложение, чтобы попробовать ещё раз.");
    return html.replace("PAGE_TITLE", "Вернитесь в SCrip").replace("PAGE_MESSAGE", "Аккаунт SoundCloud подключён. Эту вкладку можно закрыть.");
}
QString nonce() {
    QByteArray bytes(32, Qt::Uninitialized);
    for (auto &c : bytes) c = char(QRandomGenerator::system()->generate() & 255);
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
QVariantList items(const QJsonDocument &doc) {
    QJsonArray array = doc.isArray() ? doc.array() : doc.object().value("collection").toArray();
    QVariantList result;
    for (const auto &value : array) {
        auto item = value.toObject();
        if (item.contains("origin")) item = item.value("origin").toObject();
        if (item.contains("track")) item = item.value("track").toObject();
        if (!item.isEmpty()) result.append(item.toVariantMap());
    }
    return result;
}
QString apiPath(const QString &kind, const QString &urn) {
    return "/" + kind + "/" + QString::fromLatin1(QUrl::toPercentEncoding(urn));
}
}

SoundCloud::SoundCloud(QObject *parent, QNetworkAccessManager *network, std::shared_ptr<SecretStore> secrets) : QObject(parent),
    m_systemStore(!secrets),
    m_authStorage(secrets ? std::move(secrets) : systemSecretStore()),
    m_network(network ? network : new QNetworkAccessManager(this)) {
    auto dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/SCrip";
    QDir().mkpath(dir);
    m_configPath = dir + "/soundcloud.json";
    m_sessionPath = dir + "/session.json";
    m_state = {{"configured", false}, {"connected", false}, {"signedIn", false}, {"busy", false},
        {"home", QVariantList{}}, {"homeBusy", false}, {"homeError", ""}, {"authBusy", false}, {"error", ""}, {"status", "Подключите SoundCloud"}, {"tracks", QVariantList{}},
        {"queue", QVariantList{}}, {"profile", QVariantMap{}}, {"current", QVariantMap{}}, {"restoreCleared", false},
        {"playing", false}, {"playbackActive", false}, {"position", 0}, {"duration", 0}, {"hasMore", false},
        {"heading", "Откройте свою музыку"}, {"configPath", m_configPath}, {"repeat", false}, {"shuffle", false}, {"crossfade", false},
        {"updateChecking", false}, {"updateAvailable", false}, {"updateError", ""}, {"updateResult", ""}, {"updateManual", false}};
#ifdef Q_OS_LINUX
    if (m_systemStore) {
        auto storage = m_authStorage;
        const auto configPath = m_configPath, sessionPath = m_sessionPath;
        const bool autoConnect = !network;
        m_storageCompletion = std::make_shared<SoundCloudStorageCompletion>();
        m_storageCompletion->owner = this;
        auto completion = m_storageCompletion;
        std::thread([storage, configPath, sessionPath, completion, autoConnect]() mutable {
            QJsonObject connection, session;
            const bool ok = storage.load("connection", configPath, connection)
                && storage.load("session", sessionPath, session);
            std::lock_guard lock(completion->mutex);
            if (completion->owner) {
                auto *owner = completion->owner;
                QMetaObject::invokeMethod(owner, [owner, ok, connection, session, autoConnect] {
                    owner->finishStorageLoad(ok, connection, session, autoConnect);
                }, Qt::QueuedConnection);
            }
        }).detach();
    } else {
        QJsonObject connection, session;
        m_storageReady = m_authStorage.load("connection", m_configPath, connection)
            && m_authStorage.load("session", m_sessionPath, session);
        if (m_storageReady) {
            m_connection = connection; loadConfig();
            if (session.value("client_id").toString() == m_clientId && (!m_useWorker || session.value("user_session").toBool())) {
                m_access = session.value("access_token").toString(); m_refresh = session.value("refresh_token").toString();
                m_expires = session.value("expires_at").toVariant().toLongLong(); m_userSession = session.value("user_session").toBool();
            }
        } else fail(AuthStorage::errorMessage());
    }
#else
    QJsonObject connection, session;
    m_storageReady = m_authStorage.load("connection", m_configPath, connection)
        && m_authStorage.load("session", m_sessionPath, session);
    if (m_storageReady) {
        m_connection = connection;
        loadConfig();
        if (session.value("client_id").toString() == m_clientId && (!m_useWorker || session.value("user_session").toBool())) {
            m_access = session.value("access_token").toString(); m_refresh = session.value("refresh_token").toString();
            m_expires = session.value("expires_at").toVariant().toLongLong(); m_userSession = session.value("user_session").toBool();
        }
    } else fail(AuthStorage::errorMessage());
#endif
    player().setAudioOutput(&m_audio);
    auto *devices = new QMediaDevices(this);
    const auto followOutput = [this] {
        // Оба плеера должны следовать системному выходу, включая горячее подключение.
        const auto output = QMediaDevices::defaultAudioOutput();
        m_audio.setDevice(output);
        m_preloadAudio.setDevice(output);
    };
    connect(devices, &QMediaDevices::audioOutputsChanged, this, followOutput);
    followOutput();
    QSettings preferences;
    m_audio.setVolume(preferences.value("player/volume", 0.7).toDouble());
    set("volume", m_audio.volume());
    set("crossfade", preferences.value("player/crossfade", false).toBool());
    m_volumeFade.setDuration(65);
    connect(&m_volumeFade, &QVariantAnimation::valueChanged, this, [this](const QVariant &level) {
        m_audio.setVolume(level.toFloat());
    });
    m_crossfade.setDuration(3000);
    connect(&m_crossfade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        const qreal progress = value.toReal();
        const qreal volume = m_state.value("volume").toReal();
        m_audio.setVolume(volume * (1.0 - progress));
        m_preloadAudio.setVolume(volume * progress);
    });
    connect(&m_crossfade, &QVariantAnimation::finished, this, [this] {
        m_preloadPlayer->stop(); m_preloadPlayer->setSource(QUrl{}); m_preloadPlayer->setAudioOutput(nullptr);
        player().setAudioOutput(nullptr); player().setAudioOutput(&m_audio);
        m_preloadPlayer->setAudioOutput(&m_preloadAudio);
        m_audio.setVolume(m_state.value("volume").toReal()); m_preloadAudio.setVolume(0);
        m_crossfadeStarted = false;
        prepareNext();
    });
    QList<QMediaPlayer *> players{&m_player, &m_sparePlayer};
    m_preloadAudio.setVolume(0);
    m_preloadPlayer->setAudioOutput(&m_preloadAudio);
    for (auto *media : players) {
        connect(media, &QMediaPlayer::positionChanged, this, [this, media](qint64 p) {
            if (media != m_activePlayer) return;
            set("position", p);
            const qint64 duration = media->duration();
            if (m_state.value("crossfade").toBool() && !m_crossfadeStarted && duration > 6000 && duration - p <= 3000)
                beginCrossfade();
        });
        connect(media, &QMediaPlayer::durationChanged, this, [this, media](qint64 d) { if (media == m_activePlayer) set("duration", d); });
        connect(media, &QMediaPlayer::playbackStateChanged, this, [this, media] { if (media == m_activePlayer) set("playing", media->playbackState() == QMediaPlayer::PlayingState); else holdPreloadedAudio(media); });
        connect(media, &QMediaPlayer::errorOccurred, this, [this, media](QMediaPlayer::Error e, const QString &) {
            if (media == m_activePlayer && e != QMediaPlayer::NoError) {
                pausePlayback(); set("buffering", false);
                failAndroidRestore();
                fail("Не удалось воспроизвести трек. Проверьте сеть или выберите другой трек.");
            }
        });
        connect(media, &QMediaPlayer::mediaStatusChanged, this, [this, media](QMediaPlayer::MediaStatus status) {
            if (media != m_activePlayer) {
                holdPreloadedAudio(media);
                return;
            }
            set("buffering", status == QMediaPlayer::LoadingMedia || status == QMediaPlayer::BufferingMedia);
            if (m_restoringPlayback && (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)) {
                if (m_restorePosition > 0) player().setPosition(qMin(m_restorePosition, player().duration()));
                m_restoringPlayback = false;
            }
            if (status == QMediaPlayer::EndOfMedia) {
                if (m_state.value("repeat").toBool()) startTrack(m_state.value("current").toMap()); else next();
            }
        });
    }
    connect(this, &SoundCloud::signedInChanged, this, &SoundCloud::tryRestoreAndroidTrack);
    connect(&m_callback, &QTcpServer::newConnection, this, &SoundCloud::callbackConnection);
    m_loginTimeout.setSingleShot(true); m_loginTimeout.setInterval(600000);
    connect(&m_loginTimeout, &QTimer::timeout, this, [this] { cancelLogin(); fail("Время ожидания входа истекло. Попробуйте снова."); });
    QTimer::singleShot(0, this, [this, network] {
        if (!network) checkForUpdates(false);
#if !defined(Q_OS_LINUX)
        if (m_state.value("configured").toBool() && (!m_useWorker || m_userSession)) connectCatalog();
#else
        if (!m_systemStore && m_state.value("configured").toBool() && (!m_useWorker || m_userSession)) connectCatalog();
#endif
    });
}
SoundCloud::~SoundCloud() {
#ifdef Q_OS_LINUX
    if (m_storageCompletion) {
        std::lock_guard lock(m_storageCompletion->mutex);
        m_storageCompletion->owner = nullptr;
    }
#endif
}
#ifdef Q_OS_LINUX
void SoundCloud::finishStorageLoad(bool ok, const QJsonObject &connection, const QJsonObject &session, bool autoConnect) {
    Q_ASSERT(QThread::currentThread() == thread());
    m_storageReady = ok;
    if (ok) {
        m_connection = connection;
        loadConfig();
        if (session.value("client_id").toString() == m_clientId && (!m_useWorker || session.value("user_session").toBool())) {
            m_access = session.value("access_token").toString(); m_refresh = session.value("refresh_token").toString();
            m_expires = session.value("expires_at").toVariant().toLongLong(); m_userSession = session.value("user_session").toBool();
        }
    } else fail(AuthStorage::errorMessage());
    if (autoConnect && m_state.value("configured").toBool() && (!m_useWorker || m_userSession)) connectCatalog();
}
#endif
void SoundCloud::set(const QString &key, const QVariant &value) {
    if (m_state.value(key) == value) return;
    m_state[key] = value;
    emit changed();
    if (key == "position" || key == "duration") emit progressChanged();
    else emit catalogChanged();
    if (key == "signedIn") emit signedInChanged();
}
void SoundCloud::fail(const QString &message) { set("error", message); }
void SoundCloud::clearError() { set("error", ""); }
bool SoundCloud::loadConfig() {
    if (!m_storageReady) { fail(AuthStorage::errorMessage()); return false; }
    m_useWorker = qEnvironmentVariable("SCRIP_AUTH_MODE") != "local";
    if (m_useWorker) {
        // Публичный OAuth-идентификатор нужен браузеру для PKCE.
        m_clientId = "Tg84lXkGKCC4ALkAzLAwO4q3J0d85NKe";
        m_clientSecret.clear();
        m_redirect = "http://127.0.0.1:43821/callback";
        set("configured", true);
        return true;
    }
    const auto &obj = m_connection;
    m_clientId = qEnvironmentVariable("SOUNDCLOUD_CLIENT_ID", obj.value("client_id").toString());
    m_clientSecret = qEnvironmentVariable("SOUNDCLOUD_CLIENT_SECRET", obj.value("client_secret").toString());
    m_redirect = obj.value("redirect_uri").toString("http://127.0.0.1:43821/callback");
    set("configured", !m_clientId.isEmpty() && !m_clientSecret.isEmpty());
    return m_state.value("configured").toBool();
}
void SoundCloud::configure(const QString &id, const QString &secret, const QString &redirect) {
    if (!m_storageReady) { fail(AuthStorage::errorMessage()); return; }
    QUrl url(redirect.trimmed());
    if (id.trimmed().isEmpty() || (secret.isEmpty() && m_clientSecret.isEmpty())) { fail("Укажите Client ID и Client Secret."); return; }
    if (url.scheme() != "http" || url.host() != "127.0.0.1" || (url.port() < 1024 || url.port() > 65535) || url.path() != "/callback" || url.hasQuery() || url.hasFragment() || !url.userInfo().isEmpty()) {
        fail("Адрес возврата должен иметь вид http://127.0.0.1:43821/callback (порт от 1024)."); return;
    }
    auto newSecret = secret.isEmpty() ? m_clientSecret : secret.trimmed();
    const QJsonObject connection{{"client_id", id.trimmed()}, {"client_secret", newSecret}, {"redirect_uri", url.toString()}};
    if (!m_authStorage.save("connection", connection)) { storageFailure(); return; }
    m_connection = connection;
    cancelLogin(); if (!clearSession()) return; loadConfig(); clearError(); connectCatalog();
}
void SoundCloud::storageFailure() {
    m_storageReady = false;
    set("connected", false); set("signedIn", false);
    ++m_homeGeneration; set("homeBusy", false);
    set("status", "Хранилище авторизации недоступно");
    fail(AuthStorage::errorMessage());
}
bool SoundCloud::saveSession() {
    if (!m_authStorage.save("session", {{"client_id", m_clientId}, {"access_token", m_access}, {"refresh_token", m_refresh},
        {"expires_at", m_expires}, {"user_session", m_userSession}})) { storageFailure(); return false; }
    return true;
}
void SoundCloud::token(const QUrlQuery &form, bool basic, std::function<void()> done, std::function<void()> failed, int loginEpoch) {
    QNetworkRequest request{QUrl(m_useWorker ? "https://scrip-auth.scrip-desktop-aca3d9.workers.dev/oauth/token"
                                            : "https://secure.soundcloud.com/oauth/token")};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("Accept", "application/json"); request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    if (basic && !m_useWorker) request.setRawHeader("Authorization", "Basic " + (m_clientId + ":" + m_clientSecret).toUtf8().toBase64());
    auto *reply = m_network->post(request, form.query(QUrl::FullyEncoded).toUtf8().replace("+", "%2B"));
    const int epoch = m_epoch;
    connect(reply, &QNetworkReply::finished, this, [this, reply, done, failed, epoch, loginEpoch] {
        reply->deleteLater(); if (epoch != m_epoch || (loginEpoch >= 0 && loginEpoch != m_loginEpoch)) return;
        const auto obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError || obj.value("access_token").toString().isEmpty()) {
            if (m_state.value("buffering").toBool()) set("playbackActive", false);
            m_refreshing = false; m_waiting.clear(); set("busy", false); set("authBusy", false); set("buffering", false);
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (code == 400 || code == 401) { m_access.clear(); m_refresh.clear(); if (!m_authStorage.remove("session", m_sessionPath)) storageFailure(); set("connected", false); set("signedIn", false); }
            if (failed) failed();
            if (!m_storageReady) return;
            fail(code == 429 ? "Лимит входов SoundCloud. Подождите перед повторной попыткой." : QString("SoundCloud не выдал токен (HTTP %1). Проверьте ключи, адрес возврата и доступ к сети.").arg(code));
            return;
        }
        m_access = obj.value("access_token").toString(); m_refresh = obj.value("refresh_token").toString();
        m_expires = QDateTime::currentSecsSinceEpoch() + obj.value("expires_in").toInt(3600);
        if (!saveSession()) {
            m_refreshing = false; m_waiting.clear(); set("busy", false); set("authBusy", false); set("buffering", false);
            if (failed) failed();
            return;
        }
        set("connected", true); set("signedIn", m_userSession); set("status", m_userSession ? "Аккаунт подключён" : "Публичный каталог подключён");
        done();
    });
}
void SoundCloud::ensureToken(std::function<void()> continuation) {
    if (!m_storageReady) { set("busy", false); set("buffering", false); fail(AuthStorage::errorMessage()); return; }
    if (!m_access.isEmpty() && QDateTime::currentSecsSinceEpoch() < m_expires - 60) { continuation(); return; }
    if (!m_state.value("configured").toBool()) { if (m_state.value("buffering").toBool()) { set("playbackActive", false); set("buffering", false); } set("busy", false); fail("Сначала настройте подключение к SoundCloud."); return; }
    if (m_useWorker && m_refresh.isEmpty()) { if (m_state.value("buffering").toBool()) { set("playbackActive", false); set("buffering", false); } set("busy", false); fail("Войдите в SoundCloud."); return; }
    m_waiting.append(continuation);
    if (m_refreshing) return;
    m_refreshing = true;
    QUrlQuery form;
    const bool basic = m_refresh.isEmpty();
    if (basic) { m_userSession = false; form.addQueryItem("grant_type", "client_credentials"); }
    else {
        form.addQueryItem("grant_type", "refresh_token"); form.addQueryItem("refresh_token", m_refresh);
        if (!m_useWorker) { form.addQueryItem("client_id", m_clientId); form.addQueryItem("client_secret", m_clientSecret); }
    }
    token(form, basic, [this] {
        m_refreshing = false;
        auto waiting = std::exchange(m_waiting, {});
        for (const auto &task : waiting) task();
    });
}
void SoundCloud::request(const QString &path, Callback done, const QByteArray &method, const QJsonObject &body, bool retry, std::function<void(QString)> failed) {
    QUrl url(path.startsWith('/') ? "https://api.soundcloud.com" + path : path);
    if (url.scheme() != "https" || url.host() != "api.soundcloud.com" || (url.port() != -1 && url.port() != 443) || !url.userInfo().isEmpty()) { if (failed) failed("API вернул недопустимый адрес."); else fail("API вернул недопустимый адрес."); return; }
    ensureToken([this, url, path, done, method, body, retry, failed] {
        QNetworkRequest req(url); req.setRawHeader("Authorization", "OAuth " + m_access.toUtf8());
        req.setRawHeader("Accept", "application/json"); req.setTransferTimeout(25000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        auto *reply = m_network->sendCustomRequest(req, method, body.isEmpty() ? QByteArray{} : QJsonDocument(body).toJson(QJsonDocument::Compact));
        const int epoch = m_epoch;
        connect(reply, &QNetworkReply::finished, this, [=, this] {
            reply->deleteLater(); if (epoch != m_epoch) return;
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401 && retry && !m_refresh.isEmpty()) { m_expires = 0; request(path, done, method, body, false, failed); return; }
            if (reply->error() != QNetworkReply::NoError || status >= 300) {
                const QString message = status == 429 ? "SoundCloud ограничил запросы. Повторите позже." : status == 403 ? "SoundCloud ограничил доступ к этому действию или треку." : status == 401 ? "Сессия истекла. Подключитесь заново." : QString("Не удалось выполнить запрос (HTTP %1). Проверьте сеть и повторите.").arg(status);
                if (failed) failed(message); else fail(message);
                return;
            }
            const QByteArray bytes = reply->readAll(); QJsonParseError error;
            auto doc = QJsonDocument::fromJson(bytes, &error);
            if (!bytes.isEmpty() && error.error != QJsonParseError::NoError) { if (failed) failed("SoundCloud вернул некорректный ответ."); else fail("SoundCloud вернул некорректный ответ."); return; }
            done(doc);
        });
    });
}
void SoundCloud::connectCatalog() {
    if (m_refreshing || m_state.value("authBusy").toBool()) return;
    clearError(); set("busy", true);
    ensureToken([this] { set("connected", true); set("signedIn", m_userSession); set("busy", false); set("status", m_userSession ? "Аккаунт подключён" : "Публичный каталог подключён"); if (m_userSession) { profile(); browse("likes"); } else { tryRestoreAndroidTrack(); browse("tracks", ""); } });
}
void SoundCloud::profile() { request("/me", [this](QJsonDocument d) { set("profile", d.object().toVariantMap()); }); }
void SoundCloud::login() {
    if (m_state.value("authBusy").toBool() || m_refreshing) return;
    if (!m_storageReady) { fail(AuthStorage::errorMessage()); return; }
    if (!loadConfig()) { fail("Подключение ещё не настроено. См. раздел «Первое подключение» в README."); return; }
    const QUrl redirect(m_redirect);
    if (redirect.scheme() != "http" || redirect.host() != "127.0.0.1" || redirect.path() != "/callback" || (redirect.port() < 1024 || redirect.port() > 65535) || redirect.hasQuery() || redirect.hasFragment() || !redirect.userInfo().isEmpty()) { fail("Настройте локальный адрес возврата."); return; }
    if (!m_callback.listen(QHostAddress::LocalHost, quint16(redirect.port()))) { fail("Порт для входа занят. Закройте второй экземпляр SCrip или измените порт."); return; }
    ++m_loginEpoch; clearError(); m_verifier = nonce(); m_oauthState = nonce();
    QUrl url("https://secure.soundcloud.com/authorize"); QUrlQuery query;
    query.addQueryItem("client_id", m_clientId); query.addQueryItem("redirect_uri", m_redirect);
    query.addQueryItem("response_type", "code"); query.addQueryItem("code_challenge_method", "S256"); query.addQueryItem("state", m_oauthState);
    query.addQueryItem("code_challenge", QString::fromLatin1(QCryptographicHash::hash(m_verifier.toLatin1(), QCryptographicHash::Sha256).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    url.setQuery(query); set("authBusy", true); m_loginTimeout.start();
    set("authStep", "browser"); emit authorizationRequested(url);
}
void SoundCloud::browserFailed() { cancelLogin(); fail("Не удалось открыть окно входа. Проверьте установленный браузер."); }
void SoundCloud::cancelLogin() { ++m_loginEpoch; m_callback.close(); m_loginTimeout.stop(); m_oauthState.clear(); m_verifier.clear(); set("authBusy", false); set("authStep", ""); }
void SoundCloud::callbackConnection() {
    while (m_callback.hasPendingConnections()) {
        auto *socket = m_callback.nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(10000, socket, [socket] { if (!socket->property("handled").toBool()) socket->disconnectFromHost(); });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            QByteArray data = socket->property("request").toByteArray() + socket->readAll();
            if (data.size() > 8192) { socket->disconnectFromHost(); return; }
            socket->setProperty("request", data);
            if (!data.contains("\r\n\r\n")) return;
            if (socket->property("handled").toBool()) return; socket->setProperty("handled", true);
            auto parts = data.left(data.indexOf("\r\n")).split(' ');
            QUrl url = parts.size() > 1 ? QUrl::fromEncoded(parts[1]) : QUrl{}; QUrlQuery query(url);
            const bool valid = parts.value(0) == "GET" && url.path() == "/callback" && !m_oauthState.isEmpty() && query.queryItemValue("state") == m_oauthState;
            const bool receivedCode = !query.hasQueryItem("error") && !query.queryItemValue("code").isEmpty();
            const auto respond = [peer = QPointer<QTcpSocket>(socket), valid](bool success) {
                if (!peer || peer->state() != QAbstractSocket::ConnectedState) return;
                const auto html = callbackPage(valid, success);
                peer->write(QByteArray(valid && success ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 400 Bad Request\r\n") +
                    "Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
                    QByteArray::number(html.size()) + "\r\n\r\n" + html);
                peer->disconnectFromHost();
            };
            if (!valid) { respond(false); return; }
            const auto verifier = m_verifier; const int loginEpoch = m_loginEpoch;
            m_callback.close(); m_loginTimeout.stop(); m_oauthState.clear(); m_verifier.clear();
            if (!receivedCode) { respond(false); fail("Вход отменён в SoundCloud."); return; }
            // Отменяем ответы старой гостевой сессии перед обменом кода.
            ++m_epoch; m_waiting.clear(); m_refreshing = false; set("authBusy", true); set("authStep", "exchange");
            QUrlQuery form; form.addQueryItem("grant_type", "authorization_code");
            if (!m_useWorker) {
                form.addQueryItem("client_id", m_clientId);
                form.addQueryItem("client_secret", m_clientSecret); form.addQueryItem("redirect_uri", m_redirect);
            }
            form.addQueryItem("code", query.queryItemValue("code")); form.addQueryItem("code_verifier", verifier);
            token(form, false, [this, respond] {
                m_userSession = true;
                if (!saveSession()) {
                    m_userSession = false;
                    set("authBusy", false);
                    set("authStep", "");
                    respond(false);
                    return;
                }
                set("signedIn", true);
                set("authBusy", false);
                set("authStep", "");
                set("status", "Аккаунт подключён");
                respond(true);
                profile();
                browse("likes");
            }, [this, respond] { set("authStep", ""); respond(false); }, loginEpoch);
        });
    }
}
bool SoundCloud::clearSession() {
    const bool removed = m_authStorage.remove("session", m_sessionPath);
    ++m_homeGeneration; set("home", QVariantList{}); set("homeBusy", false); set("homeError", "");
    set("savingTrack", false); set("loadingDestinations", false); set("destinations", QVariantList{}); set("destinationNext", "");
    stopPlayback();
    ++m_epoch; ++m_listing;
    m_access.clear(); m_refresh.clear(); m_expires = 0; m_userSession = false; m_refreshing = false; m_waiting.clear();
    m_queue.clear(); m_queueIndex = -1; m_nextHref.clear();
    set("queue", m_queue); set("queueIndex", -1); set("buffering", false); set("current", QVariantMap{}); set("tracks", QVariantList{}); set("profile", QVariantMap{});
    set("connected", false); set("signedIn", false); set("busy", false); set("hasMore", false); set("status", "Аккаунт отключён");
    if (!removed) storageFailure();
    return removed;
}
void SoundCloud::logout() {
    cancelLogin();
    const bool revoke = m_storageReady && m_userSession && !m_access.isEmpty();
    const auto access = m_access;
    if (!clearSession()) return;
    clearError();
    if (revoke) {
        QNetworkRequest req(QUrl("https://api.soundcloud.com/disconnect"));
        req.setRawHeader("Authorization", "OAuth " + access.toUtf8());
        req.setRawHeader("Accept", "application/json");
        req.setTransferTimeout(25000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        auto *reply = m_network->sendCustomRequest(req, "POST");
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
}
QString SoundCloud::urn(const QVariantMap &item) { return item.value("urn").toString(); }
void SoundCloud::restoreAndroidTrack(const QVariantMap &track, qint64 position) {
    if (!urn(track).startsWith("soundcloud:tracks:")) { set("restoreCleared", true); return; }
    if (!m_state.value("current").toMap().isEmpty() || m_playGeneration != 0) return;
    m_androidRestoreTrack = track;
    m_restorePosition = qMax<qint64>(0, position);
    m_androidRestorePending = true;
    tryRestoreAndroidTrack();
}
void SoundCloud::tryRestoreAndroidTrack() {
    if (!m_androidRestorePending) return;
    if (m_state.value("connected").toBool() && !m_state.value("signedIn").toBool()) {
        m_androidRestorePending = false; m_androidRestoreTrack.clear(); set("restoreCleared", true); return;
    }
    if (!m_state.value("signedIn").toBool()) return;
    if (!m_state.value("current").toMap().isEmpty() || m_playGeneration != 0) {
        m_androidRestorePending = false; m_androidRestoreTrack.clear(); set("restoreCleared", true); return;
    }
    const auto track = m_androidRestoreTrack;
    const auto position = m_restorePosition;
    m_androidRestorePending = false; m_androidRestoreTrack.clear();
    m_queue = {track}; m_queueIndex = 0;
    set("queue", m_queue); set("queueIndex", m_queueIndex);
    startTrack(track, false, position);
}
void SoundCloud::failAndroidRestore() {
    if (!m_restoringPlayback) return;
    m_restoringPlayback = false; set("restoreCleared", true); set("current", QVariantMap{});
    m_queue.clear(); m_queueIndex = -1; set("queue", m_queue); set("queueIndex", -1);
}
void SoundCloud::browse(const QString &page, const QString &query) {
    clearError(); QString path;
    const bool personal = page == "likes" || page == "mine" || page == "playlists" || page == "history" || page == "feed" || page == "reposts";
    if (personal && !m_state.value("signedIn").toBool()) { ++m_listing; set("tracks", QVariantList{}); set("hasMore", false); set("heading", "Войдите в SoundCloud"); set("busy", false); return; }
    if (page == "likes") { path = "/me/likes/tracks"; set("heading", "Любимые треки"); }
    else if (page == "reposts") { path = "/me/reposts/tracks"; set("heading", "Репосты"); }
    else if (page == "mine") { path = "/me/tracks"; set("heading", "Мои треки"); }
    else if (page == "playlists") { path = "/me/playlists"; set("heading", "Мои плейлисты"); }
    else if (page == "userTracks" || page == "userSets") {
        if (!query.startsWith("soundcloud:users:")) return;
        const auto userPath = apiPath("users", query);
        if (urn(m_state.value("visitedProfile").toMap()) != query)
            request(userPath, [this, query](QJsonDocument doc) {
                if (urn(doc.object().toVariantMap()) == query) set("visitedProfile", doc.object().toVariantMap());
            });
        path = userPath + (page == "userSets" ? "/playlists" : "/tracks");
    }
    else if (page == "history") { path = "/me/recently-played/tracks"; set("heading", "Недавно слушали"); }
    else if (page == "feed") { path = "/me/feed/tracks"; set("heading", "Лента подписок"); }
    else {
        if (query.trimmed().isEmpty()) { ++m_listing; set("busy", false); set("tracks", QVariantList{}); set("hasMore", false); set("heading", "Введите название или исполнителя"); return; }
        path = page == "users" ? "/users" : page == "sets" ? "/playlists" : "/tracks";
        QUrlQuery q; q.addQueryItem("q", query.trimmed()); path += "?" + q.query(QUrl::FullyEncoded); set("heading", "Результаты: " + query.trimmed());
    }
    if (page != "history") path += (path.contains('?') ? "&" : "?") + QString("limit=30&linked_partitioning=true");
    m_lastPath = path; collection(path);
}
void SoundCloud::collection(const QString &path, bool append) {
    int generation = ++m_listing; set("busy", true); set("hasMore", false);
    if (!append) { m_nextHref.clear(); set("tracks", QVariantList{}); }
    request(path, [this, generation, append](QJsonDocument doc) {
        if (generation != m_listing) return;
        auto list = append ? m_state.value("tracks").toList() : QVariantList{};
        QSet<QString> seen;
        for (const auto &old : list) seen.insert(urn(old.toMap()));
        for (auto item : items(doc)) {
            auto map = item.toMap();
            if (m_lastPath.startsWith("/me/likes/tracks")) map["user_favorite"] = true;
            const auto id = urn(map);
            if (seen.contains(id)) continue;
            seen.insert(id);
            list.append(map);
        }
        set("tracks", list); const auto next = doc.object().value("next_href").toString(); m_nextHref = next == m_nextHref ? QString{} : next; set("hasMore", !m_nextHref.isEmpty()); set("busy", false);
    }, "GET", {}, true, [this, generation, append](QString error) {
        if (generation != m_listing) return;
        set("hasMore", append && !m_nextHref.isEmpty()); set("busy", false); fail(error);
    });
}
void SoundCloud::loadHome(bool force) {
    if (!m_state.value("signedIn").toBool() || m_state.value("homeBusy").toBool()) return;
    if (!force && !m_state.value("home").toList().isEmpty()) return;
    const int generation = ++m_homeGeneration;
    set("homeBusy", true); set("homeError", ""); set("home", QVariantList{});
    // Независимые источники: недоступность одного не скрывает остальные.
    auto remaining = std::make_shared<int>(3);
    auto finish = [this, generation, remaining] {
        if (generation == m_homeGeneration && --*remaining == 0) set("homeBusy", false);
    };
    auto failed = [this, generation, finish](QString error) {
        if (generation != m_homeGeneration) return;
        set("homeError", error); finish();
    };
    auto add = [this, generation](QString title, QString subtitle, QVariantList tracks, bool mix, int order) {
        if (generation != m_homeGeneration) return;
        QVariantList clean;
        QSet<QString> seen;
        for (const auto &track : tracks) {
            auto id = urn(track.toMap());
            if (!id.startsWith("soundcloud:tracks:") || seen.contains(id)) continue;
            seen.insert(id); clean.append(track);
        }
        if (clean.isEmpty()) return;
        auto shelves = m_state.value("home").toList();
        shelves.append(QVariantMap{{"title", title}, {"subtitle", subtitle}, {"tracks", clean}, {"mix", mix}, {"order", order}});
        std::stable_sort(shelves.begin(), shelves.end(), [](const QVariant &a, const QVariant &b) { return a.toMap().value("order").toInt() < b.toMap().value("order").toInt(); });
        set("home", shelves);
    };
    request("/me/followings/tracks?limit=24", [add, finish](QJsonDocument doc) {
        auto tracks = items(doc);
        std::stable_sort(tracks.begin(), tracks.end(), [](const QVariant &a, const QVariant &b) { return a.toMap().value("created_at").toString() > b.toMap().value("created_at").toString(); });
        add("Новые релизы", "Свежие загрузки исполнителей из ваших подписок", tracks, false, 1); finish();
    }, "GET", {}, true, failed);
    request("/me/recently-played/tracks", [add, finish](QJsonDocument doc) {
        add("Слушать снова", "Ваши недавние прослушивания", items(doc), false, 3); finish();
    }, "GET", {}, true, failed);
    request("/me/likes/tracks?limit=24&linked_partitioning=true", [this, generation, add, finish, failed](QJsonDocument doc) {
        if (generation != m_homeGeneration) return;
        auto tracks = items(doc);
        for (auto &entry : tracks) { auto track = entry.toMap(); track["user_favorite"] = true; entry = track; }
        add("Микс любимых", "Подборка SCrip из последних добавленных любимых треков", tracks, true, 0);
        if (tracks.isEmpty()) { finish(); return; }
        auto seed = tracks.first().toMap();
        request(apiPath("tracks", urn(seed)) + "/related?limit=24&linked_partitioning=true", [add, finish, seed](QJsonDocument related) {
            add("Вам может понравиться", "Похожие на «" + seed.value("title").toString() + "» · SoundCloud", items(related), false, 2);
            add("Микс открытий", "Подборка SCrip из похожих треков", items(related), true, 0); finish();
        }, "GET", {}, true, failed);
    }, "GET", {}, true, failed);
    QTimer::singleShot(60000, this, [this, generation] {
        if (generation == m_homeGeneration && m_state.value("homeBusy").toBool()) {
            ++m_homeGeneration; set("homeBusy", false); set("homeError", "Загрузка заняла слишком много времени. Обновите главную.");
        }
    });
}
void SoundCloud::playMix(const QVariantList &tracks, int index) {
    if (tracks.isEmpty()) return;
    const int selectedIndex = qBound(0, index, int(tracks.size()) - 1);
    QVariantList queue;
    int selectedQueueIndex = -1;
    int previousQueueIndex = -1;
    int nextQueueIndex = -1;
    for (int i = 0; i < tracks.size(); ++i) {
        const auto &track = tracks[i];
        if (!urn(track.toMap()).startsWith("soundcloud:tracks:")) continue;
        const int queueIndex = queue.size();
        queue.append(track);
        if (i < selectedIndex) previousQueueIndex = queueIndex;
        else if (i == selectedIndex) selectedQueueIndex = queueIndex;
        else if (nextQueueIndex < 0) nextQueueIndex = queueIndex;
    }
    if (queue.isEmpty()) return;
    const int playIndex = selectedQueueIndex >= 0 ? selectedQueueIndex
        : nextQueueIndex >= 0 ? nextQueueIndex : previousQueueIndex;
    m_queue = queue;
    set("queue", m_queue); playQueue(playIndex);
}
void SoundCloud::playStation(const QVariantMap &seed) {
    if (!urn(seed).startsWith("soundcloud:tracks:")) return;
    const int generation = ++m_playGeneration;
    request(apiPath("tracks", urn(seed)) + "/related?limit=40", [this, generation, seed](QJsonDocument doc) {
        if (generation != m_playGeneration) return;
        QVariantList queue{seed};
        for (const auto &entry : items(doc))
            if (urn(entry.toMap()) != urn(seed)) queue.append(entry);
        playMix(queue);
    });
}

void SoundCloud::listDestinations(const QString &next) {
    if (!signedIn() || m_state.value("loadingDestinations").toBool()) return;
    if (!next.isEmpty() && next != m_state.value("destinationNext").toString()) return;
    if (next.isEmpty()) { set("destinations", QVariantList{}); set("destinationNext", ""); }
    set("loadingDestinations", true);
    request(next.isEmpty() ? QString("/me/playlists?limit=50&linked_partitioning=true") : next,
        [this](QJsonDocument doc) {
            auto list = m_state.value("destinations").toList();
            list.append(items(doc));
            set("destinations", list);
            set("destinationNext", doc.object().value("next_href").toString());
            set("loadingDestinations", false);
        }, "GET", {}, true, [this](QString error) { set("loadingDestinations", false); fail(error); });
}

void SoundCloud::saveToPlaylist(const QString &track, const QString &playlist, const QString &newTitle) {
    if (!signedIn() || !track.startsWith("soundcloud:tracks:") || m_state.value("savingTrack").toBool()) return;
    if (playlist.isEmpty() ? newTitle.trimmed().isEmpty() : !playlist.startsWith("soundcloud:playlists:")) return;
    set("savingTrack", true);
    auto failed = [this](QString error) { set("savingTrack", false); fail(error); };
    auto done = [this](QJsonDocument) { set("savingTrack", false); emit trackSaved(); };
    const QJsonObject entry{{"urn", track}};
    if (playlist.isEmpty()) {
        QJsonObject album{{"title", newTitle.trimmed()}, {"sharing", "private"}, {"set_type", "album"}, {"tracks", QJsonArray{entry}}};
        request("/playlists", done, "POST", {{"playlist", album}}, true, failed);
        return;
    }
    const auto path = apiPath("playlists", playlist);
    request(path + "?representation=full", [=, this](QJsonDocument doc) {
        const auto album = doc.object();
        const auto existing = album.value("tracks").toArray();
        if (!album.value("tracks").isArray() || existing.size() != album.value("track_count").toInt(-1)) {
            failed("SoundCloud вернул неполный список треков. Альбом сохранён без изменений."); return;
        }
        QJsonArray updated;
        for (const auto &value : existing) {
            const auto id = value.toObject().value("urn").toString();
            if (!id.startsWith("soundcloud:tracks:")) { failed("Не удалось прочитать состав альбома."); return; }
            if (id == track) { done({}); return; }
            updated.append(QJsonObject{{"urn", id}});
        }
        updated.append(entry);
        request(path, done, "PUT", {{"playlist", QJsonObject{{"tracks", updated}}}}, true, failed);
    }, "GET", {}, true, failed);
}

void SoundCloud::more() { if (!m_state.value("busy").toBool() && !m_nextHref.isEmpty()) collection(m_nextHref, true); }
void SoundCloud::openPlaylist(const QVariantMap &item) {
    if (urn(item).isEmpty()) return;
    m_lastPath = apiPath("playlists", urn(item)) + "/tracks?linked_partitioning=true";
    set("heading", item.value("title")); collection(m_lastPath);
}
void SoundCloud::enqueue(const QVariantMap &item) {
    if (!urn(item).startsWith("soundcloud:tracks:")) return;
    m_queue.append(item); set("queue", m_queue);
    prepareNext();
}
void SoundCloud::play(const QVariantMap &item) {
    if (item.value("kind").toString() == "playlist" || urn(item).startsWith("soundcloud:playlists:")) { openPlaylist(item); return; }
    m_queue.clear();
    for (const auto &entry : m_state.value("tracks").toList()) if (urn(entry.toMap()).startsWith("soundcloud:tracks:")) m_queue.append(entry);
    m_queueIndex = -1;
    for (int i=0; i<m_queue.size(); ++i) if (urn(m_queue[i].toMap()) == urn(item)) m_queueIndex = i;
    if (m_queueIndex < 0) { m_queue.prepend(item); m_queueIndex = 0; }
    set("queue", m_queue); set("queueIndex", m_queueIndex); startTrack(item);
}
void SoundCloud::playQueue(int index) { if (index >= 0 && index < m_queue.size()) { m_queueIndex = index; set("queueIndex", index); startTrack(m_queue[index].toMap()); } }
void SoundCloud::moveQueue(int from, int to) {
    if (from < 0 || to < 0 || from >= m_queue.size() || to >= m_queue.size() || from == to) return;
    m_queue.move(from, to);
    if (m_queueIndex == from) m_queueIndex = to;
    else if (from < m_queueIndex && to >= m_queueIndex) --m_queueIndex;
    else if (from > m_queueIndex && to <= m_queueIndex) ++m_queueIndex;
    m_state["queueIndex"] = m_queueIndex;
    m_state["queue"] = m_queue;
    emit changed();
    emit catalogChanged();
    prepareNext();
}
void SoundCloud::queueNext(int index) {
    if (index < 0 || index >= m_queue.size() || index == m_queueIndex) return;
    moveQueue(index, index < m_queueIndex ? m_queueIndex : m_queueIndex + 1);
    m_forceNext = true;
    prepareNext();
}
void SoundCloud::removeQueue(int index) {
    if (index < 0 || index >= m_queue.size()) return;
    const bool removingCurrent = index == m_queueIndex;
    m_queue.removeAt(index);
    if (m_queue.isEmpty()) {
        m_queueIndex = -1;
        set("queue", m_queue); set("queueIndex", m_queueIndex);
        stopPlayback();
        prepareNext();
        return;
    }
    if (index < m_queueIndex) --m_queueIndex;
    else if (removingCurrent) m_queueIndex = qMin(index, int(m_queue.size()) - 1);
    set("queue", m_queue); set("queueIndex", m_queueIndex);
    if (removingCurrent) startTrack(m_queue[m_queueIndex].toMap());
    prepareNext();
}
void SoundCloud::next() {
    if (m_queue.isEmpty()) { m_queueIndex = -1; set("queueIndex", -1); stopPlayback(); return; }
    int index = m_queueIndex + 1;
    if (!m_forceNext && m_state.value("shuffle").toBool() && m_queue.size() > 1) { do { index = QRandomGenerator::global()->bounded(int(m_queue.size())); } while (index == m_queueIndex); }
    if (m_preloadIndex >= 0 && m_preloadIndex < m_queue.size() && urn(m_queue[m_preloadIndex].toMap()) == m_preloadUrn) index = m_preloadIndex;
    if (index < m_queue.size()) playQueue(index);
    else if (player().mediaStatus() == QMediaPlayer::EndOfMedia) set("playbackActive", false);
}
void SoundCloud::previous() { if (player().position() > 3000) player().setPosition(0); else if (m_queueIndex > 0) playQueue(m_queueIndex - 1); }
void SoundCloud::togglePlayback() {
    if (m_state.value("playbackActive").toBool()) pausePlayback(); else resumePlayback();
}
void SoundCloud::pausePlayback() {
    set("playbackActive", false);
    if (m_crossfadeStarted) { m_crossfade.pause(); m_preloadPlayer->pause(); }
    player().pause();
}
void SoundCloud::resumePlayback() {
    if (player().source().isEmpty()) {
        if (m_state.value("buffering").toBool()) set("playbackActive", true);
        else if (m_queueIndex >= 0 && m_queueIndex < m_queue.size()) startTrack(m_queue[m_queueIndex].toMap());
        return;
    }
    set("playbackActive", true);
    player().play();
    if (m_crossfadeStarted) { m_preloadPlayer->play(); m_crossfade.resume(); }
}
void SoundCloud::stopPlayback() {
    m_androidRestorePending = false; m_androidRestoreTrack.clear(); m_restoringPlayback = false;
    ++m_playGeneration; ++m_preloadGeneration;
    set("playbackActive", false);
    m_crossfade.stop(); m_crossfadeStarted = false;
    player().stop(); player().setSource(QUrl{});
    m_preloadPlayer->stop(); m_preloadPlayer->setSource(QUrl{});
    player().setAudioOutput(nullptr); m_preloadPlayer->setAudioOutput(nullptr);
    player().setAudioOutput(&m_audio); m_preloadPlayer->setAudioOutput(&m_preloadAudio);
    m_audio.setVolume(m_state.value("volume").toReal()); m_preloadAudio.setVolume(0);
    m_preloadUrn.clear(); m_preloadIndex = -1;
    set("buffering", false); set("playing", false); set("current", QVariantMap{});
    set("position", 0); set("duration", 0);
    set("restoreCleared", true);
}
void SoundCloud::seek(qint64 position) { if (player().isSeekable()) player().setPosition(qBound(qint64(0), position, player().duration())); }
void SoundCloud::volume(double value) {
    const double target = qBound(0.0, value, 1.0);
    if (target == m_state.value("volume").toDouble()) return;
    if (m_crossfadeStarted) {
        const qreal progress = m_crossfade.currentValue().toReal();
        m_audio.setVolume(target * (1.0 - progress));
        m_preloadAudio.setVolume(target * progress);
        set("volume", target); QSettings().setValue("player/volume", target); return;
    }
    m_volumeFade.stop();
    m_volumeFade.setStartValue(double(m_audio.volume()));
    m_volumeFade.setEndValue(target);
    m_volumeFade.start();
    set("volume", target);
    QSettings().setValue("player/volume", target);
}
void SoundCloud::setRepeat(bool enabled) { set("repeat", enabled); }
void SoundCloud::setShuffle(bool enabled) { set("shuffle", enabled); prepareNext(); }
void SoundCloud::setCrossfade(bool enabled) {
    set("crossfade", enabled);
    QSettings().setValue("player/crossfade", enabled);
}
void SoundCloud::startTrack(const QVariantMap &track, bool autoplay, qint64 position) {
    m_forceNext = false;
    if (!urn(track).startsWith("soundcloud:tracks:")) return;
    m_androidRestorePending = false; m_androidRestoreTrack.clear();
    m_restoringPlayback = !autoplay; m_restorePosition = qMax<qint64>(0, position);
    set("restoreCleared", false); set("playbackActive", autoplay);
    clearError(); ++m_playGeneration;
    if (m_crossfadeStarted) {
        m_crossfade.stop();
        player().setAudioOutput(nullptr); player().setAudioOutput(&m_audio);
        m_preloadPlayer->setAudioOutput(nullptr); m_preloadPlayer->setAudioOutput(&m_preloadAudio);
        m_audio.setVolume(m_state.value("volume").toReal()); m_preloadAudio.setVolume(0); m_crossfadeStarted = false;
    }
    const bool prepared = urn(track) == m_preloadUrn && !m_preloadPlayer->source().isEmpty() && m_preloadPlayer->error() == QMediaPlayer::NoError;
    ++m_preloadGeneration;
    player().stop(); player().setSource(QUrl{});
    if (prepared) {
        player().setAudioOutput(nullptr);
        m_preloadPlayer->setAudioOutput(nullptr);
        std::swap(m_activePlayer, m_preloadPlayer);
        player().setAudioOutput(&m_audio);
        m_preloadPlayer->setAudioOutput(&m_preloadAudio);
    }
    m_preloadUrn.clear(); m_preloadIndex = -1;
    set("current", track); set("position", 0); set("duration", track.value("duration")); set("buffering", true);
    if (track.value("access").toString() == "blocked" || track.value("streamable") == false) { set("playbackActive", false); set("buffering", false); failAndroidRestore(); fail("Этот трек недоступен для воспроизведения через API."); return; }
    if (prepared) {
        set("buffering", player().mediaStatus() != QMediaPlayer::BufferedMedia);
        if (player().position() > 0) player().setPosition(0);
        if (autoplay) player().play(); prepareNext(); return;
    }
    const int generation = m_playGeneration; set("buffering", true);
    request(apiPath("tracks", urn(track)) + "/streams", [this, generation](QJsonDocument doc) {
        if (generation != m_playGeneration) return;
        auto object = doc.object(); QString url = object.value("hls_aac_160_url").toString();
        if (url.isEmpty()) url = object.value("hls_mp3_128_url").toString();
        if (url.isEmpty()) url = object.value("preview_mp3_128_url").toString();
        if (url.isEmpty()) { set("playbackActive", false); set("buffering", false); failAndroidRestore(); fail("SoundCloud не предоставил доступный аудиопоток."); return; }
        resolveStream(QUrl(url), generation);
    }, "GET", {}, true, [this, generation](QString message) {
        if (generation != m_playGeneration) return;
        set("playbackActive", false); set("buffering", false); failAndroidRestore(); fail(message);
    });
}
void SoundCloud::resolveStream(const QUrl &url, int generation, int redirects) {
    if (generation != m_playGeneration) return;
    if (url.scheme() != "https" || redirects > 5 || !url.userInfo().isEmpty()) { set("playbackActive", false); set("buffering", false); failAndroidRestore(); fail("Недопустимый адрес аудиопотока."); return; }
    // CDN SoundCloud выдаёт подписанные URL. OAuth передаём только API, никогда стороннему хосту.
    if (url.host() != "api.soundcloud.com") { player().setSource(url); if (m_state.value("playbackActive").toBool()) player().play(); prepareNext(); return; }
    QNetworkRequest req(url); req.setRawHeader("Authorization", "OAuth " + m_access.toUtf8()); req.setTransferTimeout(20000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_network->get(req);
    connect(reply, &QNetworkReply::finished, this, [=, this] {
        reply->deleteLater(); if (generation != m_playGeneration) return;
        auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!redirect.isEmpty()) { resolveStream(url.resolved(redirect), generation, redirects + 1); return; }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto nextUrl = doc.object().value("url").toString();
        if (!nextUrl.isEmpty()) { resolveStream(QUrl(nextUrl), generation, redirects + 1); return; }
        set("playbackActive", false); set("buffering", false); failAndroidRestore(); fail("Не удалось получить адрес потока SoundCloud. Попробуйте другой трек.");
    });
}
void SoundCloud::like(const QVariantMap &item) {
    if (!m_state.value("signedIn").toBool()) { fail("Войдите в аккаунт, чтобы менять избранное."); return; }
    const auto id = urn(item); if (!id.startsWith("soundcloud:tracks:")) return; const bool liked = item.value("user_favorite").toBool();
    request(apiPath("likes/tracks", id), [this, id, liked](QJsonDocument) {
        auto update = [id, liked](QVariantList list) { for (auto &v : list) { auto m = v.toMap(); if (urn(m) == id) { m["user_favorite"] = !liked; v = m; } } return list; };
        set("tracks", update(m_state.value("tracks").toList())); m_queue = update(m_queue); set("queue", m_queue);
        auto shelves = m_state.value("home").toList();
        for (auto &entry : shelves) {
            auto shelf = entry.toMap();
            shelf["tracks"] = update(shelf.value("tracks").toList());
            entry = shelf;
        }
        set("home", shelves);
        auto current = m_state.value("current").toMap(); if (urn(current) == id) { current["user_favorite"] = !liked; set("current", current); }
    }, liked ? "DELETE" : "POST");
}
void SoundCloud::createPlaylist(const QString &title, bool includeCurrent) {
    if (!m_state.value("signedIn").toBool()) { fail("Сначала войдите в SoundCloud."); return; }
    if (title.trimmed().isEmpty()) { fail("Укажите название плейлиста."); return; }
    QJsonObject playlist{{"title", title.trimmed()}, {"sharing", "private"}};
    if (includeCurrent && !m_state.value("current").toMap().isEmpty()) playlist["tracks"] = QJsonArray{QJsonObject{{"urn", urn(m_state.value("current").toMap())}}};
    request("/playlists", [this](QJsonDocument) { browse("playlists"); }, "POST", {{"playlist", playlist}});
}
void SoundCloud::openExternal(const QString &value) {
    QUrl url(value);
    const bool soundcloud = url.host() == "soundcloud.com" || url.host().endsWith(".soundcloud.com");
    const bool updates = url.host() == "github.com" && url.path() == "/sykia/SCrip/releases/latest";
    if (url.scheme() == "https" && (soundcloud || updates)) QDesktopServices::openUrl(url);
}
void SoundCloud::copyText(const QString &text) { QGuiApplication::clipboard()->setText(text); }

void SoundCloud::checkForUpdates(bool manual) {
    if (m_state.value("updateChecking").toBool() || m_state.value("updateDownloading").toBool() || m_state.value("updateInstalling").toBool()) return;
    set("updateError", ""); set("updateManual", manual); set("updateChecking", true);
    set("updateAvailable", false); set("updateResult", "Проверяем обновления…");
    QNetworkRequest req(QUrl("https://api.github.com/repos/sykia/SCrip/releases/latest"));
    req.setRawHeader("Accept", "application/vnd.github+json"); req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setRawHeader("User-Agent", "SCrip-update-checker"); req.setTransferTimeout(15000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_network->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual] {
        reply->deleteLater(); set("updateChecking", false);
        auto updateFailure = [this, manual](const QString &message) {
            set("updateAvailable", false); set("updateReady", false);
            m_updateAsset = {}; m_updateDigest.clear(); m_updateVersion.clear(); m_updateExpectedSize = -1;
            m_updateReadyVersion.clear(); m_updateReadyName.clear(); m_updateReadyDigest.clear();
            if (manual) set("updateError", message); set("updateResult", "Ошибка проверки");
        };
        const QUrl responseUrl = reply->url();
        if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 || responseUrl.scheme() != "https" || responseUrl.host() != "api.github.com" || responseUrl.port() != -1 || !responseUrl.userInfo().isEmpty()) { updateFailure("Не удалось проверить обновления. Проверьте подключение к GitHub."); return; }
        if (reply->bytesAvailable() > 1024 * 1024) { updateFailure("Ответ сервиса обновлений слишком велик."); return; }
        QJsonParseError parseError; const auto doc = QJsonDocument::fromJson(reply->read(1024 * 1024), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) { updateFailure("Сервис обновлений вернул некорректный ответ."); return; }
        const auto release = doc.object(); const QString tag = release.value("tag_name").toString();
        const QString remote = tag.startsWith('v') ? tag.mid(1) : tag;
        const auto latest = QVersionNumber::fromString(remote), current = QVersionNumber::fromString(QCoreApplication::applicationVersion());
        if (latest.isNull() || current.isNull()) { updateFailure("Не удалось определить версии приложения."); return; }
        const bool newerThanCurrent = QVersionNumber::compare(latest, current) > 0;
        bool sameReadyRelease = m_state.value("updateReady").toBool() && m_updateReadyVersion == remote && newerThanCurrent;
        if (!sameReadyRelease) {
            m_updatePath.clear(); m_updateReadyVersion.clear(); m_updateReadyName.clear(); m_updateReadyDigest.clear();
            set("updateReady", false);
            m_updateAsset = {}; m_updateDigest.clear(); m_updateExpectedSize = -1;
        } else {
            m_updateVersion = remote; m_updateDigest = m_updateReadyDigest;
        }
        set("latestVersion", remote); set("updateAvailable", newerThanCurrent);
        set("updateUrl", "https://github.com/sykia/SCrip/releases/latest");
        if (QVersionNumber::compare(latest, current) <= 0) { set("updateResult", "Установлена последняя версия"); return; }
        QString suffix;
#ifdef Q_OS_ANDROID
        suffix = "-Android-universal.apk";
#elif defined(Q_OS_WIN)
        suffix = "-Windows-x64.exe";
#elif defined(Q_OS_LINUX)
        if (QFileInfo::exists("/etc/arch-release")) suffix = "-x86_64.pkg.tar.zst";
        else if (QFileInfo::exists("/etc/debian_version")) suffix = "-amd64.deb";
        else { updateFailure("Автообновление поддерживает Arch Linux и Debian."); return; }
#else
        updateFailure("Обновление для этой платформы не поддерживается."); return;
#endif
        for (const auto &entry : release.value("assets").toArray()) {
            const auto asset = entry.toObject(); const QString name = asset.value("name").toString();
            const QUrl url(asset.value("browser_download_url").toString());
            const QString digest = asset.value("digest").toString();
            const auto sizeValue = asset.value("size");
            const double size = sizeValue.toDouble(-1);
            const QString lowerDigest = digest.toLower();
            if (name.isEmpty() || QFileInfo(name).fileName() != name || name == "." || name == ".." || !name.endsWith(suffix) || sizeValue.isNull() || size <= 0 || size > 1024.0*1024*1024 || qFloor(size) != size ||
                !lowerDigest.startsWith("sha256:") || lowerDigest.size() != 71 ||
                !QRegularExpression("^sha256:[0-9a-f]{64}$").match(lowerDigest).hasMatch() ||
                url.scheme() != "https" || url.host() != "github.com" || url.port() != -1 || !url.userInfo().isEmpty() ||
                !url.path().startsWith("/sykia/SCrip/releases/download/")) continue;
            if (sameReadyRelease && (m_updateReadyName != name || m_updateReadyDigest != lowerDigest || m_updateExpectedSize != qint64(size))) {
                m_updatePath.clear(); m_updateReadyVersion.clear(); m_updateReadyName.clear(); m_updateReadyDigest.clear();
                set("updateReady", false); sameReadyRelease = false;
            }
            m_updateAsset = asset; m_updateDigest = lowerDigest; m_updateExpectedSize = qint64(size); m_updateVersion = remote;
            set("updateResult", "Доступна версия " + remote); set("updateError", ""); return;
        }
        updateFailure("Обновление доступно, но подходящий установочный файл с проверяемыми метаданными не найден.");
    });
}

void SoundCloud::downloadUpdate() {
    if (!m_state.value("updateAvailable").toBool() || m_updateReply || m_state.value("updateDownloading").toBool() || m_state.value("updateInstalling").toBool() || m_updateAsset.isEmpty()) return;
    const QUrl url(m_updateAsset.value("browser_download_url").toString());
    if (url.scheme() != "https" || url.host() != "github.com" || url.port() != -1 || !url.userInfo().isEmpty() || !url.path().startsWith("/sykia/SCrip/releases/download/") || m_updateVersion != m_state.value("latestVersion").toString()) { set("updateError", "Источник или версия файла обновления не разрешены."); return; }
    const QString dir = QStandardPaths::writableLocation(
#ifdef Q_OS_ANDROID
        QStandardPaths::AppDataLocation
#else
        QStandardPaths::CacheLocation
#endif
    ) + "/updates";
    if (!QDir().mkpath(dir)) { set("updateError", "Не удалось создать каталог для обновления."); return; }
    m_updatePath = dir + "/" + QFileInfo(m_updateAsset.value("name").toString()).fileName();
    m_updateFile = new QSaveFile(m_updatePath, this);
    if (!m_updateFile->open(QIODevice::WriteOnly)) { delete m_updateFile; m_updateFile = nullptr; set("updateError", "Не удалось подготовить файл обновления."); return; }
    m_updateHash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_updateReceived = 0; set("updateError", ""); set("updateDownloading", true); set("updateResult", "Скачивание обновления…");
    QNetworkRequest req(url); req.setTransferTimeout(60000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_updateReply = m_network->get(req);
    connect(m_updateReply, &QNetworkReply::metaDataChanged, this, [this] {
        if (!m_updateReply) return;
        const QUrl finalUrl = m_updateReply->url();
        if (finalUrl.scheme() != "https" || finalUrl.port() != -1 || !finalUrl.userInfo().isEmpty() ||
            !(finalUrl.host() == "github.com" || finalUrl.host().endsWith(".githubusercontent.com"))) {
            set("updateError", "Сервер перенаправил загрузку на запрещённый адрес."); cancelUpdateDownload(); return;
        }
        const auto length = m_updateReply->header(QNetworkRequest::ContentLengthHeader);
        if (length.isValid()) {
            bool valid = false; const qint64 n = length.toString().toLongLong(&valid);
            if (!valid || n <= 0 || n > 1024LL*1024*1024 || (m_updateExpectedSize >= 0 && n != m_updateExpectedSize)) {
                set("updateError", "Сервер указал некорректный размер обновления."); cancelUpdateDownload(); return;
            }
        }
    });
    connect(m_updateReply, &QNetworkReply::readyRead, this, [this] {
        if (!m_updateReply || !m_updateFile) return;
        while (m_updateReply && m_updateFile && m_updateReply->bytesAvailable() > 0) {
            const QByteArray chunk = m_updateReply->read(256 * 1024);
            if (chunk.isEmpty()) break;
            if (m_updateReceived > 1024LL*1024*1024 - chunk.size() || (m_updateExpectedSize >= 0 && chunk.size() > m_updateExpectedSize - m_updateReceived)) { set("updateError", "Файл превышает допустимый размер."); cancelUpdateDownload(); return; }
            qint64 written = 0;
            while (written < chunk.size()) { const qint64 n = m_updateFile->write(chunk.constData() + written, chunk.size() - written); if (n <= 0) break; written += n; }
            if (written != chunk.size()) { set("updateError", "Не удалось записать файл обновления."); cancelUpdateDownload(); return; }
            m_updateHash->addData(chunk); m_updateReceived += chunk.size();
        }
    });
    disconnect(&m_updateProgressTimer, nullptr, this, nullptr);
    connect(&m_updateProgressTimer, &QTimer::timeout, this, [this] {
        set("updateDownloaded", m_updateReceived); set("updateTotal", m_updateExpectedSize);
    });
    m_updateProgressTimer.start(150);
    connect(m_updateReply, &QNetworkReply::finished, this, [this] {
        auto *reply = qobject_cast<QNetworkReply *>(sender());
        if (!reply || m_updateReply != reply) return;
        m_updateProgressTimer.stop();
        if (reply && m_updateFile) {
            while (reply->bytesAvailable() > 0) {
                const QByteArray tail = reply->read(256 * 1024);
                if (tail.isEmpty() || m_updateReceived > 1024LL*1024*1024-tail.size() || (m_updateExpectedSize >= 0 && tail.size() > m_updateExpectedSize-m_updateReceived)) { set("updateError", "Не удалось записать все данные обновления."); break; }
                qint64 written = 0;
                while (written < tail.size()) { const qint64 n = m_updateFile->write(tail.constData() + written, tail.size() - written); if (n <= 0) break; written += n; }
                if (written != tail.size()) { set("updateError", "Не удалось записать все данные обновления."); break; }
                m_updateHash->addData(tail); m_updateReceived += tail.size();
            }
        }
        const int status = reply ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
        const bool ok = reply && reply->error() == QNetworkReply::NoError && status == 200 && m_updateExpectedSize > 0 && m_updateReceived == m_updateExpectedSize &&
            ("sha256:" + QString::fromLatin1(m_updateHash->result().toHex()) == m_updateDigest);
        m_updateHash.reset();
        m_updateReply = nullptr;
        set("updateDownloaded", m_updateReceived); set("updateTotal", m_updateExpectedSize);
        if (!ok) {
            if (m_updateFile) { m_updateFile->cancelWriting(); delete m_updateFile; m_updateFile = nullptr; }
            set("updateDownloading", false); set("updateAvailable", true);
            if (m_state.value("updateError").toString().isEmpty()) set("updateError", reply && reply->error() != QNetworkReply::NoError ? "Ошибка сети при скачивании обновления." : (m_updateReceived != m_updateExpectedSize ? "Обновление скачалось не полностью." : "Контрольная сумма SHA-256 не совпадает."));
            set("updateResult", "Ошибка или отмена скачивания"); if (reply) reply->deleteLater(); return;
        }
        const bool committed = m_updateFile && m_updateFile->commit(); delete m_updateFile; m_updateFile = nullptr;
        set("updateDownloading", false);
        if (!committed) { set("updateError", "Не удалось сохранить проверенное обновление."); set("updateResult", "Ошибка сохранения файла"); }
        else { m_updateReadyVersion = m_updateVersion; m_updateReadyName = m_updateAsset.value("name").toString(); m_updateReadyDigest = m_updateDigest; set("updateReady", true); set("updateResult", "Обновление скачано и проверено"); }
        if (reply) reply->deleteLater();
    });
}

void SoundCloud::cancelUpdateDownload() {
    if (!m_updateReply) return;
    m_updateProgressTimer.stop(); auto *reply = m_updateReply; m_updateReply = nullptr;
    disconnect(reply, nullptr, this, nullptr); reply->abort(); reply->deleteLater();
    if (m_updateFile) { m_updateFile->cancelWriting(); delete m_updateFile; m_updateFile = nullptr; }
    m_updateHash.reset();
    set("updateDownloading", false); set("updateReady", false); set("updateResult", "Скачивание отменено"); if (m_state.value("updateError").toString().isEmpty()) set("updateError", "");
}

void SoundCloud::installUpdate() {
    if (!m_state.value("updateReady").toBool() || m_state.value("updateInstalling").toBool() || m_updateReply || m_state.value("updateChecking").toBool() || m_updateReadyVersion != m_state.value("latestVersion").toString() || m_updateReadyVersion != m_updateVersion || m_updateReadyDigest != m_updateDigest || m_updateReadyName != m_updateAsset.value("name").toString()) return;
    QFile file(m_updatePath);
    if (!file.open(QIODevice::ReadOnly) || (m_updateExpectedSize >= 0 && file.size() != m_updateExpectedSize) || file.size() <= 0 || file.size() > 1024LL*1024*1024) { set("updateReady", false); set("updateError", "Проверенный файл обновления недоступен или повреждён."); set("updateResult", "Ошибка проверки файла"); return; }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) { const auto chunk = file.read(256 * 1024); if (chunk.isEmpty() && file.error() != QFile::NoError) break; hash.addData(chunk); }
    if (file.error() != QFile::NoError || !file.atEnd()) { set("updateReady", false); set("updateError", "Не удалось полностью прочитать проверенный файл обновления."); set("updateResult", "Ошибка проверки файла"); return; }
    if ("sha256:" + QString::fromLatin1(hash.result().toHex()) != m_updateDigest) { set("updateReady", false); set("updateError", "Контрольная сумма файла изменилась. Установка заблокирована."); set("updateResult", "Ошибка проверки файла"); return; }
    set("updateInstalling", true); set("updateResult", "Передача системному установщику…");
#ifdef Q_OS_ANDROID
    const jint result = QJniObject::callStaticMethod<jint>("org/scrip/app/ApkInstaller", "install", "(Landroid/content/Context;Ljava/lang/String;)I", QNativeInterface::QAndroidApplication::context().object(), QJniObject::fromString(m_updatePath).object());
    set("updateInstalling", false);
    if (result == 1) { set("updateResult", "Разрешите установку SCrip в настройках Android, затем нажмите «Установить сейчас» ещё раз."); set("updateError", "Нужно разрешить установку приложений из этого источника."); }
    else if (result == 2) { set("updateResult", "Открыт системный установщик Android. Подтвердите или отмените установку там."); set("updateError", ""); }
    else { set("updateResult", "Не удалось открыть системный установщик Android."); set("updateError", "Android не открыл системный установщик APK."); }
#elif defined(Q_OS_WIN)
    qint64 installerPid = 0;
    if (!QProcess::startDetached(m_updatePath, {}, QFileInfo(m_updatePath).absolutePath(), &installerPid)) {
        set("updateInstalling", false); set("updateError", "Не удалось открыть мастер установки Windows."); set("updateResult", "Ошибка запуска установщика");
    } else {
        set("updateError", "");
        set("updateResult", "Мастер установки Windows открыт. Завершите его; результат будет проверен при следующем запуске.");
        HANDLE installer = OpenProcess(SYNCHRONIZE, FALSE, DWORD(installerPid));
        if (installer) {
            auto *watcher = new QTimer(this); watcher->setInterval(500);
            connect(watcher, &QTimer::timeout, this, [this, watcher, installer] {
                if (WaitForSingleObject(installer, 0) != WAIT_OBJECT_0) return;
                watcher->stop(); watcher->deleteLater(); CloseHandle(installer);
                set("updateInstalling", false);
                set("updateResult", "Мастер установки закрыт. Результат обновления проверится при следующем запуске.");
            });
            watcher->start();
        } else {
            set("updateResult", "Мастер установки открыт. Завершите его; перезапустите SCrip после установки или отмены.");
        }
    }
#elif defined(Q_OS_LINUX)
    const bool arch = m_updateReadyName.endsWith(".pkg.tar.zst");
    const QFileInfo packageInfo(m_updatePath);
    if (!packageInfo.isAbsolute() || packageInfo.fileName() != m_updateReadyName ||
        (!arch && !m_updateReadyName.endsWith(".deb"))) {
        set("updateInstalling", false); set("updateReady", false);
        set("updateError", "Формат или путь пакета обновления недопустим."); set("updateResult", "Ошибка проверки пакета"); return;
    }
    auto *process = new QProcess(this);
    if (!arch) { auto env = QProcessEnvironment::systemEnvironment(); env.insert("DEBIAN_FRONTEND", "noninteractive"); process->setProcessEnvironment(env); }
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) { if (!process->property("updateTerminal").toBool()) { process->setProperty("updateTerminal", true); set("updateInstalling", false); set("updateError", "Не удалось запустить системный установщик."); set("updateResult", "Ошибка передачи установщику"); process->deleteLater(); } });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process](int code, QProcess::ExitStatus status) { if (!process->property("updateTerminal").toBool()) { process->setProperty("updateTerminal", true); set("updateInstalling", false); if (code == 0 && status == QProcess::NormalExit) { set("updateError", ""); set("updateResult", "Установка завершена"); } else if (code == 126 && status == QProcess::NormalExit) { set("updateError", ""); set("updateResult", "Установка отменена пользователем."); } else { set("updateError", "Системный установщик не завершил установку."); set("updateResult", "Ошибка установки"); } process->deleteLater(); } });
    process->start("pkexec", arch ? QStringList{"pacman", "--noconfirm", "-U", "--", m_updatePath} : QStringList{"apt-get", "install", "-y", "--", m_updatePath});
#endif
}

void SoundCloud::holdPreloadedAudio(QMediaPlayer *media) {
    if (media != m_preloadPlayer || m_crossfadeStarted || media->mediaStatus() != QMediaPlayer::BufferedMedia ||
        media->playbackState() != QMediaPlayer::PlayingState) return;
    media->pause();
    if (media->position() > 0) media->setPosition(0);
}

// Готовим только один следующий поток. Ошибки фоновой загрузки не прерывают текущий трек.
void SoundCloud::prepareNext() {
    const int generation = ++m_preloadGeneration;
    m_preloadPlayer->stop(); m_preloadPlayer->setSource(QUrl{});
    m_preloadUrn.clear(); m_preloadIndex = -1;
    if (m_queueIndex < 0 || m_queue.isEmpty() || player().source().isEmpty()) return;
    int index = m_queueIndex + 1;
    if (!m_forceNext && m_state.value("shuffle").toBool() && m_queue.size() > 1) {
        do { index = QRandomGenerator::global()->bounded(int(m_queue.size())); } while (index == m_queueIndex);
    }
    if (index >= m_queue.size()) return;
    const auto track = m_queue[index].toMap();
    if (track.value("access").toString() == "blocked" || track.value("streamable") == false) return;
    m_preloadIndex = index; m_preloadUrn = urn(track);
    request(apiPath("tracks", m_preloadUrn) + "/streams", [this, generation](QJsonDocument doc) {
        if (generation != m_preloadGeneration) return;
        const auto object = doc.object();
        QString url = object.value("hls_aac_160_url").toString();
        if (url.isEmpty()) url = object.value("hls_mp3_128_url").toString();
        if (url.isEmpty()) url = object.value("preview_mp3_128_url").toString();
        if (!url.isEmpty()) prepareStream(QUrl(url), generation);
    }, "GET", {}, true, [](QString) {});
}
void SoundCloud::beginCrossfade() {
    if (m_crossfadeStarted || m_queue.isEmpty() || m_preloadIndex < 0 || m_preloadIndex >= m_queue.size() ||
        m_preloadPlayer->source().isEmpty() || m_preloadPlayer->error() != QMediaPlayer::NoError ||
        m_preloadPlayer->mediaStatus() != QMediaPlayer::BufferedMedia ||
        urn(m_queue[m_preloadIndex].toMap()) != m_preloadUrn) return;
    const QVariantMap nextTrack = m_queue[m_preloadIndex].toMap();
    m_queueIndex = m_preloadIndex;
    set("queueIndex", m_queueIndex); set("queue", m_queue);
    set("current", nextTrack); set("position", 0); set("duration", nextTrack.value("duration")); set("buffering", false);
    m_forceNext = false;
    QMediaPlayer *incoming = m_preloadPlayer;
    std::swap(m_activePlayer, m_preloadPlayer);
    m_preloadUrn.clear(); m_preloadIndex = -1;
    m_crossfadeStarted = true;
    incoming->play();
    m_crossfade.stop(); m_crossfade.setStartValue(0.0); m_crossfade.setEndValue(1.0); m_crossfade.start();
}
void SoundCloud::prepareStream(const QUrl &url, int generation, int redirects) {
    if (generation != m_preloadGeneration || url.scheme() != "https" || redirects > 5 || !url.userInfo().isEmpty()) return;
    if (url.host() != "api.soundcloud.com") {
        m_preloadPlayer->setSource(url);
        // Запускаем без звука до заполнения буфера, затем приостанавливаем у начала.
        m_preloadPlayer->play();
        return;
    }
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", "OAuth " + m_access.toUtf8());
    req.setTransferTimeout(20000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_network->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, generation, redirects] {
        reply->deleteLater();
        if (generation != m_preloadGeneration || reply->error() != QNetworkReply::NoError) return;
        auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!redirect.isEmpty()) { prepareStream(url.resolved(redirect), generation, redirects + 1); return; }
        const auto next = QJsonDocument::fromJson(reply->readAll()).object().value("url").toString();
        if (!next.isEmpty()) prepareStream(QUrl(next), generation, redirects + 1);
    });
}
