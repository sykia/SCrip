#include "soundcloud.h"
#include <QSysInfo>
#include "recommendations.h"
#include <QRegularExpression>
#include <utility>
#include <algorithm>
#include <cmath>
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
#include <limits>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSettings>
#include <QNetworkInformation>
#include <QGuiApplication>
#include <QClipboard>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QVersionNumber>
#include <QProcess>
#include <QDebug>
#include <QScopeGuard>
#include <cstdio>
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
// Frequent checkpoints stay separate from multi-megabyte queue snapshots.
QString progressPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("playback-progress.json");
}
QJsonObject readProgress() {
    QFile file(progressPath());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
void writeProgress(const QVariantMap &position, const QString &mode) {
    auto data = readProgress();
    const auto record = QJsonObject::fromVariantMap(position);
    data["current"] = record;
    if (mode == "library" || mode == "discovery") data[mode] = record;
    const auto path = progressPath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;
    const auto bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (file.write(bytes) == bytes.size()) file.commit();
}
qint64 wavePosition(const QVariantMap &snapshot, const QString &mode) {
    const auto checkpoint = readProgress().value(mode).toObject().toVariantMap();
    const auto queue = snapshot.value("queue").toList();
    const int index = snapshot.value("index", -1).toInt();
    if (index >= 0 && index < queue.size()
        && checkpoint.value("account") == snapshot.value("account")
        && checkpoint.value("track") == queue[index].toMap().value("urn"))
        return qMax<qint64>(0, checkpoint.value("position").toLongLong());
    return qMax<qint64>(0, snapshot.value("position").toLongLong());
}
QByteArray callbackPage(bool valid, bool receivedCode, const QString &retryToken) {
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
    html.replace("RETRY_ACTION", retryToken.isEmpty() ? QByteArray{} :
        ("<p class=retry><a href='/retry?token=" + retryToken.toUtf8() + "'>Повторить вход</a></p>"));
    if (!valid)
        return html.replace("PAGE_TITLE", "Начните вход в Sange").replace("PAGE_MESSAGE", "Откройте приложение и нажмите «Войти в SoundCloud».");
    if (!receivedCode)
        return html.replace("PAGE_TITLE", "Вход не завершён").replace("PAGE_MESSAGE", "Нажмите «Повторить вход», чтобы начать заново в этой вкладке.");
    return html.replace("PAGE_TITLE", "Вернитесь в Sange").replace("PAGE_MESSAGE", "Аккаунт SoundCloud подключён. Эту вкладку можно закрыть.");
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
        if (item.contains("playlist")) item = item.value("playlist").toObject();
        if (!item.isEmpty()) result.append(item.toVariantMap());
    }
    return result;
}
QString apiPath(const QString &kind, const QString &urn) {
    return "/" + kind + "/" + QString::fromLatin1(QUrl::toPercentEncoding(urn));
}
}

void TrackModel::update(const QVariantList &items) {
    if (items.isEmpty() && !m_items.isEmpty()) {
        beginResetModel(); m_items.clear(); endResetModel(); return;
    }
    // Rows are presentation slots: changing collections must not destroy every
    // visible delegate (including its artwork and input handlers). Selection is
    // tracked separately by the collection context in QML.
    const int common = qMin(m_items.size(), items.size());
    for (int i = 0; i < common; ++i) {
        if (m_items[i] != items[i]) { m_items[i] = items[i]; emit dataChanged(index(i), index(i)); }
    }
    if (items.size() < m_items.size()) {
        beginRemoveRows({}, items.size(), m_items.size() - 1);
        while (m_items.size() > items.size()) m_items.removeLast();
        endRemoveRows();
    } else if (items.size() > m_items.size()) {
        beginInsertRows({}, m_items.size(), items.size() - 1);
        for (int i = common; i < items.size(); ++i) m_items.append(items[i]);
        endInsertRows();
    }
}

bool TrackModel::moveItem(int from, int to) {
    if (from < 0 || to < 0 || from >= m_items.size() || to >= m_items.size() || from == to) return false;
    if (!beginMoveRows({}, from, from, {}, to > from ? to + 1 : to)) return false;
    m_items.move(from, to); endMoveRows(); return true;
}
bool TrackModel::removeItem(int index) {
    if (index < 0 || index >= m_items.size()) return false;
    beginRemoveRows({}, index, index); m_items.removeAt(index); endRemoveRows(); return true;
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
        {"heading", "Откройте свою музыку"}, {"configPath", m_configPath}, {"repeat", false}, {"repeatMode", 0}, {"shuffle", false}, {"crossfade", false}, {"waveMode", ""}, {"waveBusy", false}, {"waveError", ""},
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
    m_volumeSaveTimer.setSingleShot(true); m_volumeSaveTimer.setInterval(250);
    connect(&m_volumeSaveTimer, &QTimer::timeout, this, [this] { QSettings().setValue("player/volume", volumeLevel()); });
    m_saveTimer.setInterval(1000);
    connect(&m_saveTimer, &QTimer::timeout, this, &SoundCloud::savePlaybackPosition);
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
    set("playbackQuality", qBound(0, preferences.value("player/playbackQuality", 0).toInt(), 1));
    set("preloadEnabled", preferences.value("player/preloadEnabled", true).toBool());
    set("preloadWifiOnly", preferences.value("player/preloadWifiOnly", false).toBool());
    set("wifiPolicyAvailable", false);
#ifdef Q_OS_ANDROID
    QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::TransportMedium);
    if (auto *info = QNetworkInformation::instance(); info && info->supports(QNetworkInformation::Feature::TransportMedium)) {
        set("wifiPolicyAvailable", true);
        connect(info, &QNetworkInformation::transportMediumChanged, this, [this] { updatePreloadPolicy(); });
    }
#endif
    m_streamCache.setBackgroundEnabled(preloadAllowed());
    const int repeatMode = qBound(0, preferences.value("player/repeatMode",
        preferences.value("player/repeat", false).toBool() ? 1 : 0).toInt(), 2);
    set("repeatMode", repeatMode); set("repeat", repeatMode == 1);
    preferences.setValue("player/repeatMode", repeatMode);
    set("shuffle", preferences.value("player/shuffle", false).toBool());
    m_audio.setVolume(preferences.value("player/volume", 0.7).toDouble());
    set("volume", m_audio.volume());
    for (int i = 0; i < 5; ++i)
        m_equalizerGains[i] = qBound(-12.0, preferences.value(QString("player/equalizer/%1").arg(i), 0).toDouble(), 12.0);
    m_playerEffects.setGains(m_equalizerGains); m_spareEffects.setGains(m_equalizerGains);
    m_equalizerEnabled = equalizerAvailable() && preferences.value("player/equalizerEnabled", false).toBool();
    m_spectrum = m_spectrumTarget = QVariantList(AudioDsp::spectrumBands, 0.0);
    updateAudioRouting();
#ifdef Q_OS_ANDROID
    // На мобильных устройствах переходы между треками отключены, включая старую настройку.
    set("crossfade", false);
    // Диагностический A/B-переключатель; обычный режим сохраняет предзагрузку.
    m_androidPreload = !qEnvironmentVariableIsSet("SCRIP_ANDROID_PRELOAD") ||
        qEnvironmentVariableIntValue("SCRIP_ANDROID_PRELOAD") == 1;
#else
    set("crossfade", preferences.value("player/crossfade", false).toBool());
#endif
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
    connect(&m_crossfade, &QVariantAnimation::finished, this, [this] { finishCrossfade(); });
    m_waveRetryTimer.setSingleShot(true);
    connect(&m_waveRetryTimer, &QTimer::timeout, this, [this] {
        set("waveBusy", false);
        refillWave();
    });
    m_waveAudioTimer.setInterval(33);
    connect(&m_waveAudioTimer, &QTimer::timeout, this, [this] {
        auto envelope = [](double current, double target) {
            const double value = current + (target - current) * (target > current ? 0.65 : 0.18);
            return value < 0.001 ? 0.0 : value;
        };
        m_waveLevel = envelope(m_waveLevel, m_waveTargetLevel);
        m_waveBass = envelope(m_waveBass, m_waveTargetBass);
        m_waveBeat = qMax(m_waveTargetBeat, m_waveBeat * 0.80);
        if (m_waveBeat < 0.001) m_waveBeat = 0;
        m_waveTargetLevel *= 0.85; m_waveTargetBass *= 0.85; m_waveTargetBeat = 0;
        for (int i = 0; i < m_spectrum.size(); ++i) {
            const double current = m_spectrum[i].toDouble(), target = m_spectrumTarget[i].toDouble();
            // Gentle spectrum attack/release, independent from the beat animation.
            const double value = current + (target - current) * (target > current ? 0.24 : 0.085);
            m_spectrum[i] = value < 0.001 ? 0.0 : value;
            m_spectrumTarget[i] = target * 0.95;
        }
        emit spectrumChanged();
        emit waveAudioChanged();
    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    connect(&m_waveOutput, &QAudioBufferOutput::audioBufferReceived, this, [this](const QAudioBuffer &buffer) {
        processAudio(&m_player, buffer);
    });
    connect(&m_waveSpareOutput, &QAudioBufferOutput::audioBufferReceived, this, [this](const QAudioBuffer &buffer) {
        processAudio(&m_sparePlayer, buffer);
    });
#endif
    QList<QMediaPlayer *> players{&m_player, &m_sparePlayer};
    m_preloadAudio.setVolume(0);
    m_preloadPlayer->setAudioOutput(&m_preloadAudio);
    for (auto *media : players) {
        connect(media, &QMediaPlayer::sourceChanged, this, [this, media] {
            (media == &m_player ? m_playerEffects : m_spareEffects).clear();
        });
        connect(media, &QMediaPlayer::playbackStateChanged, this, [this, media](QMediaPlayer::PlaybackState state) {
            if (state != QMediaPlayer::PlayingState)
                (media == &m_player ? m_playerEffects : m_spareEffects).clear();
        });
        connect(media, &QMediaPlayer::positionChanged, this, [this, media](qint64 p) {
            if (media != m_activePlayer) return;
            // Restore/seek emits positionChanged too; count progress only after the
            // saved start position has actually advanced while Qt is playing.
            if (p > m_startupInitialPosition && m_startupAudible) {
                startupStage("first_progress"); m_startupAudible = false; m_startupPending = false;
            }
            if (!m_restoringPlayback && m_pendingSeek < 0) {
                set("position", p);
                if (!m_state.value("playbackActive").toBool()) savePlaybackPosition();
            }
            const qint64 duration = media->duration();
            if (m_state.value("crossfade").toBool() && !m_crossfadeStarted && duration > 6000 && duration - p <= 3000)
                beginCrossfade();
        });
        connect(media, &QMediaPlayer::seekableChanged, this, [this, media](bool) {
            if (media == m_activePlayer) { applyPendingSeek(); emit catalogChanged(); }
        });
        connect(media, &QMediaPlayer::durationChanged, this, [this, media](qint64 d) { if (media == m_activePlayer) set("duration", d); });
        connect(media, &QMediaPlayer::playbackStateChanged, this, [this, media] {
            if (media != m_activePlayer) { holdPreloadedAudio(media); return; }
            const bool playing = media->playbackState() == QMediaPlayer::PlayingState;
            set("playing", playing);
            if (playing) m_streamCache.warm(media->source());
            else m_streamCache.suspend();
            if (playing) {
                set("buffering", false);
                if (m_startupPending && !m_startupAudible) { startupStage("qt_playing"); m_startupAudible = true; }
            }
        });
        connect(media, &QMediaPlayer::errorOccurred, this, [this, media](QMediaPlayer::Error e, const QString &) {
            if (media == m_activePlayer && e != QMediaPlayer::NoError) {
                qWarning() << "Playback backend error; code=" << int(e);
                if (m_startupPending && m_startupUsedPreload && !m_preloadedRetryUsed) {
                    m_preloadedRetryUsed = true;
                    startupStage("preloaded_url_refresh");
                    startTrack(m_state.value("current").toMap(), m_state.value("playbackActive").toBool(),
                               media->position(), true);
                    return;
                }
                if (m_startupPending && m_streamCandidateIndex + 1 < m_streamCandidates.size()) {
                    startupStage("qt_error_fallback");
                    nextStream(m_playGeneration, "qt_error");
                    return;
                }
                recoverPlayback();
            }
        });
        connect(media, &QMediaPlayer::mediaStatusChanged, this, [this, media](QMediaPlayer::MediaStatus status) {
            if (media != m_activePlayer) {
                holdPreloadedAudio(media);
                return;
            }
            if (m_startupPending && !m_startupLoadedLogged && m_startupSourceSet && !media->source().isEmpty() &&
                (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)) {
                startupStage("qt_loaded"); m_startupLoadedLogged = true;
            }
            // BufferingMedia уже содержит достаточно данных для продолжения; StalledMedia — нет.
            set("buffering", status == QMediaPlayer::LoadingMedia || status == QMediaPlayer::StalledMedia ||
                (status == QMediaPlayer::BufferingMedia && media->playbackState() != QMediaPlayer::PlayingState));
            applyPendingSeek();
            if (m_restoringPlayback && m_pendingSeek < 0 && (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)) {
                if (m_restorePosition > 0) player().setPosition(player().duration() > 0 ? qMin(m_restorePosition, player().duration()) : m_restorePosition);
                m_restoringPlayback = false;
                set("position", player().position());
                if (m_state.value("playbackActive").toBool()) player().play();
            }
            if (status == QMediaPlayer::EndOfMedia && !m_playbackRecovering) {
                const qint64 expected = m_state.value("current").toMap().value("duration").toLongLong();
                if (m_state.value("playbackActive").toBool() && expected > 0 &&
                    media->position() + 5000 < expected) recoverPlayback();
                else advancePlayback(true);
            }
        });
    }
    m_playbackRecoveryTimer.setSingleShot(true);
    connect(&m_playbackRecoveryTimer, &QTimer::timeout, this, &SoundCloud::checkPlaybackConnection);
    m_playbackWatchdog.setInterval(1000);
    connect(&m_playbackWatchdog, &QTimer::timeout, this, [this] {
        if (!m_state.value("playbackActive").toBool() || m_playbackRecovering ||
            m_state.value("current").toMap().isEmpty()) { m_playbackIdleSeconds = 0; return; }
        const auto position = player().position();
        if (position != m_watchdogPosition && m_state.value("playing").toBool()) {
            m_watchdogPosition = position; m_playbackIdleSeconds = 0;
            if (position > m_startupInitialPosition + 15000) {
                m_playbackRetries = 0; m_failedPlaybackTracks.clear();
            }
        } else if (++m_playbackIdleSeconds >= 30) recoverPlayback();
    });
    m_playbackWatchdog.start();
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
    if (m_volumeSaveTimer.isActive()) {
        m_volumeSaveTimer.stop(); QSettings().setValue("player/volume", volumeLevel());
    }
    savePlayback();
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
void SoundCloud::set(const QString &key, const QVariant &input) {
    static const bool measure = qEnvironmentVariableIntValue("SCRIP_PERF") == 1;
    QElapsedTimer elapsed;
    if (measure) elapsed.start();
    const auto report = qScopeGuard([&] {
        if (measure && elapsed.elapsed() >= 10) {
            std::fprintf(stderr, "SCripPerf state=%s elapsed_ms=%lld\n", key.toUtf8().constData(),
                         static_cast<long long>(elapsed.elapsed()));
            std::fflush(stderr);
        }
    });
    QVariant value = input;
    if ((key == "likePending" || key == "engagementPending") && input != m_state.value(key))
        ++m_engagementRevision;
    if ((key == "signedIn" && !value.toBool()) ||
        (key == "profile" && urn(value.toMap()) != urn(m_state.value("profile").toMap()))) {
        ++m_likeGeneration; ++m_likeRevision; set("likePending", QVariantMap{});
        m_likedTracks.clear(); m_likeOverrides.clear(); m_likePages.clear(); m_firstLikes = {}; m_likesComplete = false;
    }
    if ((key == "signedIn" && !value.toBool()) ||
        (key == "profile" && urn(value.toMap()) != urn(m_state.value("profile").toMap()))) {
        if (key == "signedIn" || !urn(m_state.value("profile").toMap()).isEmpty()) {
            ++m_listing; ++m_profileBrowseGeneration; m_listings.clear(); m_nextHref.clear();
        }
        cancelCollectionSearch(); m_commentTrack.clear();
        m_undoQueue.clear(); set("queueUndoAvailable", false);
        resetSocialState();
        m_followPages.clear();
        set("followings", QVariantMap{}); set("followingPending", QVariantMap{});
        set("followingsReady", false); set("followingsLoading", false);
        ++m_commentsGeneration; m_commentsNext.clear(); m_commentsPages.clear(); m_comments.clear(); emit commentsChanged();
        m_likedTracks.clear(); m_likeOverrides.clear(); m_likePages.clear(); m_firstLikes = {}; m_likesComplete = false;
    }
    if ((key == "profile" && urn(value.toMap()) != m_recommendationAccount) ||
        (key == "signedIn" && !value.toBool())) {
        ++m_homeGeneration; ++m_waveGeneration; set("waveBusy", false); set("homeBusy", false);
        loadRecommendationPreferences(key == "profile" ? urn(value.toMap()) : QString{});
    }
    if (key == "current") value = withLikeState(value.toMap());
    else if (key == "tracks" || key == "queue" || key == "playlistTracks") value = withLikeStates(value.toList());
    else if (key == "home") {
        auto shelves = value.toList();
        for (auto &entry : shelves) {
            auto shelf = entry.toMap(); shelf["tracks"] = withLikeStates(shelf.value("tracks").toList()); entry = shelf;
        }
        value = shelves;
    }
    // Refresh selected/queued credits without replacing the uploader or restarting
    // playback. Legacy and partial objects without the field remain compatible.
    if (key == "tracks" || key == "playlistTracks") {
        QHash<QString, QVariant> credits;
        for (const auto &entry : value.toList()) {
            const auto track = entry.toMap();
            if (track.contains("metadata_artist") && !urn(track).isEmpty())
                credits.insert(urn(track), track.value("metadata_artist"));
        }
        auto updateCredit = [&credits](QVariantMap &track) {
            const auto credit = credits.constFind(urn(track));
            if (credit == credits.cend() || track.value("metadata_artist") == *credit) return false;
            track["metadata_artist"] = *credit; return true;
        };
        auto current = m_state.value("current").toMap();
        if (updateCredit(current)) set("current", current);
        auto queue = m_queue; bool queueChanged = false;
        for (auto &entry : queue) {
            auto track = entry.toMap();
            if (updateCredit(track)) { entry = track; queueChanged = true; }
        }
        if (queueChanged) { set("queue", queue); savePlayback(); }
    }
    if (key == "queue") m_queue = value.toList();
    if (m_state.value(key) == value) return;
    m_state[key] = value;
    if (key == "playbackActive") {
        if (value.toBool()) m_saveTimer.start();
        else m_saveTimer.stop();
    }
    if (key == "current" && !urn(m_commentTrack).isEmpty() && urn(value.toMap()) == urn(m_commentTrack)) setCommentTrack(value.toMap());
    if (key == "queue" && m_queueIds != m_undoAfterIds) set("queueUndoAvailable", false);
    if (key == "tracks") m_trackModel.update(value.toList());
    if (key == "tracks") emit collectionTracksChanged();
    else if (key == "playlistTracks") emit playlistRowsChanged();
    else if (key == "queue") emit queueTracksChanged();
    else if (key == "home") emit homeShelvesChanged();
    emit changed();
    if (key == "volume") emit volumeChanged();
    else if (key == "position" || key == "duration") emit progressChanged();
    else emit catalogChanged();
    if (key == "signedIn") emit signedInChanged();
    if (key == "connected" && value.toBool() && !m_pendingLink.isEmpty()) {
        const auto link = std::exchange(m_pendingLink, {});
        QTimer::singleShot(0, this, [this, link] { openLink(link); });
    }
}
void SoundCloud::fail(const QString &message) { qWarning().noquote() << "SoundCloud operation failed:" << message; set("error", message); }
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
    const auto clientId = id.trimmed();
    const auto enteredSecret = secret.trimmed();
    const auto newSecret = enteredSecret.isEmpty() ? m_clientSecret.trimmed() : enteredSecret;
    if (clientId.isEmpty() || newSecret.isEmpty()) { fail("Укажите Client ID и Client Secret."); return; }
    if (url.scheme() != "http" || url.host() != "127.0.0.1" || (url.port() < 1024 || url.port() > 65535) || url.path() != "/callback" || url.hasQuery() || url.hasFragment() || !url.userInfo().isEmpty()) {
        fail("Адрес возврата должен иметь вид http://127.0.0.1:43821/callback (порт от 1024)."); return;
    }
    const QJsonObject connection{{"client_id", clientId}, {"client_secret", newSecret}, {"redirect_uri", url.toString()}};
    if (!m_authStorage.save("connection", connection)) { storageFailure(); return; }
    m_connection = connection;
    cancelLogin(); if (!clearSession()) return; loadConfig(); clearError(); connectCatalog();
}
void SoundCloud::storageFailure() {
    m_storageReady = false;
    set("connected", false); set("signedIn", false);
    set("status", "Хранилище авторизации недоступно");
    fail(AuthStorage::errorMessage());
}
bool SoundCloud::saveSession(const QString &access, const QString &refresh, qint64 expires, bool userSession) {
    return m_authStorage.save("session", {{"client_id", m_clientId}, {"access_token", access}, {"refresh_token", refresh},
        {"expires_at", expires}, {"user_session", userSession}});
}
void SoundCloud::token(const QUrlQuery &form, bool basic, TokenFlow flow, std::function<void()> done, TokenFailure failed, int loginEpoch) {
    const auto wait = m_rateUntil.value("oauth") - m_requestNow();
    if (wait > 0) {
        deferRequest(wait, [=, this] {
            if (loginEpoch < 0 || loginEpoch == m_loginEpoch) token(form, basic, flow, done, failed, loginEpoch);
        });
        return;
    }
    QNetworkRequest request{QUrl(m_useWorker ? "https://scrip-auth.scrip-desktop-aca3d9.workers.dev/oauth/token"
                                            : "https://secure.soundcloud.com/oauth/token")};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("Accept", "application/json"); request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    if (basic && !m_useWorker) request.setRawHeader("Authorization", "Basic " + (m_clientId + ":" + m_clientSecret).toUtf8().toBase64());
    auto *reply = m_network->post(request, form.query(QUrl::FullyEncoded).toUtf8().replace("+", "%2B"));
    const int epoch = m_epoch;
    auto handled = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done, failed, epoch, loginEpoch, flow, handled] {
        reply->deleteLater(); if (epoch != m_epoch || (loginEpoch >= 0 && loginEpoch != m_loginEpoch)) return;
        if (std::exchange(*handled, true)) return;
        observeRate(reply, "oauth");
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        const auto obj = document.object();
        if (reply->error() != QNetworkReply::NoError || parseError.error != QJsonParseError::NoError
            || !document.isObject() || obj.value("access_token").toString().isEmpty()) {
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString message = code == 0 ? "Плохое соединение с интернетом. Проверьте сеть и повторите подключение." : code == 429 ? "Лимит входов SoundCloud. Подождите перед повторной попыткой."
                : QString("SoundCloud не выдал токен (HTTP %1). Проверьте ключи, адрес возврата и доступ к сети.").arg(code);
            m_refreshing = false;
            if (flow == TokenFlow::Refresh && (code == 400 || code == 401)) {
                m_access.clear(); m_refresh.clear();
                m_expires = 0; m_userSession = false;
                if (!m_authStorage.remove("session", m_sessionPath)) {
                    message = AuthStorage::errorMessage();
                    const bool quarantined = m_authStorage.save("session", {{"client_id", m_clientId},
                        {"access_token", ""}, {"refresh_token", ""}, {"expires_at", 0}, {"user_session", false}});
                    if (!quarantined) m_storageReady = false;
                }
                ++m_epoch; m_reads.clear();
                ++m_listing; ++m_homeGeneration;
                set("profile", QVariantMap{}); set("tracks", QVariantList{});
                set("home", QVariantList{}); set("homeBusy", false); set("homeError", "");
                set("destinations", QVariantList{}); set("destinationNext", "");
                set("loadingDestinations", false); set("savingTrack", false); set("busy", false);
                set("connected", false); set("signedIn", false); set("status", "Войдите в SoundCloud");
            }
            if (failed) failed(message); else fail(message);
            return;
        }
        const QString access = obj.value("access_token").toString();
        const QString refresh = obj.value("refresh_token").toString();
        const qint64 expires = QDateTime::currentSecsSinceEpoch() + obj.value("expires_in").toInt(3600);
        const bool userSession = flow == TokenFlow::AuthorizationCode ? true
            : flow == TokenFlow::ClientCredentials ? false : m_userSession;
        if (!saveSession(access, refresh, expires, userSession)) {
            m_refreshing = false;
            const auto message = AuthStorage::errorMessage();
            if (!m_authStorage.consistent()) {
                ++m_epoch; m_reads.clear(); ++m_listing; ++m_homeGeneration;
                m_access.clear(); m_refresh.clear(); m_expires = 0; m_userSession = false;
                m_storageReady = false;
                set("connected", false); set("signedIn", false); set("busy", false);
                set("homeBusy", false); set("loadingDestinations", false); set("savingTrack", false);
            }
            fail(message);
            if (failed) failed(message);
            return;
        }
        if (flow == TokenFlow::AuthorizationCode) {
            ++m_epoch; m_reads.clear();
            const auto track = m_comments.value("track").toString();
            ++m_commentsGeneration; m_commentsNext.clear(); m_commentsPages.clear(); m_comments.clear(); emit commentsChanged();
            if (!track.isEmpty()) QTimer::singleShot(0, this, [this, track] { loadComments(track); });
        }
        m_access = access; m_refresh = refresh; m_expires = expires; m_userSession = userSession;
        if (flow != TokenFlow::AuthorizationCode) {
            set("connected", true); set("signedIn", m_userSession);
            set("status", m_userSession ? "Аккаунт подключён" : "Публичный каталог подключён");
        }
        done();
    });
}
void SoundCloud::ensureToken(std::function<void()> continuation, TokenFailure failed) {
    const auto reject = [this, &failed](const QString &message) { if (failed) failed(message); else fail(message); };
    if (!m_storageReady) { reject(AuthStorage::errorMessage()); return; }
    if (!m_access.isEmpty() && QDateTime::currentSecsSinceEpoch() < m_expires - 60) { continuation(); return; }
    if (m_state.value("authBusy").toBool()) { reject("Вход в SoundCloud ещё выполняется."); return; }
    if (!m_state.value("configured").toBool()) { reject("Сначала настройте подключение к SoundCloud."); return; }
    if (m_useWorker && m_refresh.isEmpty()) { reject("Войдите в SoundCloud."); return; }
    m_waiting.append({continuation, std::move(failed)});
    if (m_refreshing) return;
    m_refreshing = true;
    QUrlQuery form;
    const bool basic = m_refresh.isEmpty();
    TokenFlow flow;
    if (basic) { flow = TokenFlow::ClientCredentials; form.addQueryItem("grant_type", "client_credentials"); }
    else {
        flow = TokenFlow::Refresh;
        form.addQueryItem("grant_type", "refresh_token"); form.addQueryItem("refresh_token", m_refresh);
        if (!m_useWorker) { form.addQueryItem("client_id", m_clientId); form.addQueryItem("client_secret", m_clientSecret); }
    }
    token(form, basic, flow, [this] {
        m_refreshing = false;
        const int epoch = m_epoch;
        auto waiting = std::exchange(m_waiting, {});
        for (auto &task : waiting) {
            if (epoch != m_epoch) break;
            if (task.continuation) task.continuation();
        }
    }, [this](QString message) {
        m_refreshing = false;
        const int epoch = m_epoch;
        auto waiting = std::exchange(m_waiting, {});
        for (auto &task : waiting) {
            if (epoch != m_epoch) break;
            if (task.failed) task.failed(message);
            else fail(message);
        }
    });
}
QString SoundCloud::rateGroup(const QUrl &url, const QByteArray &method) const {
    const auto path = url.path();
    if (path.contains("/stream")) return QStringLiteral("streams");
    if (path.startsWith("/likes/") || path.contains("/likes/") || path.endsWith("/favorites")) return QStringLiteral("likes");
    if (path.startsWith("/reposts/") && (method == "POST" || method == "DELETE")) return QStringLiteral("reposts");
    if (path.endsWith("/download")) return QStringLiteral("downloads");
    if (method == "GET" && (path == "/tracks" || path == "/users" || path == "/playlists" || path.startsWith("/search"))) return QStringLiteral("search");
    return QStringLiteral("api");
}
qint64 SoundCloud::rateDeadline(const QString &group) const {
    // A known general cooldown also applies to routes with a narrower quota.
    return qMax(m_rateUntil.value("api"), m_rateUntil.value(group));
}
void SoundCloud::deferRequest(qint64 delay, std::function<void()> operation) {
    const int epoch = m_epoch;
    const QPointer<SoundCloud> owner(this);
    auto guarded = [owner, epoch, operation] { if (owner && epoch == owner->m_epoch) operation(); };
    if (m_requestLater) m_requestLater(delay, guarded);
    else QTimer::singleShot(int(qMin(delay, qint64(std::numeric_limits<int>::max()))), this, guarded);
}
void SoundCloud::observeRate(QNetworkReply *reply, const QString &groupOverride) {
    const qint64 now = m_requestNow();
    auto header = [reply](const QByteArray &name) {
        for (const auto &pair : reply->rawHeaderPairs())
            if (pair.first.compare(name, Qt::CaseInsensitive) == 0) return pair.second.trimmed();
        return QByteArray{};
    };
    bool ok = false;
    const auto remaining = header("X-Ratelimit-Remaining").toLongLong(&ok);
    const bool exhausted = ok && remaining == 0;
    qint64 until = 0;
    if (exhausted || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 429) {
        const auto reset = header("X-Ratelimit-Reset").toLongLong(&ok);
        if (ok && reset > 0 && reset <= std::numeric_limits<qint64>::max() / 1000) until = reset * 1000;
    }
    const auto after = header("Retry-After");
    const auto seconds = after.toLongLong(&ok);
    if (ok && seconds >= 0 && seconds < (std::numeric_limits<qint64>::max() - now) / 1000)
        until = qMax(until, now + seconds * 1000);
    else if (!after.isEmpty()) {
        const auto date = QDateTime::fromString(QString::fromLatin1(after), Qt::RFC2822Date);
        if (date.isValid()) until = qMax(until, date.toMSecsSinceEpoch());
    }
    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 429 && until <= now)
        until = now + 1000; // Backoff when the server supplies no usable deadline, not a quota.
    const auto group = groupOverride.isEmpty() ? rateGroup(reply->url()) : groupOverride;
    m_rateUntil[group] = qMax(m_rateUntil.value(group), until);
}
void SoundCloud::request(const QString &path, Callback done, const QByteArray &method, const QJsonObject &body, bool retry, TokenFailure failed, std::function<void()> started, int redirects, bool background) {
    const QUrl url(path.startsWith('/') ? "https://api.soundcloud.com" + path : path);
    // Include session/account generations and all parameters; never share mutations.
    const QString key = QString::number(m_epoch) + ':' + QString::number(m_playlistOperationGeneration) + ':' + url.toString(QUrl::FullyEncoded)
        + ':' + QString::fromLatin1(QJsonDocument(body).toJson(QJsonDocument::Compact))
        + ':' + QString::number(m_engagementRevision)
        + ':' + QString::number(retry) + ':' + QString::number(background)
        + (background && rateGroup(url) == "streams" ? ':' + QString::number(m_preloadGeneration) : QString{});
    if (method != "GET") { requestAttempt(path, done, method, body, retry, failed, started, redirects, 0, background); return; }
    if (m_reads.contains(key)) { m_reads[key]->append({done, failed}); return; }
    auto waiters = std::make_shared<QList<ReadWaiter>>();
    waiters->append({done, failed});
    m_reads.insert(key, waiters);
    const int epoch = m_epoch;
    const auto revision = m_engagementRevision;
    const bool writing = !m_state.value("likePending").toMap().isEmpty()
        || !m_state.value("engagementPending").toMap().isEmpty();
    auto success = [this, key, epoch, waiters, revision, writing](QJsonDocument doc) {
        m_reads.remove(key);
        if (epoch == m_epoch && revision == m_engagementRevision && !writing)
            acceptEngagementCounts(doc);
        for (const auto &waiter : *waiters) { if (epoch != m_epoch) break; if (waiter.done) waiter.done(doc); }
    };
    auto failure = [this, key, waiters](QString message) {
        m_reads.remove(key);
        // Invalid refresh deliberately advances the session epoch before failing its waiters.
        const int failureEpoch = m_epoch;
        for (const auto &waiter : *waiters) {
            if (failureEpoch != m_epoch) break;
            if (waiter.failed) waiter.failed(message); else fail(message);
        }
    };
    requestAttempt(path, success, method, body, retry, failure, started, redirects, 0, background);
}
void SoundCloud::requestAttempt(const QString &path, Callback done, const QByteArray &method, const QJsonObject &body, bool retry, std::function<void(QString)> failed, std::function<void()> started, int redirects, int attempt, bool background) {
    QUrl url(path.startsWith('/') ? "https://api.soundcloud.com" + path : path);
    if (url.scheme() != "https" || url.host() != "api.soundcloud.com" || (url.port() != -1 && url.port() != 443) || !url.userInfo().isEmpty()) { if (failed) failed("API вернул недопустимый адрес."); else fail("API вернул недопустимый адрес."); return; }
    const auto wait = rateDeadline(rateGroup(url, method)) - m_requestNow();
    if (background && (wait > 0 || (m_startupPending && !m_startupSourceSet))) {
        if (failed) failed("Фоновая загрузка отложена. Повторите позже.");
        return;
    }
    if (wait > 0) {
        deferRequest(wait, [=, this] { requestAttempt(path, done, method, body, retry, failed, started, redirects, attempt, background); });
        return;
    }
    const int preloadGeneration = m_preloadGeneration;
    const bool preloadRequest = background && rateGroup(url, method) == "streams";
    const int requestEpoch = m_epoch;
    ensureToken([this, url, path, done, method, body, retry, failed, started, redirects, requestEpoch, attempt, background, preloadGeneration, preloadRequest] {
        if (requestEpoch != m_epoch) return;
        if (preloadRequest && (preloadGeneration != m_preloadGeneration || !preloadAllowed())) {
            if (failed) failed({});
            return;
        }
        if (rateDeadline(rateGroup(url, method)) > m_requestNow() || (background && m_startupPending && !m_startupSourceSet)) {
            requestAttempt(path, done, method, body, retry, failed, started, redirects, attempt, background); return;
        }
        if (started) started();
        QNetworkRequest req(url); req.setRawHeader("Authorization", "OAuth " + m_access.toUtf8());
        req.setPriority(rateGroup(url) == "streams" && !background ? QNetworkRequest::HighPriority : background ? QNetworkRequest::LowPriority : QNetworkRequest::NormalPriority);
        req.setRawHeader("Accept", "application/json"); req.setTransferTimeout(25000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        auto *reply = m_network->sendCustomRequest(req, method, body.isEmpty() ? QByteArray{} : QJsonDocument(body).toJson(QJsonDocument::Compact));
        if (preloadRequest) m_preloadReply = reply;
        const int epoch = m_epoch;
        auto handled = std::make_shared<bool>(false);
        connect(reply, &QNetworkReply::finished, this, [=, this] {
            reply->deleteLater(); if (epoch != m_epoch) return;
            if (std::exchange(*handled, true)) return;
            observeRate(reply, rateGroup(url, method));
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError || status >= 400)
                qWarning() << "SoundCloud request failed; HTTP=" << status << "network=" << int(reply->error()) << "attempt=" << attempt;
            if (status == 401 && retry && !m_refresh.isEmpty()) { m_expires = 0; requestAttempt(path, done, method, body, false, failed, started, redirects, attempt, background); return; }
            if (status == 302 && method == "GET" && (path.startsWith("/resolve?") || path.startsWith("https://api.soundcloud.com/resolve?")) && redirects == 0) {
                const auto location = reply->url().resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
                requestAttempt(location.toString(QUrl::FullyEncoded), done, method, {}, retry, failed, started, redirects + 1, attempt, background);
                return;
            }
            const bool transient = status == 429 || status == 502 || status == 503 || status == 504
                || (status == 0 && (reply->error() == QNetworkReply::TimeoutError
                    || reply->error() == QNetworkReply::TemporaryNetworkFailureError
                    || reply->error() == QNetworkReply::RemoteHostClosedError));
            if (method == "GET" && !background && transient && attempt < 2) {
                deferRequest(qMax(qint64(500 * (1 << attempt)), rateDeadline(rateGroup(url, method)) - m_requestNow()),
                    [=, this] { requestAttempt(path, done, method, body, retry, failed, started, redirects, attempt + 1, background); });
                return;
            }
            if (reply->error() != QNetworkReply::NoError || status >= 300) {
                const QString message = status == 429 ? "SoundCloud ограничил запросы. Повторите позже." : status == 403 ? "SoundCloud ограничил доступ к этому действию или треку." : status == 404 ? "SoundCloud не нашёл этот ресурс." : status == 401 ? "Сессия истекла. Подключитесь заново." : reply->error() == QNetworkReply::TimeoutError ? "Время ожидания SoundCloud истекло. Проверьте сеть и повторите." : QString("Не удалось выполнить запрос. Проверьте сеть и повторите.");
                if (failed) failed(message); else fail(message);
                return;
            }
            const QByteArray bytes = reply->readAll(); QJsonParseError error;
            auto doc = QJsonDocument::fromJson(bytes, &error);
            if (!bytes.isEmpty() && error.error != QJsonParseError::NoError) { if (failed) failed("SoundCloud вернул некорректный ответ."); else fail("SoundCloud вернул некорректный ответ."); return; }
            done(doc);
        });
    }, [this, failed](QString message) { if (failed) failed(message); else fail(message); });
}
void SoundCloud::updateComments(const QString &key, const QVariant &value) {
    if (m_comments.value(key) == value) return;
    m_comments[key] = value;
    emit commentsChanged();
}

void SoundCloud::setCommentTrack(const QVariantMap &track) {
    if (m_commentTrack == track) return;
    const bool hidden = track.value("reveal_comments").isValid() && !track.value("reveal_comments").isNull() && !track.value("reveal_comments").toBool();
    const bool changed = urn(track) != urn(m_commentTrack) || hidden != m_comments.value("hidden").toBool();
    m_commentTrack = track;
    if (changed) {
        ++m_commentsGeneration; m_commentsNext.clear(); m_commentsPages.clear();
        m_comments = {{"track", urn(track)}, {"items", QVariantList{}}, {"loaded", hidden}, {"hidden", hidden}};
    }
    m_comments["canPost"] = !(track.value("commentable").isValid() && !track.value("commentable").isNull() && !track.value("commentable").toBool());
    emit commentsChanged();
}

void SoundCloud::loadComments(const QString &track, bool append, bool force) {
    static const QRegularExpression validTrack("^soundcloud:tracks:[0-9]+$");
    if (!validTrack.match(track).hasMatch()) {
        m_commentTrack.clear(); ++m_commentsGeneration; m_commentsNext.clear(); m_commentsPages.clear(); m_comments.clear(); emit commentsChanged(); return;
    }
    if (track != urn(m_commentTrack)) {
        const auto current = m_state.value("current").toMap();
        if (urn(current) == track) setCommentTrack(current);
        else for (const auto &entry : m_state.value("tracks").toList()) {
            if (urn(entry.toMap()) == track) { setCommentTrack(entry.toMap()); break; }
        }
    }
    if (track == urn(m_commentTrack) && m_comments.value("hidden").toBool()) return;
    const bool same = m_comments.value("track").toString() == track;
    if (same && m_comments.value("sending").toBool()) return;
    if (append && (!same || m_commentsNext.isEmpty() || m_comments.value("busy").toBool())) return;
    if (!append && same && !force && (m_comments.value("loaded").toBool() || m_comments.value("busy").toBool())) return;
    const auto path = append ? m_commentsNext : apiPath("tracks", track) + "/comments?limit=200&linked_partitioning=true";
    if (!append) {
        ++m_commentsGeneration; m_commentsNext.clear(); m_commentsPages.clear();
        m_comments = {{"track", track}, {"items", QVariantList{}}, {"busy", true}, {"loaded", false}, {"sending", false}};
        emit commentsChanged();
    } else updateComments("busy", true);
    updateComments("error", "");
    const int generation = m_commentsGeneration;
    m_commentsPages.insert(QUrl(path.startsWith('/') ? "https://api.soundcloud.com" + path : path).toString());
    request(path, [this, generation, path](QJsonDocument doc) {
        if (generation != m_commentsGeneration) return;
        auto list = m_comments.value("items").toList();
        QSet<QString> seen;
        for (const auto &entry : list) seen.insert(entry.toMap().value("urn").toString());
        const auto collection = doc.isArray() ? doc.array() : doc.object().value("collection").toArray();
        for (const auto &entry : collection) {
            auto comment = entry.toObject().toVariantMap();
            auto id = comment.value("urn").toString();
            if (id.isEmpty() && comment.contains("id")) { id = "soundcloud:comments:" + comment.value("id").toString(); comment["urn"] = id; }
            if (id.isEmpty() || seen.contains(id)) continue;
            seen.insert(id);
            bool ok = false;
            const auto time = comment.value("timestamp").toString().toDouble(&ok);
            comment["timestamp"] = ok && std::isfinite(time) && time >= 0 && time < 86400000 ? qint64(time) : qint64(-1);
            list.append(comment);
        }
        const auto next = doc.object().value("next_href").toString();
        const QUrl nextUrl(next);
        // Pagination must stay on this track's comments endpoint.
        m_commentsNext = !m_commentsPages.contains(nextUrl.toString()) && nextUrl.scheme() == "https" && nextUrl.host() == "api.soundcloud.com"
            && nextUrl.path() == QUrl(path.startsWith('/') ? "https://api.soundcloud.com" + path : path).path()
            && nextUrl.userInfo().isEmpty() && (nextUrl.port() == -1 || nextUrl.port() == 443) ? next : QString{};
        updateComments("items", list); updateComments("hasMore", !m_commentsNext.isEmpty());
        updateComments("loaded", true); updateComments("busy", false);
    }, "GET", {}, true, [this, generation](QString message) {
        if (generation != m_commentsGeneration) return;
        updateComments("error", message); updateComments("busy", false);
    });
}

void SoundCloud::postComment(const QString &track, const QString &body, qint64 timestamp) {
    const auto text = body.trimmed();
    if ((track == urn(m_commentTrack) && m_commentTrack.value("commentable").isValid() && !m_commentTrack.value("commentable").isNull() && !m_commentTrack.value("commentable").toBool()) || !signedIn() || track != m_comments.value("track").toString() || text.isEmpty()
        || text.size() > 1000 || m_comments.value("sending").toBool()) return;
    QJsonObject comment{{"body", text}};
    if (timestamp >= 0) comment.insert("timestamp", double(timestamp));
    const int generation = m_commentsGeneration;
    updateComments("sending", true); updateComments("postError", "");
    // Retrying POST after an ambiguous failure could publish a duplicate.
    request(apiPath("tracks", track) + "/comments", [this, generation, track](QJsonDocument doc) {
        if (generation != m_commentsGeneration) return;
        auto entry = doc.object().toVariantMap();
        bool ok = false; const auto time = entry.value("timestamp").toString().toDouble(&ok);
        entry["timestamp"] = ok && std::isfinite(time) && time >= 0 && time < 86400000 ? qint64(time) : qint64(-1);
        if (!entry.contains("user")) entry["user"] = m_state.value("profile");
        auto list = m_comments.value("items").toList(); if (!m_comments.value("hidden").toBool()) list.prepend(entry);
        updateComments("items", list); updateComments("sending", false); emit commentPosted(track);
    }, "POST", {{"comment", comment}}, false, [this, generation](QString message) {
        if (generation != m_commentsGeneration) return;
        updateComments("sending", false); updateComments("postError", message);
    });
}

void SoundCloud::connectCatalog() {
    if (m_refreshing || m_state.value("authBusy").toBool()) return;
    // A saved account remains signed in while its token is checked over the network.
    set("signedIn", m_userSession && (!m_access.isEmpty() || !m_refresh.isEmpty()));
    clearError(); set("busy", true);
    ensureToken([this] { set("connected", true); set("signedIn", m_userSession); set("busy", false); set("status", m_userSession ? "Аккаунт подключён" : "Публичный каталог подключён"); if (m_userSession) { profile(); browse("likes"); } else { tryRestoreAndroidTrack(); browse("tracks", ""); } }, [this](QString message) { set("busy", false); fail(message); });
}
void SoundCloud::profile() { request("/me", [this](QJsonDocument d) {
    const auto profile = d.object().toVariantMap();
    if (urn(profile) != urn(m_state.value("profile").toMap())) {
        m_likedTracks.clear(); m_likeOverrides.clear(); m_likePages.clear(); m_firstLikes = {}; m_likesComplete = false;
    }
    set("profile", profile); restorePlayback(profile);
    if (signedIn()) { m_likedTracks.clear(); m_likesComplete = false; m_likePages.clear(); m_firstLikes = {}; syncLikes(); }
}); }
QVariantMap SoundCloud::withLikeState(QVariantMap track) const {
    const auto id = urn(track);
    const auto counts = m_state.value("engagementCounts").toMap().value(id).toMap();
    for (auto it = counts.begin(); it != counts.end(); ++it) track[it.key()] = it.value();
    if (!id.startsWith("soundcloud:tracks:")) return track;
    if (m_likeOverrides.contains(id)) track["user_favorite"] = m_likeOverrides.value(id);
    else if (m_likesComplete || m_likedTracks.contains(id)) track["user_favorite"] = m_likedTracks.contains(id);
    return track;
}
QVariantList SoundCloud::withLikeStates(QVariantList tracks) const {
    for (auto &entry : tracks) entry = withLikeState(entry.toMap());
    return tracks;
}
void SoundCloud::refreshLikeStates() {
    for (auto it = m_listings.begin(); it != m_listings.end(); ++it) it->tracks = withLikeStates(it->tracks);
    set("tracks", m_state.value("tracks")); set("queue", m_queue);
    set("current", m_state.value("current")); set("home", m_state.value("home"));
    set("playlistTracks", m_state.value("playlistTracks"));
}
void SoundCloud::readLikes(Callback done, TokenFailure failed) {
    if (!m_firstLikes.isNull()) { done(m_firstLikes); return; }
    const int generation = m_likeGeneration, revision = m_likeRevision;
    request("/me/likes/tracks?limit=200&linked_partitioning=true", [this, generation, revision, done, failed](QJsonDocument doc) {
        if (generation != m_likeGeneration) return;
        if (!doc.isArray() && !doc.object().value("collection").isArray()) {
            if (failed) failed("SoundCloud вернул некорректный список лайков."); else fail("SoundCloud вернул некорректный список лайков.");
            return;
        }
        if (revision == m_likeRevision) m_firstLikes = doc;
        done(doc);
    }, "GET", {}, true, failed);
}
void SoundCloud::syncLikes(const QString &path) {
    if (!signedIn() || m_likePages.contains(path)) return;
    m_likePages.insert(path);
    const int generation = m_likeGeneration;
    auto done = [this, path, generation](QJsonDocument doc) {
        if (generation != m_likeGeneration) return;
        // A malformed success response must not turn unknown likes into dislikes.
        if (!doc.isArray() && !doc.object().value("collection").isArray()) { m_likePages.remove(path); return; }
        for (const auto &entry : items(doc)) m_likedTracks.insert(urn(entry.toMap()));
        const auto next = doc.object().value("next_href").toString();
        m_likesComplete = next.isEmpty();
        refreshLikeStates();
        if (!next.isEmpty()) syncLikes(next);
    };
    auto failed = [this, path, generation](QString) { if (generation == m_likeGeneration) m_likePages.clear(); };
    if (path == "/me/likes/tracks?limit=200&linked_partitioning=true") readLikes(done, failed);
    else request(path, done, "GET", {}, true, failed);
}
void SoundCloud::savePlayback() {
    const QString account = urn(m_state.value("profile").toMap());
    if (!m_restoreChecked || account.isEmpty() || !signedIn()) return;
    QSettings settings;
    if (m_queueIndex < 0 || m_queueIndex >= m_queue.size() || m_state.value("current").toMap().isEmpty()) {
        settings.remove("playback/session"); settings.remove("playback/position"); return;
    }
    syncQueueOrder();
    if (m_waveMode.isEmpty()) nextQueueIndex();
    const QVariantMap snapshot{{"order", queueOrderSnapshot()}, {"repeatMode", m_state.value("repeatMode")}, {"shuffle", m_state.value("shuffle")}, {"account", account}, {"queue", m_queue}, {"index", m_queueIndex},
                               {"position", m_state.value("position")}, {"waveMode", m_waveMode},
                               {"waveNext", m_waveNext}, {"waveSeeds", m_waveSeeds}, {"waveFamiliar", m_familiarTracks}, {"waveCandidates", m_waveCandidates},
                               {"waveRelatedNext", m_waveRelatedNext}, {"waveLoadSeeds", m_waveLoadSeeds},
                               {"waveSeedIndex", m_waveSeedIndex}, {"waveSeen", QStringList(m_waveSeen.values())}};
    if (!m_waveMode.isEmpty())
        settings.setValue("playback/waves/" + m_waveMode, QJsonDocument::fromVariant(snapshot).toJson(QJsonDocument::Compact));
    settings.setValue("playback/session", QJsonDocument::fromVariant(snapshot).toJson(QJsonDocument::Compact));
    savePlaybackPosition();
}
void SoundCloud::savePlaybackPosition() {
    QElapsedTimer elapsed;
    const bool measure = qEnvironmentVariableIntValue("SCRIP_PERF") == 1;
    if (measure) elapsed.start();
    const auto report = qScopeGuard([&] {
        if (measure && elapsed.elapsed() >= 10) {
            std::fprintf(stderr, "SCripPerf save_position_ms=%lld\n", static_cast<long long>(elapsed.elapsed()));
            std::fflush(stderr);
        }
    });
    const auto account = urn(m_state.value("profile").toMap());
    const auto track = urn(m_state.value("current").toMap());
    if (!m_restoreChecked || !signedIn() || account.isEmpty() || track.isEmpty()) return;
    const QVariantMap position{{"account", account}, {"track", track}, {"position", m_state.value("position")}};
    writeProgress(position, m_waveMode);
}
void SoundCloud::restorePlayback(const QVariantMap &profile) {
    if (m_restoreChecked) return;
    m_restoreChecked = true;
    const auto snapshot = QJsonDocument::fromJson(QSettings().value("playback/session").toByteArray()).object().toVariantMap();
    if (urn(profile).isEmpty() || snapshot.value("account").toString() != urn(profile) || m_playGeneration != 0) {
        m_androidRestorePending = false; m_androidRestoreTrack.clear(); return;
    }
    const auto queue = snapshot.value("queue").toList();
    const int index = snapshot.value("index", -1).toInt();
    if (index < 0 || index >= queue.size() || queue.size() > 10000) return;
    for (const auto &track : queue) if (!urn(track.toMap()).startsWith("soundcloud:tracks:")) return;
    m_androidRestorePending = false; m_androidRestoreTrack.clear(); m_snapshotPlayback = true;
    m_queue = queue; m_queueIndex = index;
    restoreQueueOrder(snapshot.value("order").toMap());
    if (snapshot.contains("repeatMode") || snapshot.contains("repeat")) {
        const int mode = qBound(0, snapshot.value("repeatMode", snapshot.value("repeat").toBool() ? 1 : 0).toInt(), 2);
        set("repeatMode", mode); set("repeat", mode == 1); QSettings().setValue("player/repeatMode", mode);
    }
    if (snapshot.contains("shuffle")) { set("shuffle", snapshot.value("shuffle").toBool()); QSettings().setValue("player/shuffle", m_state.value("shuffle")); }
    restoreWaveState(snapshot);
    pruneRecommendations(false);
    set("queue", m_queue); set("queueIndex", index);
    const auto track = queue[index].toMap();
    auto savedPosition = readProgress().value("current").toObject().toVariantMap();
    if (savedPosition.isEmpty())
        savedPosition = QJsonDocument::fromJson(QSettings().value("playback/position").toByteArray()).object().toVariantMap();
    const bool sameTrack = savedPosition.value("account").toString() == urn(profile) && savedPosition.value("track").toString() == urn(track);
    m_restorePosition = qMax<qint64>(0, (sameTrack ? savedPosition.value("position") : snapshot.value("position")).toLongLong());
    set("current", track); set("duration", track.value("duration")); set("position", m_restorePosition);
}
void SoundCloud::login() { beginLogin(true); }
QUrl SoundCloud::beginLogin(bool openBrowser) {
    if (m_state.value("authBusy").toBool() || m_refreshing) return {};
    if (!m_storageReady) { fail(AuthStorage::errorMessage()); return {}; }
    if (!loadConfig()) { fail("Подключение ещё не настроено. См. раздел «Первое подключение» в README."); return {}; }
    const QUrl redirect(m_redirect);
    if (redirect.scheme() != "http" || redirect.host() != "127.0.0.1" || redirect.path() != "/callback" || (redirect.port() < 1024 || redirect.port() > 65535) || redirect.hasQuery() || redirect.hasFragment() || !redirect.userInfo().isEmpty()) { fail("Настройте локальный адрес возврата."); return {}; }
    m_callback.close();
    if (!m_callback.listen(QHostAddress::LocalHost, quint16(redirect.port()))) { fail("Порт для входа занят. Закройте второй экземпляр Sange или измените порт."); return {}; }
    m_retryToken.clear();
    ++m_loginEpoch; clearError(); m_verifier = nonce(); m_oauthState = nonce();
    QUrl url("https://secure.soundcloud.com/authorize"); QUrlQuery query;
    query.addQueryItem("client_id", m_clientId); query.addQueryItem("redirect_uri", m_redirect);
    query.addQueryItem("response_type", "code"); query.addQueryItem("code_challenge_method", "S256"); query.addQueryItem("state", m_oauthState);
    query.addQueryItem("code_challenge", QString::fromLatin1(QCryptographicHash::hash(m_verifier.toLatin1(), QCryptographicHash::Sha256).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    url.setQuery(query); set("authBusy", true); m_loginTimeout.start();
    set("authStep", "browser"); if (openBrowser) emit authorizationRequested(url);
    return url;
}
void SoundCloud::browserFailed() { cancelLogin(); fail("Не удалось открыть окно входа. Проверьте установленный браузер."); }
void SoundCloud::cancelLogin() { ++m_loginEpoch; m_callback.close(); m_retryToken.clear(); m_loginTimeout.stop(); m_oauthState.clear(); m_verifier.clear(); set("authBusy", false); set("authStep", ""); }
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
            if (parts.value(0) == "GET" && url.path() == "/retry" && !m_retryToken.isEmpty()
                && query.queryItemValue("token") == m_retryToken) {
                cancelLogin();
                const QUrl authorization = beginLogin(false);
                if (!authorization.isEmpty()) {
                    socket->write("HTTP/1.1 303 See Other\r\nLocation: " + authorization.toEncoded()
                        + "\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                    return;
                }
            }
            const bool valid = parts.value(0) == "GET" && url.path() == "/callback" && !m_oauthState.isEmpty() && query.queryItemValue("state") == m_oauthState;
            const bool receivedCode = !query.hasQueryItem("error") && !query.queryItemValue("code").isEmpty();
            const auto respond = [this, peer = QPointer<QTcpSocket>(socket), valid](bool success) {
                if (valid && !success) {
                    m_retryToken = nonce();
                    set("authBusy", false); set("authStep", "");
                    m_loginTimeout.start();
                }
                if (!peer || peer->state() != QAbstractSocket::ConnectedState) return;
                const auto html = callbackPage(valid, success, valid && !success ? m_retryToken : QString{});
                peer->write(QByteArray(valid && success ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 400 Bad Request\r\n") +
                    "Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\nContent-Length: " +
                    QByteArray::number(html.size()) + "\r\n\r\n" + html);
                peer->disconnectFromHost();
            };
            if (!valid) { respond(false); return; }
            const auto verifier = m_verifier; const int loginEpoch = m_loginEpoch;
            m_loginTimeout.stop(); m_oauthState.clear(); m_verifier.clear();
            if (!receivedCode) { respond(false); fail("Вход отменён в SoundCloud."); return; }
            set("authBusy", true); set("authStep", "exchange");
            QUrlQuery form; form.addQueryItem("grant_type", "authorization_code");
            if (!m_useWorker) {
                form.addQueryItem("client_id", m_clientId);
                form.addQueryItem("client_secret", m_clientSecret); form.addQueryItem("redirect_uri", m_redirect);
            }
            form.addQueryItem("code", query.queryItemValue("code")); form.addQueryItem("code_verifier", verifier);
            token(form, false, TokenFlow::AuthorizationCode, [this, respond] {
                cancelCollectionSearch(); m_commentTrack.clear();
                m_undoQueue.clear(); set("queueUndoAvailable", false);
                resetSocialState();
                // A successful OAuth exchange commits the candidate once. Drop
                // account-scoped UI data before exposing the newly active user.
                m_listings.clear(); m_homeLoaded = false; m_restoreChecked = true; QSettings().remove("playback/session"); QSettings().remove("playback/position");
                ++m_homeGeneration; ++m_listing; ++m_playGeneration;
                set("home", QVariantList{}); set("homeBusy", false); set("homeError", "");
                set("tracks", QVariantList{}); set("profile", QVariantMap{});
                set("destinations", QVariantList{}); set("destinationNext", "");
                set("loadingDestinations", false); set("savingTrack", false); set("busy", false);
                m_queue.clear(); m_queueIndex = -1; resetQueueOrder(); set("queue", m_queue); set("queueIndex", -1);
                stopPlayback();
                QSettings().remove("playback/waves"); QFile::remove(progressPath());
                set("connected", true); set("signedIn", true);
                set("authBusy", false);
                set("authStep", "");
                set("status", "Аккаунт подключён");
                respond(true);
                m_callback.close(); m_retryToken.clear();
                profile();
                browse("likes");
        }, [this, respond](QString message) { set("authBusy", false); set("authStep", ""); respond(false); fail(message); }, loginEpoch);
        });
    }
}
bool SoundCloud::clearSession() {
    const bool removed = m_authStorage.remove("session", m_sessionPath);
    const bool quarantined = removed || m_authStorage.save("session", {{"client_id", m_clientId},
        {"access_token", ""}, {"refresh_token", ""}, {"expires_at", 0}, {"user_session", false}});
    cancelCollectionSearch(); m_commentTrack.clear();
    m_listings.clear(); m_homeLoaded = false; m_restoreChecked = true; QSettings().remove("playback/session"); QSettings().remove("playback/position");
    ++m_homeGeneration; set("home", QVariantList{}); set("homeBusy", false); set("homeError", "");
    set("savingTrack", false); set("loadingDestinations", false); set("destinations", QVariantList{}); set("destinationNext", "");
    stopPlayback();
    m_followPages.clear();
    set("followings", QVariantMap{}); set("followingPending", QVariantMap{});
    set("followingsReady", false); set("followingsLoading", false);
    QSettings().remove("playback/waves"); QFile::remove(progressPath());
    ++m_commentsGeneration; m_commentsNext.clear(); m_commentsPages.clear(); m_comments.clear(); emit commentsChanged();
    ++m_epoch; m_reads.clear(); ++m_listing; ++m_playlistGeneration;
    m_playlistNext.clear();
    set("playlistTracks", QVariantList{}); set("playlistBusy", false); set("playlistHasMore", false); set("playlistError", "");
    m_access.clear(); m_refresh.clear(); m_expires = 0; m_userSession = false; m_refreshing = false; m_waiting.clear();
    m_queue.clear(); m_queueIndex = -1; resetQueueOrder(); m_nextHref.clear();
    set("queue", m_queue); set("queueIndex", -1); set("buffering", false); set("current", QVariantMap{}); set("tracks", QVariantList{}); set("profile", QVariantMap{});
    set("connected", false); set("signedIn", false); set("busy", false); set("hasMore", false); set("status", "Аккаунт отключён");
    if (!removed) {
        if (!quarantined) m_storageReady = false;
        fail(AuthStorage::errorMessage());
    }
    return removed;
}
void SoundCloud::logout() {
    cancelLogin();
    const bool revoke = m_storageReady && m_userSession && !m_access.isEmpty();
    const auto access = m_access;
    const bool cleared = clearSession();
    if (cleared) clearError();
    set("disconnectStatus", revoke ? "pending" : "local");
    if (revoke) {
        QNetworkRequest req(QUrl("https://api.soundcloud.com/disconnect"));
        req.setRawHeader("Authorization", "OAuth " + access.toUtf8());
        req.setRawHeader("Accept", "application/json");
        req.setTransferTimeout(25000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        auto *reply = m_network->sendCustomRequest(req, "POST");
        const int epoch = m_epoch;
        connect(reply, &QNetworkReply::finished, this, [this, reply, epoch] {
            reply->deleteLater();
            if (epoch != m_epoch || signedIn()) return;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool confirmed = reply->error() == QNetworkReply::NoError && status == 204;
            set("disconnectStatus", confirmed ? "confirmed" : "unconfirmed");
            set("status", confirmed ? "Вы вышли. Доступ Sange отозван." : "Вы вышли на этом устройстве. Отзыв доступа не подтверждён.");
            if (!confirmed) fail("Вы вышли на этом устройстве. Не удалось подтвердить отзыв доступа. Откройте настройки подключённых приложений SoundCloud и отключите Sange.");
        });
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
    if (!m_restoreChecked && !QSettings().value("playback/session").toByteArray().isEmpty()) return;
    if (!m_state.value("signedIn").toBool()) return;
    if (!m_state.value("current").toMap().isEmpty() || m_playGeneration != 0) {
        m_androidRestorePending = false; m_androidRestoreTrack.clear(); set("restoreCleared", true); return;
    }
    const auto track = m_androidRestoreTrack;
    const auto position = m_restorePosition;
    m_androidRestorePending = false; m_androidRestoreTrack.clear();
    m_queue = {track}; m_queueIndex = 0; resetQueueOrder();
    set("queue", m_queue); set("queueIndex", m_queueIndex);
    startTrack(track, false, position);
}
void SoundCloud::failAndroidRestore() {
    if (!m_restoringPlayback) return;
    m_restoringPlayback = false;
    if (m_snapshotPlayback) return;
    set("restoreCleared", true); set("current", QVariantMap{});
    m_queue.clear(); m_queueIndex = -1; resetQueueOrder(); set("queue", m_queue); set("queueIndex", -1);
}
// UI values are seconds and UTC calendar days; API ranges are inclusive.
QVariantMap SoundCloud::validateTrackFilters(const QVariantMap &filters) const {
    QUrlQuery query;
    for (const auto &key : {QStringLiteral("genres"), QStringLiteral("tags")}) {
        QStringList values;
        for (const auto &value : filters.value(key).toString().split(','))
            if (!value.trimmed().isEmpty()) values.append(value.trimmed());
        if (!values.isEmpty()) query.addQueryItem(key, values.join(','));
    }
    for (const auto &key : {QStringLiteral("duration"), QStringLiteral("bpm")}) {
        qint64 bounds[2] = {}; bool present[2] = {};
        for (int i = 0; i < 2; ++i) {
            const auto name = key + (i ? "To" : "From");
            const auto value = filters.value(name).toString().trimmed();
            if (value.isEmpty()) continue;
            bool ok = false; bounds[i] = value.toLongLong(&ok); present[i] = true;
            const qint64 maximum = key == "duration" ? 2147483 : 2147483647;
            if (!ok || bounds[i] < 0 || bounds[i] > maximum || !QRegularExpression("^[0-9]+$").match(value).hasMatch())
                return {{"error", key == "duration" ? "Длительность: укажите целые секунды от 0 до 2147483" : "BPM: укажите целое число от 0 до 2147483647"}};
            query.addQueryItem(key + (i ? "[to]" : "[from]"), QString::number(bounds[i] * (key == "duration" ? 1000 : 1)));
        }
        if (present[0] && present[1] && bounds[0] > bounds[1]) return {{"error", "Начало диапазона больше конца: " + key}};
    }
    QDate dates[2];
    for (int i = 0; i < 2; ++i) {
        const auto value = filters.value(i ? "createdTo" : "createdFrom").toString().trimmed();
        if (value.isEmpty()) continue;
        dates[i] = QDate::fromString(value, "yyyy-MM-dd");
        if (!dates[i].isValid() || dates[i].toString("yyyy-MM-dd") != value) return {{"error", "Дата загрузки: используйте ГГГГ-ММ-ДД"}};
        query.addQueryItem(i ? "created_at[to]" : "created_at[from]", value + (i ? " 23:59:59" : " 00:00:00"));
    }
    if (dates[0].isValid() && dates[1].isValid() && dates[0] > dates[1]) return {{"error", "Начальная дата позже конечной"}};
    if (filters.value("fullOnly").toBool()) query.addQueryItem("access", "playable");
    return {{"query", query.query(QUrl::FullyEncoded)}};
}
bool SoundCloud::applyTrackFilters(const QVariantMap &filters) {
    const auto result = validateTrackFilters(filters);
    if (result.contains("error")) return false;
    m_trackFilters = QUrlQuery(result.value("query").toString());
    return true;
}
void SoundCloud::browse(const QString &page, const QString &query, bool force) {
    cancelCollectionSearch(); set("collectionSearchStatus", "");
    clearError(); QString path;
    const int profileGeneration = ++m_profileBrowseGeneration;
    set("collectionStatus", "loading");
    const bool personal = page == "likes" || page == "mine" || page == "playlists" || page == "history" || page == "feed" || page == "reposts" || page == "likedPlaylists" || page == "likedStations" || page == "repostedPlaylists";
    if (personal && !m_state.value("signedIn").toBool()) { ++m_listing; set("tracks", QVariantList{}); set("hasMore", false); set("heading", "Войдите в SoundCloud"); set("collectionStatus", "unavailable"); m_nextHref.clear(); set("busy", false); return; }
    if (page == "likes") { path = "/me/likes/tracks"; set("heading", "Любимые треки"); }
    else if (page == "likedPlaylists") { path = "/me/likes/playlists"; set("heading", "Любимые плейлисты"); }
    else if (page == "likedStations") { path = "/me/likes/system-playlists"; set("heading", "Любимые станции"); }
    else if (page == "repostedPlaylists") { path = "/me/reposts/playlists"; set("heading", "Репосты плейлистов"); }
    else if (page == "reposts") { path = "/me/reposts/tracks"; set("heading", "Репосты"); }
    else if (page == "mine") { path = "/me/tracks"; set("heading", "Мои треки"); }
    else if (page == "playlists") { path = "/me/playlists"; set("heading", "Мои плейлисты"); }
    else if (page == "userTracks" || page == "userSets" || page == "userLikes" || page == "userLikedPlaylists" || page == "userLikedStations" || page == "userReposts" || page == "userRepostedPlaylists") {
        if (!query.startsWith("soundcloud:users:") || query.mid(17).isEmpty()) {
            ++m_listing; m_nextHref.clear(); set("tracks", QVariantList{}); set("hasMore", false); set("busy", false); set("collectionStatus", "error"); fail("Некорректный профиль SoundCloud."); return;
        }
        const auto userPath = apiPath("users", query);
        if (urn(m_state.value("visitedProfile").toMap()) != query)
            request(userPath, [this, query, profileGeneration](QJsonDocument doc) {
                if (profileGeneration == m_profileBrowseGeneration && urn(doc.object().toVariantMap()) == query) set("visitedProfile", doc.object().toVariantMap());
            }, "GET", {}, true, [this, profileGeneration](QString error) {
                if (profileGeneration == m_profileBrowseGeneration) fail(error);
            });
        const QHash<QString, QString> routes{{"userTracks", "/tracks"}, {"userSets", "/playlists"},
            {"userLikes", "/likes/tracks"}, {"userLikedPlaylists", "/likes/playlists"},
            {"userLikedStations", "/likes/system-playlists"}, {"userReposts", "/reposts/tracks"},
            {"userRepostedPlaylists", "/reposts/playlists"}};
        path = userPath + routes.value(page);
        set("heading", "Публичная коллекция");
    }
    else if (page == "history") { path = "/me/recently-played/tracks"; set("heading", "Недавно слушали"); }
    else if (page == "feed") { path = "/me/feed/tracks"; set("heading", "Лента подписок"); }
    else {
        if (query.trimmed().isEmpty() && (page != "tracks" || m_trackFilters.isEmpty())) { m_nextHref.clear(); ++m_listing; set("busy", false); set("tracks", QVariantList{}); set("hasMore", false); set("heading", "Введите название или исполнителя"); return; }
        path = page == "users" ? "/users" : page == "sets" ? "/playlists" : "/tracks";
        QUrlQuery q = page == "tracks" ? m_trackFilters : QUrlQuery{}; if (!query.trimmed().isEmpty()) q.addQueryItem("q", query.trimmed()); path += "?" + q.query(QUrl::FullyEncoded); set("heading", "Результаты: " + query.trimmed());
    }
    if (page != "history") path += (path.contains('?') ? "&" : "?") + QString("limit=30&linked_partitioning=true");
    if (!force && path == m_lastPath && m_state.value("busy").toBool()) return;
    m_lastPath = path;
    if (!force && m_listings.contains(path)) {
        ++m_listing; const auto cached = m_listings.value(path); m_nextHref = cached.next;
        set("collectionStatus", cached.tracks.isEmpty() ? "empty" : "ready");
        set("busy", false); set("tracks", cached.tracks); set("hasMore", !m_nextHref.isEmpty());
        if (page == "reposts") {
            QSet<QString> pending;
            for (const auto &entry : cached.tracks) if (!entry.toMap().value("user_favorite").toBool()) pending.insert(urn(entry.toMap()));
            if (!pending.isEmpty() && !m_likesComplete) syncLikes();
        }
        return;
    }
    collection(path);
}
// Pagination may return legacy numeric resource paths for URN-based requests.
// Compare their identity without manufacturing or requesting a new identifier.
static QString paginationResourcePath(const QUrl &url) {
    auto path = url.path(QUrl::FullyDecoded);
    path.replace(QRegularExpression("/soundcloud:(tracks|playlists|users):([0-9]+)(?=/|$)"), "/\\2");
    return path;
}
void SoundCloud::collection(const QString &path, bool append) {
    int generation = ++m_listing; set("collectionStatus", "loading"); set("busy", true); set("hasMore", false);
    if (!append) { m_collectionPages.clear(); m_nextHref.clear(); set("tracks", QVariantList{}); }
    QString requestPath = path;
    if (append && m_lastPath.startsWith("/tracks?")) {
        QUrl next(path); QUrlQuery query(next);
        const QUrlQuery original(QUrl(m_lastPath).query());
        for (const auto &item : original.queryItems()) {
            query.removeAllQueryItems(item.first);
            query.addQueryItem(item.first, item.second);
        }
        next.setQuery(query); requestPath = next.toString(QUrl::FullyEncoded);
    }
    request(requestPath, [this, generation, append, requestPath](QJsonDocument doc) {
        if (generation != m_listing) return;
        if (!doc.isArray() && !doc.object().value("collection").isArray()) {
            cancelCollectionSearch(); set("collectionSearchStatus", "error");
            set("collectionStatus", "error"); set("busy", false);
            set("hasMore", append && !m_nextHref.isEmpty()); fail("SoundCloud вернул неполный список. Повторите загрузку."); return;
        }
        auto list = append ? m_state.value("tracks").toList() : QVariantList{};
        QSet<QString> seen;
        for (const auto &old : list) seen.insert(urn(old.toMap()));
        for (auto item : items(doc)) {
            auto map = item.toMap();
            if (m_lastPath.startsWith("/me/likes/tracks")) {
                m_likedTracks.insert(urn(map)); map["user_favorite"] = true;
            }
            const auto id = urn(map);
            const bool playlistRows = m_lastPath.startsWith("/playlists/") && QUrl(m_lastPath).path().endsWith("/tracks");
            if (!playlistRows && seen.contains(id)) continue;
            seen.insert(id);
            list.append(map);
        }
        m_collectionPages.insert(requestPath);
        set("collectionStatus", list.isEmpty() ? "empty" : "ready");
        set("tracks", list);
        const auto next = doc.object().value("next_href").toString();
        const QUrl nextUrl(next);
        const bool invalidNext = !next.isEmpty() && (next == m_nextHref || m_collectionPages.contains(next)
            || nextUrl.scheme() != "https" || nextUrl.host() != "api.soundcloud.com" || !nextUrl.userInfo().isEmpty()
            || (nextUrl.port() != -1 && nextUrl.port() != 443) || paginationResourcePath(nextUrl) != paginationResourcePath(QUrl(requestPath.startsWith('/') ? "https://api.soundcloud.com" + requestPath : requestPath)));
        m_nextHref = invalidNext ? QString{} : next;
        if (invalidNext) { cancelCollectionSearch(); set("collectionSearchStatus", "error"); }
        set("hasMore", !m_nextHref.isEmpty()); set("busy", false);
        if (m_listings.size() >= 8 && !m_listings.contains(m_lastPath)) m_listings.erase(m_listings.begin());
        m_listings.insert(m_lastPath, {list, m_nextHref});
        if (m_collectionSearch && !m_searchPlaylist) {
            if (m_nextHref.isEmpty()) { cancelCollectionSearch(); set("collectionSearchStatus", "complete"); }
            else if (--m_searchPagesRemaining <= 0) { cancelCollectionSearch(); set("collectionSearchStatus", "limit"); }
            else QTimer::singleShot(350, this, [this, generation] { if (m_collectionSearch && !m_searchPlaylist && generation == m_listing) more(); });
        }
        if (m_lastPath.startsWith("/me/likes/tracks")) refreshLikeStates();
        if (m_lastPath.startsWith("/me/reposts/tracks")) {
            QSet<QString> pending;
            for (const auto &entry : list) if (!entry.toMap().value("user_favorite").toBool()) pending.insert(urn(entry.toMap()));
            if (!pending.isEmpty() && !m_likesComplete) syncLikes();
        }
    }, "GET", {}, true, [this, generation, append](QString error) {
        if (generation != m_listing) return;
        cancelCollectionSearch(); set("collectionSearchStatus", "error");
        const bool unavailable = error.contains("не нашёл этот ресурс") || error.contains("ограничил доступ");
        set("collectionStatus", unavailable ? "unavailable" : "error");
        set("hasMore", append && !m_nextHref.isEmpty()); set("busy", false); fail(error);
    });
}
void SoundCloud::relatedSources(const QVariantList &sources, std::function<bool()> current, Callback done, TokenFailure failed) {
    const auto selected = Recommendations::seeds(sources);
    if (selected.isEmpty()) { done(QJsonDocument(QJsonArray{})); return; }
    struct Batch { QList<QVariantList> results; int remaining; QString error; };
    auto batch = std::make_shared<Batch>();
    batch->results.resize(selected.size()); batch->remaining = selected.size();
    auto finish = [batch, current, done, failed] {
        if (!current() || --batch->remaining) return;
        QVariantList pool;
        // Round-robin sources; completion order must not affect ranking.
        for (int row = 0; row < 40; ++row)
            for (const auto &list : std::as_const(batch->results)) if (row < list.size()) pool.append(list[row]);
        if (pool.isEmpty() && !batch->error.isEmpty()) { failed(batch->error); return; }
        done(QJsonDocument(QJsonArray::fromVariantList(pool)));
    };
    for (int i = 0; i < selected.size(); ++i) {
        const auto seed = selected[i].toMap();
        request(apiPath("tracks", urn(seed)) + "/related?limit=40&linked_partitioning=true",
            [batch, i, seed, current, finish](QJsonDocument doc) {
                if (!current()) return;
                auto tracks = items(doc).mid(0, 40);
                for (auto &entry : tracks) {
                    auto track = entry.toMap();
                    track["recommendationSource"] = seed.value("title");
                    track["recommendationSourceUrn"] = urn(seed); entry = track;
                }
                batch->results[i] = tracks; finish();
            }, "GET", {}, true, [batch, current, finish](QString error) {
                if (!current()) return;
                batch->error = error; finish();
            }, {}, 0, true);
    }
}

void SoundCloud::loadHome(bool force) {
    if (!m_state.value("signedIn").toBool() || m_state.value("homeBusy").toBool()) return;
    if (!force && m_homeLoaded) return;
    if (force) m_firstLikes = {};
    const int generation = ++m_homeGeneration;
    set("homeBusy", true); set("homeError", ""); set("home", QVariantList{});
    // Независимые источники: недоступность одного не скрывает остальные.
    auto remaining = std::make_shared<int>(3);
    auto finish = [this, generation, remaining] {
        if (generation == m_homeGeneration && --*remaining == 0) {
            m_homeLoaded = m_state.value("homeError").toString().isEmpty(); set("homeBusy", false);
        }
    };
    auto failed = [this, generation, finish](QString error) {
        if (generation != m_homeGeneration) return;
        set("homeError", error); finish();
    };
    auto add = [this, generation](QString title, QString subtitle, QVariantList tracks, bool mix, int order, bool recommendations = false) {
        if (generation != m_homeGeneration) return;
        if (recommendations) {
            tracks = filterRecommendations(tracks);
            for (auto &entry : tracks) { auto track = entry.toMap(); track["scripAutomaticRecommendation"] = true; entry = track; }
        }
        QVariantList clean;
        QSet<QString> seen;
        for (const auto &track : tracks) {
            auto id = urn(track.toMap());
            if (!id.startsWith("soundcloud:tracks:") || seen.contains(id)) continue;
            seen.insert(id); clean.append(track);
        }
        if (clean.isEmpty()) return;
        auto shelves = m_state.value("home").toList();
        shelves.append(QVariantMap{{"title", title}, {"subtitle", subtitle}, {"tracks", clean}, {"mix", mix}, {"order", order}, {"recommendations", recommendations}});
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
    readLikes([this, generation, add, finish, failed](QJsonDocument doc) {
        if (generation != m_homeGeneration) return;
        auto tracks = items(doc).mid(0, 24);
        for (auto &entry : tracks) { auto track = entry.toMap(); track["user_favorite"] = true; entry = track; }
        add("Микс любимых", "Подборка Sange из последних добавленных любимых треков", tracks, true, 0);
        if (tracks.isEmpty()) { finish(); return; }
        QSet<QString> excluded;
        for (const auto &track : items(doc)) excluded.insert(urn(track.toMap()));
        relatedSources(items(doc), [this, generation] { return generation == m_homeGeneration; },
            [this, add, finish, excluded](QJsonDocument related) {
                const auto ordered = Recommendations::select(filterRecommendations(items(related)), 48, excluded);
                QVariantList suggestions, discoveries;
                QHash<QString, int> suggestionArtists, discoveryArtists;
                for (const auto &track : ordered) {
                    const auto artist = Recommendations::artist(track);
                    const bool suggest = suggestions.size() < discoveries.size()
                        || (suggestions.size() == discoveries.size()
                            && suggestionArtists.value(artist) <= discoveryArtists.value(artist));
                    (suggest ? suggestions : discoveries).append(track);
                    ++(suggest ? suggestionArtists : discoveryArtists)[artist];
                }
                suggestions = Recommendations::select(suggestions, 24);
                discoveries = Recommendations::select(discoveries, 24);
                const auto explanation = [](const QVariantList &list) {
                    QSet<QString> sources;
                    for (const auto &entry : list) sources.insert(entry.toMap().value("recommendationSourceUrn").toString());
                    if (sources.size() > 1) return QString("По нескольким любимым трекам · SoundCloud");
                    const auto title = list.isEmpty() ? QString{} : list.first().toMap().value("recommendationSource").toString();
                    return title.isEmpty() ? QString("По похожим трекам · SoundCloud") : "Похоже на «" + title + "» · SoundCloud";
                };
                add("Вам может понравиться", explanation(suggestions), suggestions, false, 2, true);
                add("Микс открытий", explanation(discoveries), discoveries, true, 0, true); finish();
            }, failed);
    }, failed);
    QTimer::singleShot(60000, this, [this, generation] {
        if (generation == m_homeGeneration && m_state.value("homeBusy").toBool()) {
            ++m_homeGeneration; set("homeBusy", false); set("homeError", "Загрузка заняла слишком много времени. Обновите главную.");
        }
    });
}
QVariantList SoundCloud::equalizerGains() const {
    QVariantList result;
    for (double db : m_equalizerGains) result.append(db);
    return result;
}
void SoundCloud::setEqualizerEnabled(bool enabled) {
    enabled = enabled && equalizerAvailable();
    if (m_equalizerEnabled == enabled) return;
    m_equalizerEnabled = enabled; m_equalizerError.clear();
    m_playerEffects.clear(); m_spareEffects.clear();
    QSettings().setValue("player/equalizerEnabled", enabled);
    updateAudioRouting();
    emit soundControlsChanged();
}
void SoundCloud::setEqualizerGain(int band, double db) {
    if (band < 0 || band >= 5 || !std::isfinite(db)) return;
    m_equalizerGains[band] = qBound(-12.0, db, 12.0);
    m_playerEffects.setGains(m_equalizerGains); m_spareEffects.setGains(m_equalizerGains);
    QSettings().setValue(QString("player/equalizer/%1").arg(band), m_equalizerGains[band]);
    emit soundControlsChanged();
}
void SoundCloud::resetEqualizer() {
    for (int i = 0; i < 5; ++i) setEqualizerGain(i, 0);
}
void SoundCloud::setTrackVisualActive(bool enabled) {
    if (m_trackVisualActive == enabled) return;
    m_trackVisualActive = enabled;
    updateAudioRouting();
}
void SoundCloud::updateAudioRouting() {
    const bool capture = m_waveVisualActive || m_trackVisualActive || m_equalizerEnabled;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    m_player.setAudioBufferOutput(capture ? &m_waveOutput : nullptr);
    m_sparePlayer.setAudioBufferOutput(capture ? &m_waveSpareOutput : nullptr);
#endif
    // Keep original outputs as playback clocks, with their existing volume/crossfade ramps.
    m_audio.setMuted(m_equalizerEnabled); m_preloadAudio.setMuted(m_equalizerEnabled);
    if (m_waveVisualActive || m_trackVisualActive) {
        if (!m_waveAudioTimer.isActive()) { m_waveBeatClock.start(); m_waveAudioTimer.start(); }
    } else {
        m_waveAudioTimer.stop();
        m_waveLevel = m_waveBass = m_waveBeat = 0;
        m_waveTargetLevel = m_waveTargetBass = m_waveTargetBeat = 0;
        m_waveLowPass = m_waveBaseline = 0;
        m_spectrum = m_spectrumTarget = QVariantList(AudioDsp::spectrumBands, 0.0);
        emit spectrumChanged(); emit waveAudioChanged();
    }
}
void SoundCloud::setWaveVisualActive(bool enabled) {
    if (m_waveVisualActive == enabled) return;
    m_waveVisualActive = enabled;
    updateAudioRouting();
}
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
void SoundCloud::processAudio(QMediaPlayer *media, const QAudioBuffer &buffer) {
    if (!buffer.isValid() || buffer.frameCount() <= 0 || media->playbackState() != QMediaPlayer::PlayingState) return;
    if (media != m_activePlayer && !m_crossfadeStarted) return;
    QByteArray samples;
    if (m_equalizerEnabled) {
        auto &effects = media == &m_player ? m_playerEffects : m_spareEffects;
        const double volume = media->audioOutput() ? media->audioOutput()->volume() : 0;
        if (!effects.write(buffer, volume)) {
            const auto error = effects.error();
            setEqualizerEnabled(false);
            m_equalizerError = error;
            emit soundControlsChanged();
            return;
        }
        samples = effects.processed();
    }
    if (media != m_activePlayer) return;
    if (m_waveVisualActive) analyzeWaveAudio(buffer);
    if (m_trackVisualActive) {
        if (samples.isEmpty()) samples = AudioEffects::floatSamples(buffer);
        m_spectrumTarget = AudioDsp::spectrum(reinterpret_cast<const float *>(samples.constData()),
                                             buffer.frameCount(), buffer.format().channelCount(), buffer.format().sampleRate());
    }
}
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
void SoundCloud::analyzeWaveAudio(const QAudioBuffer &buffer) {
    if (!m_waveVisualActive || !buffer.isValid()) return;
    const auto format = buffer.format();
    if (format.sampleRate() <= 0 || format.channelCount() <= 0 || format.bytesPerSample() <= 0) return;
    const auto *data = buffer.constData<char>();
    // Downsample the analysis to <= 12 kHz; playback buffers remain untouched.
    const int stride = qMax(1, (format.sampleRate() + 11999) / 12000);
    const double step = double(stride) / format.sampleRate();
    const double alpha = 1.0 - std::exp(-2.0 * 3.141592653589793 * 160.0 * step);
    double energy = 0, lowEnergy = 0;
    int count = 0;
    for (int frame = 0; frame < buffer.frameCount(); frame += stride) {
        double mono = 0;
        for (int channel = 0; channel < qMin(format.channelCount(), 2); ++channel)
            mono += format.normalizedSampleValue(data + frame * format.bytesPerFrame() + channel * format.bytesPerSample());
        mono /= qMin(format.channelCount(), 2);
        if (!std::isfinite(mono)) mono = 0;
        m_waveLowPass += alpha * (mono - m_waveLowPass);
        energy += mono * mono; lowEnergy += m_waveLowPass * m_waveLowPass; ++count;
    }
    if (!count) return;
    const double rms = std::sqrt(energy / count), bass = std::sqrt(lowEnergy / count);
    const double duration = double(buffer.frameCount()) / format.sampleRate();
    const double baselineAlpha = 1.0 - std::exp(-duration / 0.65);
    const bool onset = bass > qMax(0.025, m_waveBaseline * 1.5) && m_waveBeatClock.elapsed() > 180;
    m_waveBaseline += baselineAlpha * (bass - m_waveBaseline);
    m_waveTargetLevel = qMax(m_waveTargetLevel, qBound(0.0, rms * 3.2, 1.0));
    m_waveTargetBass = qMax(m_waveTargetBass, qBound(0.0, bass * 5.0, 1.0));
    if (onset) { m_waveTargetBeat = qBound(0.0, bass * 6.0, 1.0); m_waveBeatClock.restart(); }
}
#endif

void SoundCloud::restoreWaveState(const QVariantMap &snapshot) {
    const QString mode = snapshot.value("waveMode").toString();
    if (mode != "library" && mode != "discovery") return;
    m_familiarTracks = snapshot.value("waveFamiliar").toList().mid(0, 200);
    m_waveCandidates = snapshot.value("waveCandidates").toList().mid(0, 120);
    m_waveMode = mode; m_waveNext = snapshot.value("waveNext").toString();
    m_waveSeeds = snapshot.value("waveSeeds").toList();
    m_waveRelatedNext = snapshot.value("waveRelatedNext").toString();
    m_waveLoadSeeds = snapshot.value("waveLoadSeeds").toBool();
    m_waveRetryAttempts = 0;
    m_waveSeedIndex = qMax(0, snapshot.value("waveSeedIndex").toInt());
    m_waveSeen.clear();
    for (const auto &id : snapshot.value("waveSeen").toStringList()) m_waveSeen.insert(id);
    set("waveMode", mode);
}
void SoundCloud::stopWave() {
    if (!m_waveMode.isEmpty()) savePlayback();
    ++m_waveGeneration;
    m_waveRetryTimer.stop();
    m_waveMode.clear(); m_waveWaiting = false; m_wavePreferenceWaiting = false;
    set("waveMode", ""); set("waveBusy", false); set("waveError", "");
}
void SoundCloud::startWave(const QString &mode) {
    if (!signedIn() || (mode != "library" && mode != "discovery")) return;
    if (mode == m_waveMode) { togglePlayback(); return; }
    stopPlayback();
    const auto saved = QJsonDocument::fromJson(QSettings().value("playback/waves/" + mode).toByteArray()).object().toVariantMap();
    const auto queue = saved.value("queue").toList();
    const int index = saved.value("index", -1).toInt();
    const bool valid = saved.value("account").toString() == urn(m_state.value("profile").toMap())
        && saved.value("waveMode").toString() == mode && index >= 0 && index < queue.size() && queue.size() <= 500
        && std::all_of(queue.begin(), queue.end(), [](const QVariant &t) { return urn(t.toMap()).startsWith("soundcloud:tracks:"); });
    if (valid) {
        m_queue = queue; m_queueIndex = index; resetQueueOrder();
        restoreWaveState(saved);
        m_waveWaiting = false; m_waveEmptyPages = 0;
        pruneRecommendations(false);
        set("queue", m_queue); set("queueIndex", index);
        startTrack(queue[index].toMap(), true, wavePosition(saved, mode));
        return;
    }
    m_familiarTracks.clear(); m_waveCandidates.clear();
    m_waveMode = mode; m_waveNext.clear(); m_waveSeeds.clear(); m_waveSeen.clear();
    m_waveRelatedNext.clear(); m_waveLoadSeeds = false; m_waveRetryAttempts = 0;
    m_waveSeedIndex = 0; m_waveEmptyPages = 0; m_waveWaiting = true;
    m_queue.clear(); m_queueIndex = -1; resetQueueOrder();
    set("queue", m_queue); set("queueIndex", -1); set("waveMode", mode);
    setRepeat(false); setShuffle(false);
    set("playbackActive", true);
    refillWave();
}
void SoundCloud::chooseWavePreference(int value) {
    if (!signedIn() || m_recommendationAccount.isEmpty() || value < 0 || value > 2) return;
    setRecommendationPreference(value);
    ++m_waveGeneration;
    m_waveRetryTimer.stop(); m_waveRetryAttempts = 0; m_waveEmptyPages = 0;
    m_waveWaiting = false; m_wavePreferenceWaiting = false;
    set("waveBusy", false); set("waveError", "");
    if (m_crossfadeStarted) finishCrossfade(false);
    cancelPreload();
    syncQueueOrder();
    QVariantList candidates = m_waveCandidates;
    // Reconsider only unplayed automatic entries. Explicit additions keep their IDs and order.
    for (int i = m_queue.size() - 1; i > m_queueIndex; --i) {
        if (!m_queue[i].toMap().value("scripAutomaticRecommendation").toBool()) continue;
        candidates.prepend(m_queue[i]);
        if (!m_history.contains(m_queueIds[i])) m_waveSeen.remove(urn(m_queue[i].toMap()));
        m_queue.removeAt(i); m_queueIds.removeAt(i);
    }
    auto excluded = m_waveSeen;
    for (const auto &entry : std::as_const(m_queue)) {
        excluded.insert(urn(entry.toMap()));
        m_waveSeen.insert(urn(entry.toMap()));
    }
    if (m_familiarTracks.isEmpty()) m_familiarTracks = items(m_firstLikes).mid(0, 200);
    if (m_waveMode != "discovery") {
        m_waveNext.clear(); m_waveRelatedNext.clear(); m_waveLoadSeeds = false;
        m_waveSeeds = Recommendations::seeds(m_familiarTracks);
        if (m_waveSeeds.isEmpty() && m_queueIndex >= 0 && m_queueIndex < m_queue.size())
            m_waveSeeds = {m_queue[m_queueIndex]};
        m_waveSeedIndex = 0;
    }
    m_waveMode = "discovery"; set("waveMode", m_waveMode);
    const auto selected = Recommendations::mix(filterRecommendations(m_familiarTracks),
        filterRecommendations(withLikeStates(candidates)), value, 20, excluded);
    int first = -1;
    for (const auto &entry : selected) {
        auto track = entry.toMap(); track["scripAutomaticRecommendation"] = true;
        const int at = first < 0 ? m_queueIndex + 1 : m_queue.size();
        if (first < 0) first = at;
        m_queue.insert(at, track); m_queueIds.insert(at, m_nextQueueId++);
        m_waveSeen.insert(urn(track));
        m_waveSeeds.append(track);
        if (m_waveSeeds.size() > 100) m_waveSeeds.removeFirst();
    }
    m_plannedId = -1; m_forceNext = false;
    set("queue", m_queue);
    if (first >= 0) {
        playQueue(first);
    } else {
        // Keep the current audio until an eligible replacement arrives. Do not consume a manual entry.
        m_wavePreferenceWaiting = true;
        set("playbackActive", true); savePlayback(); refillWave();
    }
}
void SoundCloud::retryWave() {
    if (m_waveMode.isEmpty()) return;
    m_waveEmptyPages = 0; m_waveRetryAttempts = 0;
    if (m_waveRetryTimer.isActive()) { m_waveRetryTimer.stop(); set("waveBusy", false); }
    if (m_queueIndex + 1 >= m_queue.size()) { m_waveWaiting = true; set("playbackActive", true); }
    refillWave();
}
void SoundCloud::scheduleWaveRefill() {
    if (m_waveRetryAttempts >= 3) { set("waveBusy", false); set("waveError", "Не удалось загрузить рекомендации. Повторите позже."); return; }
    if (m_waveMode != "discovery" || (!m_waveWaiting && !m_state.value("playbackActive").toBool())) return;
    // Retry quietly without hammering an exhausted graph or an unavailable service.
    const auto delay = qMax(qint64(qMin(60000, 3000 * (1 << qMin(m_waveRetryAttempts, 5)))),
                            m_rateUntil.value("api") - m_requestNow());
    m_waveRetryAttempts = qMin(m_waveRetryAttempts + 1, 5);
    m_waveEmptyPages = 0;
    set("waveError", ""); set("waveBusy", true);
    m_waveRetryTimer.start(int(qMin(delay, qint64(std::numeric_limits<int>::max()))));
    savePlayback();
}
void SoundCloud::refillWave() {
    if (m_waveMode.isEmpty() || m_state.value("waveBusy").toBool()) return;
    const int generation = m_waveGeneration;
    const bool seeds = m_waveMode == "discovery" && (m_waveSeeds.isEmpty() || m_waveLoadSeeds);
    QString path;
    if (m_waveMode == "library" || seeds)
        path = m_waveNext.isEmpty() ? QString("/me/likes/tracks?limit=50&linked_partitioning=true") : m_waveNext;
    else if (!m_waveRelatedNext.isEmpty()) path = m_waveRelatedNext;
    else {
        m_waveSeedIndex %= m_waveSeeds.size();
        const auto seed = m_waveSeeds[m_waveSeedIndex++].toMap();
        path = apiPath("tracks", urn(seed)) + "/related?limit=40&linked_partitioning=true";
    }
    set("waveBusy", true); set("waveError", "");
    const bool multiple = m_waveMode == "discovery" && !seeds && m_waveRelatedNext.isEmpty()
        && Recommendations::seeds(m_waveSeeds).size() > 1;
    auto received = [this, generation, seeds, path, multiple](QJsonDocument doc) {
        if (generation != m_waveGeneration) return;
        set("waveBusy", false);
        auto tracks = items(doc);
        if (m_waveMode == "discovery" && !seeds && !multiple && !m_waveSeeds.isEmpty()) {
            const auto seed = m_waveSeeds[(m_waveSeedIndex - 1 + m_waveSeeds.size()) % m_waveSeeds.size()].toMap();
            for (auto &entry : tracks) {
                auto track = entry.toMap(); track["recommendationSource"] = seed.value("title");
                track["recommendationSourceUrn"] = urn(seed); entry = track;
            }
        }
        if (seeds) {
            m_waveLoadSeeds = false;
            m_familiarTracks = (m_familiarTracks + tracks).mid(0, 200);
            for (const auto &entry : tracks) {
                const auto id = urn(entry.toMap());
                if (Recommendations::playable(entry) && !m_waveSeen.contains(id)) {
                    m_waveSeeds.append(entry);
                    if (m_waveSeeds.size() > 100) m_waveSeeds.removeFirst();
                }
            }
            m_waveNext = doc.object().value("next_href").toString();
            if (m_waveNext == path) m_waveNext.clear();
            if (!m_waveSeeds.isEmpty()) { refillWave(); return; }
        } else {
            if (m_waveMode == "discovery") {
                m_waveRelatedNext = doc.object().value("next_href").toString();
                if (m_waveRelatedNext == path) m_waveRelatedNext.clear();
            }
            if (m_waveMode == "library") m_waveNext = doc.object().value("next_href").toString();
            const auto before = m_queue.size();
            // A finite library cycles only after all its pages have been consumed.
            if (m_waveMode == "library" && m_waveNext.isEmpty() && tracks.isEmpty()) m_waveSeen.clear();
            if (m_waveMode == "library") {
                for (int i = tracks.size() - 1; i > 0; --i)
                    tracks.swapItemsAt(i, QRandomGenerator::global()->bounded(i + 1));
            }
            if (m_waveMode == "discovery") {
                m_waveCandidates = tracks.mid(0, 120);
                auto excluded = m_waveSeen;
                for (const auto &entry : std::as_const(m_queue)) excluded.insert(urn(entry.toMap()));
                tracks = Recommendations::mix(filterRecommendations(m_familiarTracks),
                    filterRecommendations(withLikeStates(tracks)), m_recommendationPreference, 20, excluded);
                for (auto &entry : tracks) {
                    auto track = entry.toMap(); track["scripAutomaticRecommendation"] = true; entry = track;
                }
            }
            for (const auto &entry : tracks) {
                const auto id = urn(entry.toMap());
                if (!Recommendations::playable(entry) || m_waveSeen.contains(id)) continue;
                m_waveSeen.insert(id); m_queue.append(entry);
                if (m_waveMode == "discovery") {
                    m_waveSeeds.append(entry);
                    if (m_waveSeeds.size() > 100) m_waveSeeds.removeFirst();
                }
            }
            if (m_waveMode == "library" && m_waveNext.isEmpty()) m_waveSeen.clear();
            if (m_queue.size() > before) {
                int firstAdded = int(before);
                m_waveEmptyPages = 0; m_waveRetryAttempts = 0;
                // Keep a short playback history and a bounded session footprint.
                if (m_queueIndex > 40) {
                    const int remove = m_queueIndex - 20;
                    firstAdded -= remove;
                    syncQueueOrder(); m_queueIds = m_queueIds.mid(remove);
                    m_queue = m_queue.mid(remove); m_queueIndex -= remove;
                    ++m_preloadGeneration; m_preloadIndex = -1; m_preloadUrn.clear();
                    set("queueIndex", m_queueIndex);
                }
                set("queue", m_queue);
                if (m_wavePreferenceWaiting) {
                    m_wavePreferenceWaiting = false;
                    const int target = m_queueIndex + 1;
                    syncQueueOrder();
                    m_queue.move(firstAdded, target); m_queueIds.move(firstAdded, target);
                    set("queue", m_queue); playQueue(target);
                }
                else if (m_waveWaiting) { m_waveWaiting = false; playQueue(m_queueIndex + 1); }
                else { savePlayback(); prepareNext(); }
                return;
            }
        }
        m_waveEmptyPages += multiple ? 3 : 1;
        if (m_waveEmptyPages < 6 && (!m_waveSeeds.isEmpty() || !m_waveNext.isEmpty())) { refillWave(); return; }
        if (m_waveMode == "discovery") {
            m_waveRelatedNext.clear();
            m_waveLoadSeeds = !m_waveNext.isEmpty();
            scheduleWaveRefill();
        } else set("waveError", "Добавьте любимые треки и попробуйте снова.");
    };
    auto failed = [this, generation](QString) {
        if (generation != m_waveGeneration) return;
        set("waveBusy", false);
        if (m_waveMode == "discovery") scheduleWaveRefill();
        else set("waveError", "Волна не загрузилась. Проверьте подключение и повторите.");
    };
    if (multiple) {
        auto rotated = m_waveSeeds;
        const int offset = (m_waveSeedIndex - 1 + rotated.size()) % rotated.size();
        std::rotate(rotated.begin(), rotated.begin() + offset, rotated.end());
        m_waveSeedIndex += 2;
        relatedSources(rotated, [this, generation] { return generation == m_waveGeneration; }, received, failed);
    } else request(path, received, "GET", {}, true, failed, {}, 0, true);
}

void SoundCloud::playMix(const QVariantList &tracks, int index) {
    if (tracks.isEmpty()) return;
    stopWave();
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
    m_queue = queue; resetQueueOrder();
    set("queue", m_queue); playQueue(playIndex);
}
void SoundCloud::playStation(const QVariantMap &seed) {
    stopWave();
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
    const int generation = m_playlistOperationGeneration;
    set("loadingDestinations", true);
    request(next.isEmpty() ? QString("/me/playlists?limit=50&linked_partitioning=true") : next,
        [this, generation](QJsonDocument doc) {
            if (generation != m_playlistOperationGeneration) return;
            auto list = m_state.value("destinations").toList();
            list.append(items(doc));
            set("destinations", list);
            set("destinationNext", doc.object().value("next_href").toString());
            set("loadingDestinations", false);
        }, "GET", {}, true, [this, generation](QString error) {
            if (generation != m_playlistOperationGeneration) return;
            set("loadingDestinations", false); fail(error);
        });
}

void SoundCloud::saveToPlaylist(const QString &track, const QString &playlist, const QString &newTitle) {
    saveTracksToPlaylist({QVariantMap{{"urn", track}}}, playlist, newTitle, {});
}
void SoundCloud::saveTracksToPlaylist(const QVariantList &tracks, const QString &playlist,
                                    const QString &newTitle, const QString &token) {
    auto reject = [this, token](const QString &error) {
        if (token.isEmpty()) fail(error); else emit selectionSaved(token, false, error);
    };
    if (m_playlistWriteBusy) { reject("Дождитесь завершения записи плейлиста."); return; }
    if (!signedIn() || tracks.isEmpty()) { reject("Войдите в SoundCloud и выберите треки."); return; }
    if (playlist.isEmpty() ? newTitle.trimmed().isEmpty() || newTitle.trimmed().size() > 255
                           : !playlist.startsWith("soundcloud:playlists:")) {
        reject("Укажите плейлист или название длиной до 255 символов."); return;
    }
    if (tracks.size() > 500) { reject("В плейлисте может быть не более 500 треков."); return; }
    QJsonArray additions;
    for (const auto &value : tracks) {
        const auto id = urn(value.toMap());
        if (!id.startsWith("soundcloud:tracks:")) { reject("Выбор содержит недоступную запись."); return; }
        additions.append(QJsonObject{{"urn", id}});
    }
    m_playlistWriteBusy = true;
    const int epoch = m_playlistOperationGeneration;
    set("savingTrack", true);
    auto failed = [this, epoch, reject](QString error) {
        if (epoch != m_playlistOperationGeneration) return;
        m_playlistWriteBusy = false; set("savingTrack", false); reject(error);
    };
    auto done = [this, epoch, token](QJsonDocument) {
        if (epoch != m_playlistOperationGeneration) return;
        m_playlistWriteBusy = false; set("savingTrack", false); invalidatePlaylists();
        if (token.isEmpty()) emit trackSaved(); else emit selectionSaved(token, true, {});
    };
    if (playlist.isEmpty()) {
        QJsonObject album{{"title", newTitle.trimmed()}, {"sharing", "private"},
                          {"set_type", token.isEmpty() ? "album" : "playlist"}, {"tracks", additions}};
        request("/playlists", done, "POST", {{"playlist", album}}, true, failed);
        return;
    }
    fetchPlaylistSnapshot(playlist, [=, this](QVariantMap original) {
        validatePlaylistSnapshot(playlist, original, [=, this](QVariantMap latest) {
            QJsonArray updated;
            bool present = false;
            for (const auto &value : latest.value("tracks").toList()) {
                const auto id = urn(value.toMap());
                present |= token.isEmpty() && id == additions.first().toObject().value("urn").toString();
                updated.append(QJsonObject{{"urn", id}});
            }
            if (present) { done({}); return; }
            if (updated.size() + additions.size() > 500) {
                failed("В плейлисте может быть не более 500 треков. Изменения не сохранены."); return;
            }
            for (const auto &entry : additions) updated.append(entry);
            request(apiPath("playlists", playlist), done, "PUT", {{"playlist", QJsonObject{{"tracks", updated}}}}, true, failed);
        }, failed);
    }, failed);
}

void SoundCloud::cancelCollectionSearch() {
    m_collectionSearch = false;
    set("collectionSearching", false);
}
void SoundCloud::searchWholeCollection(bool playlist) {
    if (m_state.value(playlist ? "playlistBusy" : "busy").toBool() || (playlist ? m_playlistNext : m_nextHref).isEmpty()) return;
    m_searchPlaylist = playlist; m_searchPagesRemaining = 100;
    set("collectionSearchStatus", "loading");
    m_collectionSearch = true; set("collectionSearching", true);
    if (playlist) loadPlaylist(m_searchPlaylistItem, true); else more();
}
void SoundCloud::more() { if (!m_state.value("busy").toBool() && !m_nextHref.isEmpty()) collection(m_nextHref, true); }
void SoundCloud::loadPlaylist(const QVariantMap &item, bool append) {
    if (!append) { cancelCollectionSearch(); set("collectionSearchStatus", ""); m_searchPlaylistItem = item; m_playlistPages.clear(); }
    const auto id = urn(item);
    const auto previewType = item.value("preview_type").toString();
    const bool station = previewType == "station";
    const bool local = previewType == "mix";
    const bool system = id.startsWith("soundcloud:system-playlists:");
    if (!local && !(station ? id.startsWith("soundcloud:tracks:") : system || id.startsWith("soundcloud:playlists:"))) return;
    if (append && (m_state.value("playlistBusy").toBool() || m_playlistNext.isEmpty())) return;
    const int generation = ++m_playlistGeneration;
    const auto path = append ? m_playlistNext : station
        ? apiPath("tracks", id) + "/related?limit=40&linked_partitioning=true"
        : system ? apiPath("system-playlists", id) : apiPath("playlists", id) + "/tracks?limit=100&linked_partitioning=true";
    if (!append) {
        m_playlistNext.clear();
        set("playlistTracks", station ? QVariantList{item.value("seed").toMap()} : QVariantList{});
    }
    if (local) {
        set("playlistTracks", withLikeStates(item.value("tracks").toList()));
        set("playlistError", ""); set("playlistBusy", false); set("playlistHasMore", false);
        return;
    }
    set("playlistError", "");
    set("playlistBusy", true);
    set("playlistHasMore", false);
    request(path, [this, generation, append, path, station, system](QJsonDocument doc) {
        if (generation != m_playlistGeneration) return;
        if (!(system ? doc.object().value("tracks").isArray() : doc.isArray() || doc.object().value("collection").isArray())) {
            cancelCollectionSearch(); set("collectionSearchStatus", "error");
            set("playlistBusy", false); set("playlistHasMore", append && !m_playlistNext.isEmpty());
            set("playlistError", "Не удалось загрузить список полностью. Повторите позже."); return;
        }
        auto tracks = (append || station) ? m_state.value("playlistTracks").toList() : QVariantList{};
        QSet<QString> seen;
        for (const auto &track : tracks) seen.insert(urn(track.toMap()));
        for (const auto &track : system ? doc.object().value("tracks").toArray().toVariantList() : items(doc)) {
            const auto id = urn(track.toMap());
            if (!id.startsWith("soundcloud:tracks:") || ((station || system) && seen.contains(id))) continue;
            seen.insert(id); tracks.append(track);
        }
        const auto canonical = [](const QString &url) { return QUrl(url.startsWith('/') ? "https://api.soundcloud.com" + url : url).toString(); };
        m_playlistPages.insert(canonical(path));
        m_playlistNext = doc.object().value("next_href").toString();
        const QUrl next(m_playlistNext);
        const bool invalid = !m_playlistNext.isEmpty() && (m_playlistPages.contains(canonical(m_playlistNext))
            || next.scheme() != "https" || next.host() != "api.soundcloud.com" || !next.userInfo().isEmpty()
            || (next.port() != -1 && next.port() != 443) || paginationResourcePath(next) != paginationResourcePath(QUrl(canonical(path))));
        if (invalid) { m_playlistNext.clear(); cancelCollectionSearch(); set("collectionSearchStatus", "error"); }
        set("playlistTracks", withLikeStates(tracks));
        set("playlistHasMore", !m_playlistNext.isEmpty());
        set("playlistBusy", false);
        if (m_collectionSearch && m_searchPlaylist) {
            if (m_playlistNext.isEmpty()) { cancelCollectionSearch(); set("collectionSearchStatus", "complete"); }
            else if (--m_searchPagesRemaining <= 0) { cancelCollectionSearch(); set("collectionSearchStatus", "limit"); }
            else QTimer::singleShot(350, this, [this, generation] { if (m_collectionSearch && m_searchPlaylist && generation == m_playlistGeneration) loadPlaylist(m_searchPlaylistItem, true); });
        }
    }, "GET", {}, true, [this, generation](QString error) {
        if (generation != m_playlistGeneration) return;
        cancelCollectionSearch(); set("collectionSearchStatus", "error");
        set("playlistError", error);
        set("playlistHasMore", !m_playlistNext.isEmpty());
        set("playlistBusy", false);
    });
}
void SoundCloud::openPlaylist(const QVariantMap &item) {
    if (urn(item).isEmpty()) return;
    m_lastPath = apiPath("playlists", urn(item)) + "/tracks?linked_partitioning=true";
    set("heading", item.value("title")); collection(m_lastPath);
}
QString SoundCloud::enqueueTracks(const QVariantList &tracks) {
    if (tracks.isEmpty()) return "Выберите треки.";
    if (m_queue.size() + tracks.size() > 10000) return "В очереди может быть не более 10000 записей.";
    for (const auto &track : tracks)
        if (!urn(track.toMap()).startsWith("soundcloud:tracks:")) return "Выбор содержит недоступную запись.";
    const bool hasCurrent = m_queueIndex >= 0 && m_queueIndex < m_queue.size();
    int at = hasCurrent ? m_queueIndex + 1 : m_queue.size();
    syncQueueOrder();
    for (const auto &track : tracks) { m_queueIds.insert(at, m_nextQueueId++); auto manual = track.toMap(); manual.remove("scripAutomaticRecommendation"); manual["scripManualQueue"] = true; m_queue.insert(at++, manual); }
    m_plannedId = -1;
    if (hasCurrent) m_forceNext = true;
    set("queue", m_queue); savePlayback(); prepareNext();
    return {};
}
void SoundCloud::enqueue(const QVariantMap &item) {
    if (!urn(item).startsWith("soundcloud:tracks:")) return;
    auto manual = item; manual.remove("scripAutomaticRecommendation"); manual["scripManualQueue"] = true;
    syncQueueOrder(); m_plannedId = -1;
    if (m_queueIndex >= 0 && m_queueIndex < m_queue.size()) {
        m_queueIds.insert(m_queueIndex + 1, m_nextQueueId++);
        m_queue.insert(m_queueIndex + 1, manual);
        m_forceNext = true;
    } else {
        m_queue.append(manual); m_queueIds.append(m_nextQueueId++);
    }
    set("queue", m_queue); savePlayback();
    prepareNext();
}
void SoundCloud::play(const QVariantMap &item) {
    stopWave();
    if (item.value("kind").toString() == "playlist" || urn(item).startsWith("soundcloud:playlists:")) { openPlaylist(item); return; }
    m_queue.clear(); resetQueueOrder();
    for (const auto &entry : m_state.value("tracks").toList()) if (urn(entry.toMap()).startsWith("soundcloud:tracks:")) m_queue.append(entry);
    m_queueIndex = -1;
    for (int i=0; i<m_queue.size(); ++i) if (urn(m_queue[i].toMap()) == urn(item)) m_queueIndex = i;
    if (m_queueIndex < 0) { m_queue.prepend(item); m_queueIndex = 0; }
    set("queue", m_queue); set("queueIndex", m_queueIndex); startTrack(item);
}
void SoundCloud::playQueue(int index) { if (index >= 0 && index < m_queue.size()) { m_waveWaiting = false; m_wavePreferenceWaiting = false; syncQueueOrder(); m_queueIndex = index; set("queueIndex", index); startTrack(m_queue[index].toMap()); } }
void SoundCloud::moveQueue(int from, int to) {
    if (from < 0 || to < 0 || from >= m_queue.size() || to >= m_queue.size() || from == to) return;
    syncQueueOrder(); m_queueIds.move(from, to);
    m_queue.move(from, to);
    if (m_queueIndex == from) m_queueIndex = to;
    else if (from < m_queueIndex && to >= m_queueIndex) --m_queueIndex;
    else if (from > m_queueIndex && to <= m_queueIndex) ++m_queueIndex;
    m_state["queueIndex"] = m_queueIndex;
    m_state["queue"] = m_queue;
    emit changed();
    emit catalogChanged();
    savePlayback();
    prepareNext();
}
void SoundCloud::queueNext(int index) {
    if (index < 0 || index >= m_queue.size() || index == m_queueIndex) return;
    moveQueue(index, index < m_queueIndex ? m_queueIndex : m_queueIndex + 1);
    m_forceNext = true;
    prepareNext(); savePlayback();
}
void SoundCloud::rememberQueueEdit() {
    syncQueueOrder(); m_undoQueue = m_queue; m_undoOrder = queueOrderSnapshot();
    set("queueUndoAvailable", false);
}
void SoundCloud::undoQueueEdit() {
    if (!m_state.value("queueUndoAvailable").toBool() || m_queueIds != m_undoAfterIds) return;
    const int currentId = m_queueIds.value(m_queueIndex, -1);
    const auto queue = m_undoQueue;
    auto order = m_undoOrder;
    const auto liveOrder = queueOrderSnapshot();
    if (order.value("history").toList().value(order.value("cursor").toInt()).toInt() != currentId) {
        for (const auto &key : {"history", "cursor", "visited", "planned", "forced"}) order[key] = liveOrder.value(key);
    }
    const auto ids = order.value("ids").toList();
    int current = -1;
    for (int i = 0; i < ids.size(); ++i) if (ids[i].toInt() == currentId) current = i;
    if (currentId >= 0 && current < 0) return;
    m_queue = queue; m_queueIndex = current;
    restoreQueueOrder(order);
    finishQueueEdit();
    if (order.value("forced").toBool()) { m_forceNext = true; savePlayback(); prepareNext(); }
    set("queueUndoAvailable", false);
}
void SoundCloud::removeQueue(int index) {
    if (index < 0 || index >= m_queue.size()) return;
    rememberQueueEdit(); stopWave();
    const bool removingCurrent = index == m_queueIndex;
    const int successor = removingCurrent ? nextQueueIndex() : -1;
    const int successorId = successor >= 0 && successor != index ? m_queueIds.value(successor) : -1;
    syncQueueOrder(); m_queueIds.removeAt(index);
    m_queue.removeAt(index);
    m_undoAfterIds = m_queueIds; set("queueUndoAvailable", true);
    if (m_queue.isEmpty()) {
        m_queueIndex = -1;
        set("queue", m_queue); set("queueIndex", m_queueIndex);
        stopPlayback();
        prepareNext();
        return;
    }
    if (index < m_queueIndex) --m_queueIndex;
    else if (removingCurrent) m_queueIndex = successorId >= 0 ? m_queueIds.indexOf(successorId)
        : qMin(index, int(m_queue.size()) - 1);
    set("queue", m_queue); set("queueIndex", m_queueIndex);
    syncQueueOrder();
    if (removingCurrent) { startTrack(m_queue[m_queueIndex].toMap()); }
    else finishQueueEdit();
    if (removingCurrent) prepareNext();
}
bool SoundCloud::queueHasDuplicates() const {
    QSet<QString> seen;
    for (const auto &entry : m_queue) {
        const auto id = urn(entry.toMap());
        if (id.isEmpty()) continue;
        if (seen.contains(id)) return true;
        seen.insert(id);
    }
    return false;
}
void SoundCloud::finishQueueEdit() {
    // Freeze the edited wave snapshot; late recommendations must not undo the edit.
    stopWave();
    m_forceNext = false;
    if (m_crossfadeStarted) {
        m_crossfade.stop();
        m_preloadPlayer->stop(); m_preloadPlayer->setSource(QUrl{});
        m_preloadPlayer->setAudioOutput(nullptr);
        player().setAudioOutput(nullptr); player().setAudioOutput(&m_audio);
        m_preloadPlayer->setAudioOutput(&m_preloadAudio);
        m_audio.setVolume(m_state.value("volume").toReal()); m_preloadAudio.setVolume(0);
        m_crossfadeStarted = false;
    }
    m_state["queueIndex"] = m_queueIndex;
    set("queue", m_queue);
    savePlayback();
    prepareNext();
}
void SoundCloud::deduplicateQueue() {
    if (!queueHasDuplicates()) return;
    rememberQueueEdit();
    const QString current = m_queueIndex >= 0 && m_queueIndex < m_queue.size() ? urn(m_queue[m_queueIndex].toMap()) : QString{};
    syncQueueOrder();
    QList<int> ids;
    QSet<QString> seen;
    QVariantList result;
    int index = -1;
    for (int i = 0; i < m_queue.size(); ++i) {
        const auto id = urn(m_queue[i].toMap());
        if (!id.isEmpty() && ((id == current && i != m_queueIndex) || seen.contains(id))) continue;
        if (i == m_queueIndex) index = result.size();
        result.append(m_queue[i]); ids.append(m_queueIds[i]);
        if (!id.isEmpty()) seen.insert(id);
    }
    m_queue = result; m_queueIds = ids; m_queueIndex = index; syncQueueOrder();
    finishQueueEdit();
    m_undoAfterIds = m_queueIds; set("queueUndoAvailable", true);
}
void SoundCloud::clearQueueAfterCurrent() {
    if (m_queueIndex < 0 || m_queueIndex >= m_queue.size()) return;
    if (m_queueIndex + 1 >= m_queue.size()) { finishQueueEdit(); return; }
    rememberQueueEdit();
    syncQueueOrder(); m_queueIds = m_queueIds.mid(0, m_queueIndex + 1);
    m_queue = m_queue.mid(0, m_queueIndex + 1); syncQueueOrder();
    finishQueueEdit();
    m_undoAfterIds = m_queueIds; set("queueUndoAvailable", true);
}
void SoundCloud::saveQueueAsPlaylist(const QString &title) {
    if (!signedIn() || m_playlistWriteBusy || m_queue.isEmpty()) return;
    set("queuePlaylistError", "");
    if (title.trimmed().isEmpty() || title.trimmed().size() > 255) { set("queuePlaylistError", "Укажите название длиной до 255 символов."); return; }
    if (m_queue.size() > 500) { set("queuePlaylistError", "SoundCloud допускает не более 500 треков в плейлисте. Очередь не сохранена."); return; }
    QJsonArray tracks;
    for (const auto &entry : m_queue) {
        const auto id = urn(entry.toMap());
        if (!id.startsWith("soundcloud:tracks:")) { set("queuePlaylistError", "В очереди есть недоступная запись. Очередь не сохранена."); return; }
        tracks.append(QJsonObject{{"urn", id}});
    }
    const int generation = m_playlistOperationGeneration;
    m_playlistWriteBusy = true; set("queuePlaylistBusy", true);
    request("/playlists", [this, generation](QJsonDocument) {
        if (generation != m_playlistOperationGeneration) return;
        m_playlistWriteBusy = false; set("queuePlaylistBusy", false);
        invalidatePlaylists(); emit queuePlaylistSaved();
    }, "POST", {{"playlist", QJsonObject{{"title", title.trimmed()}, {"sharing", "private"}, {"set_type", "playlist"}, {"tracks", tracks}}}}, true,
    [this, generation](QString error) {
        if (generation != m_playlistOperationGeneration) return;
        m_playlistWriteBusy = false; set("queuePlaylistBusy", false); set("queuePlaylistError", error);
    });
}
// Queue row identities are independent of track URNs (duplicate rows are distinct).
void SoundCloud::resetQueueOrder() {
    m_undoQueue.clear(); m_undoAfterIds.clear(); set("queueUndoAvailable", false);
    m_queueIds.clear(); m_history.clear(); m_visited.clear();
    m_historyCursor = -1; m_plannedId = -1; m_followHistory = false;
    syncQueueOrder();
}
void SoundCloud::syncQueueOrder() {
    while (m_queueIds.size() < m_queue.size()) m_queueIds.append(m_nextQueueId++);
    while (m_queueIds.size() > m_queue.size()) m_queueIds.removeLast();
    const QSet<int> live(m_queueIds.begin(), m_queueIds.end());
    m_visited.intersect(live);
    for (int i = m_history.size() - 1; i >= 0; --i) {
        if (live.contains(m_history[i])) continue;
        m_history.removeAt(i);
        if (i <= m_historyCursor) --m_historyCursor;
    }
    if (!live.contains(m_plannedId)) m_plannedId = -1;
}
void SoundCloud::recordQueueEntry() {
    syncQueueOrder();
    if (m_queueIndex < 0 || m_queueIndex >= m_queueIds.size()) return;
    const int id = m_queueIds[m_queueIndex];
    const bool replay = m_followHistory;
    const bool transition = replay || m_historyCursor < 0 || m_history.value(m_historyCursor) != id;
    if (m_followHistory) m_followHistory = false;
    else if (m_historyCursor < 0 || m_history.value(m_historyCursor) != id) {
        m_history = m_history.mid(0, m_historyCursor + 1);
        m_history.append(id); m_historyCursor = m_history.size() - 1;
        if (m_history.size() > 10000) { m_history.removeFirst(); --m_historyCursor; }
    }
    if (!replay && transition && m_visited.size() == m_queueIds.size() && id == m_plannedId) m_visited.clear();
    m_visited.insert(id);
    if (transition) m_plannedId = -1;
}
int SoundCloud::nextQueueIndex(bool natural) {
    syncQueueOrder();
    if (m_queueIndex < 0 || m_queueIndex >= m_queue.size()) return -1;
    if (!m_waveMode.isEmpty()) return m_queueIndex + 1 < m_queue.size() ? m_queueIndex + 1 : -1;
    if (natural && m_state.value("repeatMode").toInt() == 1) return m_queueIndex;
    if (m_forceNext && m_queueIndex + 1 < m_queue.size()) return m_queueIndex + 1;
    if (m_historyCursor + 1 < m_history.size()) return m_queueIds.indexOf(m_history[m_historyCursor + 1]);
    const bool loop = m_state.value("repeatMode").toInt() == 2;
    if (!m_state.value("shuffle").toBool())
        return m_queueIndex + 1 < m_queue.size() ? m_queueIndex + 1 : loop ? 0 : -1;
    if (m_queueIndex >= 0) m_visited.insert(m_queueIds[m_queueIndex]);
    QList<int> candidates;
    for (int id : m_queueIds) if (!m_visited.contains(id)) candidates.append(id);
    if (candidates.isEmpty()) {
        if (!loop) return -1;
        candidates = m_queueIds;
        if (candidates.size() > 1) candidates.removeAll(m_queueIds[m_queueIndex]);
    }
    if (!candidates.contains(m_plannedId))
        m_plannedId = candidates[QRandomGenerator::global()->bounded(int(candidates.size()))];
    return m_queueIds.indexOf(m_plannedId);
}
QVariantMap SoundCloud::queueOrderSnapshot() const {
    const auto list = [](const auto &values) { QVariantList result; for (int id : values) result.append(id); return result; };
    return {{"ids", list(m_queueIds)}, {"history", list(m_history)}, {"cursor", m_historyCursor},
            {"visited", list(m_visited)}, {"planned", m_plannedId}, {"forced", m_forceNext}};
}
void SoundCloud::restoreQueueOrder(const QVariantMap &snapshot) {
    resetQueueOrder();
    const auto ids = snapshot.value("ids").toList();
    if (ids.size() != m_queue.size() || ids.size() > 10000) { recordQueueEntry(); return; }
    QList<int> restored;
    QSet<int> unique;
    for (const auto &value : ids) {
        bool ok = false; const int id = value.toInt(&ok);
        if (!ok || id <= 0 || id >= 1000000000 || unique.contains(id)) { recordQueueEntry(); return; }
        unique.insert(id); restored.append(id);
    }
    m_queueIds = restored;
    for (int id : restored) m_nextQueueId = qMax(m_nextQueueId, id + 1);
    const auto history = snapshot.value("history").toList();
    if (history.size() <= 10000) for (const auto &value : history)
        if (unique.contains(value.toInt())) m_history.append(value.toInt());
    m_historyCursor = qBound(-1, snapshot.value("cursor", -1).toInt(), int(m_history.size()) - 1);
    for (const auto &value : snapshot.value("visited").toList())
        if (unique.contains(value.toInt())) m_visited.insert(value.toInt());
    m_plannedId = snapshot.value("planned", -1).toInt();
    if (!unique.contains(m_plannedId)) m_plannedId = -1;
    m_forceNext = snapshot.value("forced").toBool();
    if (m_historyCursor < 0 || m_history[m_historyCursor] != m_queueIds.value(m_queueIndex)) recordQueueEntry();
}

void SoundCloud::next() { advancePlayback(false); }
void SoundCloud::advancePlayback(bool natural) {
    if (natural && m_waveMode.isEmpty() && m_state.value("repeatMode").toInt() == 1) {
        startTrack(m_state.value("current").toMap()); return;
    }
    if (!m_waveMode.isEmpty() && m_queueIndex + 1 >= m_queue.size()) { m_waveWaiting = true; set("playbackActive", true); refillWave(); return; }
    if (m_queue.isEmpty()) { m_queueIndex = -1; set("queueIndex", -1); stopPlayback(); return; }
    const int index = nextQueueIndex();
    if (index >= 0) {
        m_followHistory = !m_forceNext && m_historyCursor + 1 < m_history.size()
            && m_queueIds.value(index) == m_history.value(m_historyCursor + 1);
        if (m_followHistory) ++m_historyCursor;
        playQueue(index);
    } else {
        pausePlayback();
        if (m_crossfadeStarted) finishCrossfade();
        player().stop(); set("playing", false); savePlayback();
    }
}
void SoundCloud::previous() {
    if (player().position() > 3000) { seek(0); return; }
    if (!m_waveMode.isEmpty()) { if (m_queueIndex > 0) playQueue(m_queueIndex - 1); return; }
    syncQueueOrder();
    if (m_historyCursor > 0) {
        --m_historyCursor; m_followHistory = true;
        playQueue(m_queueIds.indexOf(m_history[m_historyCursor]));
    }
}
void SoundCloud::togglePlayback() {
    if (m_state.value("playbackActive").toBool()) pausePlayback(); else resumePlayback();
}
void SoundCloud::pausePlayback() {
    m_playbackRecoveryTimer.stop(); m_playbackRecovering = false;
    set("buffering", false);
    m_waveWaiting = false; m_wavePreferenceWaiting = false;
    if (m_waveRetryTimer.isActive()) { m_waveRetryTimer.stop(); set("waveBusy", false); }
    set("playbackActive", false);
    if (m_crossfadeStarted) { m_crossfade.pause(); m_preloadPlayer->pause(); }
    player().pause();
    savePlayback();
}
void SoundCloud::resumePlayback() {
    if (player().error() != QMediaPlayer::NoError && !m_state.value("current").toMap().isEmpty()) {
        startTrack(m_state.value("current").toMap(), true, m_state.value("position").toLongLong());
        return;
    }
    if (!m_waveMode.isEmpty() && (m_queueIndex < 0 || player().mediaStatus() == QMediaPlayer::EndOfMedia)) {
        set("playbackActive", true);
        if (m_queueIndex + 1 < m_queue.size()) playQueue(m_queueIndex + 1);
        else { m_waveWaiting = true; refillWave(); }
        return;
    }
    if (player().source().isEmpty()) {
        if (m_state.value("buffering").toBool()) set("playbackActive", true);
        else if (m_queueIndex >= 0 && m_queueIndex < m_queue.size()) {
            const qint64 saved = m_state.value("position").toLongLong();
            startTrack(m_queue[m_queueIndex].toMap(), saved <= 0, saved);
            if (saved > 0 && m_restoringPlayback && m_state.value("buffering").toBool()) set("playbackActive", true);
        }
        return;
    }
    set("playbackActive", true);
    player().play();
    if (m_crossfadeStarted) { m_preloadPlayer->play(); m_crossfade.resume(); }
}
void SoundCloud::stopPlayback() {
    m_playbackRecoveryTimer.stop(); m_playbackRecovering = false;
    m_streamCache.suspend();
    cancelPreload();
    m_pendingSeek = -1;
    stopWave();
    m_androidRestorePending = false; m_androidRestoreTrack.clear(); m_restoringPlayback = false;
    ++m_playGeneration; ++m_preloadGeneration;
    m_startupPending = m_startupAudible = false;
    m_startupSourceSet = m_startupLoadedLogged = m_startupUsedPreload = m_preloadedRetryUsed = false;
    m_streamCandidates.clear(); m_streamCandidateIndex = -1; m_previewStreamAvailable = false;
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
    savePlayback();
}
void SoundCloud::seek(qint64 position) {
    if (m_state.value("current").toMap().isEmpty() && player().source().isEmpty()) return;
    const qint64 duration = this->duration();
    const qint64 target = duration > 0 ? qBound(qint64(0), position, duration) : qMax(qint64(0), position);
    m_pendingSeek = target;
    set("position", target);
    applyPendingSeek();
}
void SoundCloud::applyPendingSeek() {
    if (m_pendingSeek < 0 || !player().isSeekable()) return;
    const auto status = player().mediaStatus();
    if (status == QMediaPlayer::NoMedia || status == QMediaPlayer::LoadingMedia || status == QMediaPlayer::InvalidMedia) return;
    const qint64 target = player().duration() > 0 ? qMin(m_pendingSeek, player().duration()) : m_pendingSeek;
    m_pendingSeek = -1;
    m_restorePosition = target;
    m_restoringPlayback = false;
    m_playerEffects.clear(); m_spareEffects.clear();
    player().setPosition(target);
    set("position", player().position());
    if (m_state.value("playbackActive").toBool()) player().play();
    savePlaybackPosition();
    emit playbackSeeked();
}
void SoundCloud::volume(double value) {
    const double target = qBound(0.0, value, 1.0);
    if (target == m_state.value("volume").toDouble()) return;
    if (m_crossfadeStarted) {
        const qreal progress = m_crossfade.currentValue().toReal();
        m_audio.setVolume(target * (1.0 - progress));
        m_preloadAudio.setVolume(target * progress);
        set("volume", target); m_volumeSaveTimer.start(); return;
    }
    m_volumeFade.stop();
    m_volumeFade.setStartValue(double(m_audio.volume()));
    m_volumeFade.setEndValue(target);
    m_volumeFade.start();
    set("volume", target);
    m_volumeSaveTimer.start();
}
void SoundCloud::setRepeat(bool enabled) { setRepeatMode(enabled ? 1 : 0); }
void SoundCloud::setRepeatMode(int mode) {
    if (mode < 0 || mode > 2) return;
    m_state["repeat"] = mode == 1;
    set("repeatMode", mode);
    QSettings().setValue("player/repeatMode", mode);
    prepareNext(); savePlayback();
}
void SoundCloud::setShuffle(bool enabled) {
    if (m_state.value("shuffle").toBool() != enabled) {
        syncQueueOrder(); m_visited.clear();
        if (m_queueIndex >= 0) m_visited.insert(m_queueIds.value(m_queueIndex));
        m_plannedId = -1;
    }
    set("shuffle", enabled); QSettings().setValue("player/shuffle", enabled);
    prepareNext(); savePlayback();
}

QList<QPair<QString, QUrl>> SoundCloud::fullStreams(const QJsonObject &object) const {
    QList<QPair<QString, QUrl>> result;
    const QStringList keys = m_state.value("playbackQuality").toInt() == 1
        ? QStringList{"hls_aac_96_url", "hls_mp3_128_url", "hls_aac_160_url"}
        : QStringList{"hls_aac_160_url", "hls_aac_96_url", "hls_mp3_128_url"};
    for (const auto &key : keys) {
        const QUrl url(object.value(key).toString());
        if (!url.isEmpty()) result.append({key, url});
    }
    return result;
}
bool SoundCloud::preloadAllowed() const {
    if (!m_state.value("preloadEnabled", true).toBool()) return false;
#ifdef Q_OS_ANDROID
    if (m_state.value("preloadWifiOnly").toBool()) {
        auto *info = QNetworkInformation::instance();
        return info && info->supports(QNetworkInformation::Feature::TransportMedium)
            && info->transportMedium() == QNetworkInformation::TransportMedium::WiFi;
    }
#endif
    return true;
}
void SoundCloud::cancelPreload() {
    ++m_preloadGeneration;
    if (m_preloadReply) { auto reply = m_preloadReply; m_preloadReply.clear(); reply->abort(); }
    const auto source = m_preloadPlayer->source();
    m_preloadPlayer->stop(); m_preloadPlayer->setSource({});
    if (source != player().source()) m_streamCache.discard(source);
    m_preloadUrn.clear(); m_preloadIndex = -1; m_preloadCandidates.clear();
}
void SoundCloud::updatePreloadPolicy() {
    if (m_crossfadeStarted) finishCrossfade(false);
    m_streamCache.setBackgroundEnabled(preloadAllowed());
    prepareNext();
    if (preloadAllowed() && m_state.value("playing").toBool()) m_streamCache.warm(player().source());
}
void SoundCloud::setPlaybackQuality(int quality) {
    quality = qBound(0, quality, 1);
    if (quality == m_state.value("playbackQuality").toInt()) return;
    set("playbackQuality", quality); QSettings().setValue("player/playbackQuality", quality);
    updatePreloadPolicy();
}
void SoundCloud::setPreloadEnabled(bool enabled) {
    set("preloadEnabled", enabled); QSettings().setValue("player/preloadEnabled", enabled);
    updatePreloadPolicy();
}
void SoundCloud::setPreloadWifiOnly(bool enabled) {
    set("preloadWifiOnly", enabled); QSettings().setValue("player/preloadWifiOnly", enabled);
    updatePreloadPolicy();
}
bool SoundCloud::canResumePlayback() const {
    return !m_state.value("current").toMap().isEmpty() || (m_queueIndex >= 0 && m_queueIndex < m_queue.size()) || !m_waveMode.isEmpty();
}
bool SoundCloud::canGoNext() const {
    if (!m_waveMode.isEmpty()) return true;
    if (m_queueIndex < 0 || m_queueIndex >= m_queue.size()) return false;
    if ((m_forceNext && m_queueIndex + 1 < m_queue.size()) || m_historyCursor + 1 < m_history.size()) return true;
    if (m_state.value("repeatMode").toInt() == 2) return true;
    if (!m_state.value("shuffle").toBool()) return m_queueIndex + 1 < m_queue.size();
    for (int i = 0; i < m_queueIds.size(); ++i)
        if (i != m_queueIndex && !m_visited.contains(m_queueIds[i])) return true;
    return false;
}
bool SoundCloud::canGoPrevious() const {
    return !m_state.value("current").toMap().isEmpty()
        && (position() > 3000 || (!m_waveMode.isEmpty() ? m_queueIndex > 0 : m_historyCursor > 0));
}

void SoundCloud::setCrossfade(bool enabled) {
    set("crossfade", enabled);
    QSettings().setValue("player/crossfade", enabled);
}
void SoundCloud::startTrack(const QVariantMap &track, bool autoplay, qint64 position, bool recovery) {
    recordQueueEntry();
    m_pendingSeek = -1;
    m_forceNext = false;
    if (position <= 0) m_snapshotPlayback = false;
    if (!urn(track).startsWith("soundcloud:tracks:")) return;
    m_playbackRecoveryTimer.stop(); m_playbackRecovering = false;
    m_playbackIdleSeconds = 0; m_watchdogPosition = -1;
    if (!recovery) { m_preloadedRetryUsed = false; m_playbackRetries = 0; }
    m_startupClock.start(); m_lastStartupMs = 0; m_startupPending = true; m_startupAudible = false;
    m_startupInitialPosition = qMax<qint64>(0, position);
    m_startupSourceSet = false; m_startupLoadedLogged = false; m_startupUsedPreload = false;
    m_streamCandidates.clear(); m_streamCandidateIndex = -1; m_previewStreamAvailable = false;
    m_androidRestorePending = false; m_androidRestoreTrack.clear();
    m_restoringPlayback = !autoplay || position > 0;
    m_restorePosition = qMax<qint64>(0, position);
    set("restoreCleared", false); set("playbackActive", autoplay);
    m_streamCache.suspend();
    clearError(); ++m_playGeneration; startupStage("selection");
    if (m_crossfadeStarted) {
        m_crossfade.stop();
        m_preloadPlayer->stop(); m_preloadPlayer->setSource(QUrl{});
        player().setAudioOutput(nullptr); player().setAudioOutput(&m_audio);
        m_preloadPlayer->setAudioOutput(nullptr); m_preloadPlayer->setAudioOutput(&m_preloadAudio);
        m_audio.setVolume(m_state.value("volume").toReal()); m_preloadAudio.setVolume(0); m_crossfadeStarted = false;
    }
    const bool prepared = urn(track) == m_preloadUrn && !m_preloadPlayer->source().isEmpty() && m_preloadPlayer->error() == QMediaPlayer::NoError;
    if (!prepared) cancelPreload();
    m_startupUsedPreload = prepared;
    if (prepared) {
        const auto status = m_preloadPlayer->mediaStatus();
        startupStage(status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia
            ? "preloaded_ready_switch" : "preloaded_loading_switch");
    }
    ++m_preloadGeneration;
    player().stop(); player().setSource(QUrl{});
    if (prepared) {
        startupStage("preloaded_source");
        player().setAudioOutput(nullptr);
        m_preloadPlayer->setAudioOutput(nullptr);
        std::swap(m_activePlayer, m_preloadPlayer);
        m_startupSourceSet = true;
        player().setAudioOutput(&m_audio);
        m_preloadPlayer->setAudioOutput(&m_preloadAudio);
    }
    m_preloadUrn.clear(); m_preloadIndex = -1;
    set("current", track); set("position", position); set("duration", track.value("duration")); set("buffering", true);
    savePlayback();
    set("playbackAccess", track.value("access").toString());
    if (track.value("access").toString() == "preview") { set("playbackActive", false); set("buffering", false); failAndroidRestore(); fail("Доступен только короткий фрагмент. Откройте трек на SoundCloud."); return; }
    if (track.value("access").toString() == "blocked" || track.value("streamable") == false) { set("playbackActive", false); set("buffering", false); failAndroidRestore(); fail("Этот трек недоступен для воспроизведения через API."); return; }
    if (prepared) {
        set("buffering", player().mediaStatus() != QMediaPlayer::BufferedMedia && player().mediaStatus() != QMediaPlayer::LoadedMedia);
        if (m_restoringPlayback && (player().mediaStatus() == QMediaPlayer::LoadedMedia || player().mediaStatus() == QMediaPlayer::BufferedMedia)) {
            if (m_restorePosition > 0) player().setPosition(player().duration() > 0 ? qMin(m_restorePosition, player().duration()) : m_restorePosition);
            m_restoringPlayback = false;
            set("position", player().position());
        } else if (player().position() > 0) player().setPosition(0);
        if (m_state.value("playbackActive").toBool() && !m_restoringPlayback) player().play();
        prepareNext(); return;
    }
    const int generation = m_playGeneration; set("buffering", true);
    startupStage("streams_token_wait");
    request(apiPath("tracks", urn(track)) + "/streams", [this, generation](QJsonDocument doc) {
        if (generation != m_playGeneration) return;
        startupStage("streams_response");
        const auto object = doc.object();
        m_previewStreamAvailable = !object.value("preview_mp3_128_url").toString().isEmpty();
        m_streamCandidates = fullStreams(object);
        if (m_streamCandidates.isEmpty()) {
            m_startupPending = false; set("playbackActive", false); set("buffering", false); failAndroidRestore();
            fail(m_previewStreamAvailable
                ? "SoundCloud предоставил только короткий фрагмент трека. Полная версия сейчас недоступна."
                : "SoundCloud не предоставил доступный аудиопоток.");
            return;
        }
        nextStream(generation, "initial");
    }, "GET", {}, true, [this, generation](QString message) {
        if (generation != m_playGeneration) return;
        Q_UNUSED(message);
        startupStage("streams_error"); recoverPlayback();
    }, [this, generation] {
        if (generation == m_playGeneration) startupStage("streams_http_start");
    });
}
// Recovery probes never carry OAuth or signed media URLs. An HTTP response proves
// connectivity; transport failures and service outages keep the current track.
void SoundCloud::recoverPlayback() {
    if (m_playbackRecovering) return;
    if (!m_state.value("playbackActive").toBool()) {
        m_startupPending = false; set("buffering", false); failAndroidRestore();
        return;
    }
    m_recoveryPosition = m_pendingSeek >= 0 ? m_pendingSeek :
        qMax(m_state.value("position").toLongLong(), player().position());
    m_playbackRecovering = true;
    ++m_playGeneration; // Ignore requests belonging to the broken source.
    m_startupPending = false;
    player().stop();
    set("position", m_recoveryPosition);
    set("buffering", true);
    m_playbackRecoveryTimer.start(1500);
}
void SoundCloud::checkPlaybackConnection() {
    if (!m_playbackRecovering || !m_state.value("playbackActive").toBool()) return;
    const int generation = m_playGeneration;
    QNetworkRequest request(QUrl("https://api.soundcloud.com/"));
    request.setTransferTimeout(10000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_playGeneration || !m_playbackRecovering ||
            !m_state.value("playbackActive").toBool()) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0 || status == 429 || status >= 500) {
            m_playbackRecoveryTimer.start(status == 429 ? 60000 : 10000);
            return;
        }
        m_playbackRecovering = false;
        if (++m_playbackRetries <= 2) {
            startTrack(m_state.value("current").toMap(), true, m_recoveryPosition, true);
            return;
        }
        const auto id = urn(m_state.value("current").toMap());
        if (!m_failedPlaybackTracks.contains(id) && canGoNext()) {
            m_failedPlaybackTracks.insert(id);
            advancePlayback(false); // A broken track must not obey repeat-one.
        } else {
            pausePlayback();
            fail("Не удалось восстановить воспроизведение. Попробуйте другой трек или повторите позже.");
        }
    });
}
void SoundCloud::startupStage(const char *stage) {
    if (!m_startupClock.isValid()) return;
    const qint64 elapsed = m_startupClock.elapsed();
    // Только номер попытки, этап и длительность: URL, OAuth и идентификатор трека исключены.
    qInfo().noquote() << "SCripStartup" << m_playGeneration << stage
                      << "delta_ms=" << elapsed - m_lastStartupMs << "total_ms=" << elapsed;
    m_lastStartupMs = elapsed;
}
void SoundCloud::nextStream(int generation, const char *reason) {
    if (generation != m_playGeneration) return;
    m_startupAudible = false;
    ++m_streamCandidateIndex;
    if (m_streamCandidateIndex >= m_streamCandidates.size()) {
        startupStage("streams_exhausted"); recoverPlayback();
        return;
    }
    qInfo().noquote() << "SCripStartup" << generation << "candidate=" << m_streamCandidates[m_streamCandidateIndex].first << "reason=" << reason;
    resolveStream(m_streamCandidates[m_streamCandidateIndex].second, generation);
}
void SoundCloud::resolveStream(const QUrl &url, int generation, int redirects) {
    if (generation != m_playGeneration) return;
    if (url.scheme() != "https" || redirects > 5 || !url.userInfo().isEmpty()) { startupStage("invalid_stream_address"); nextStream(generation, "invalid_address"); return; }
    // CDN SoundCloud выдаёт подписанные URL. OAuth передаём только API, никогда стороннему хосту.
    if (url.host() != "api.soundcloud.com") { startupStage("cdn_source"); m_startupSourceSet = true; player().setSource(m_streamCache.source(url)); if (m_state.value("playbackActive").toBool() && !m_restoringPlayback) player().play(); prepareNext(); return; }
    const auto wait = rateDeadline("streams") - m_requestNow();
    if (wait > 0) {
        deferRequest(wait, [=, this] { resolveStream(url, generation, redirects); });
        return;
    }
    QNetworkRequest req(url);
    req.setPriority(QNetworkRequest::HighPriority); req.setRawHeader("Authorization", "OAuth " + m_access.toUtf8()); req.setTransferTimeout(20000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_network->get(req);
    connect(reply, &QNetworkReply::finished, this, [=, this] {
        reply->deleteLater(); if (generation != m_playGeneration) return;
        observeRate(reply, "streams");
        startupStage("stream_resolve_response");
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429) { m_startupPending = false; set("playbackActive", false); set("buffering", false); failAndroidRestore(); fail("SoundCloud ограничил запросы. Повторите позже."); return; }
        if (reply->error() != QNetworkReply::NoError || status >= 400) { nextStream(generation, "resolve_error"); return; }
        auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!redirect.isEmpty()) { resolveStream(url.resolved(redirect), generation, redirects + 1); return; }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto nextUrl = doc.object().value("url").toString();
        if (!nextUrl.isEmpty()) { resolveStream(QUrl(nextUrl), generation, redirects + 1); return; }
        nextStream(generation, "missing_url");
    });
}
void SoundCloud::loadFollowings() {
    if (!signedIn() || m_state.value("followingsReady").toBool() || m_state.value("followingsLoading").toBool()) return;
    m_followPages.clear();
    set("followings", QVariantMap{});
    set("followingsLoading", true);
    loadFollowingPage("/me/followings?limit=200&linked_partitioning=true");
}
void SoundCloud::loadFollowingPage(const QString &path) {
    if (m_followPages.contains(path)) {
        set("followingsLoading", false);
        fail("SoundCloud вернул повторную страницу подписок. Повторите попытку.");
        return;
    }
    m_followPages.insert(path);
    request(path, [this](QJsonDocument doc) {
        auto following = m_state.value("followings").toMap();
        for (const auto &entry : items(doc)) {
            const auto id = urn(entry.toMap());
            if (id.startsWith("soundcloud:users:")) following.insert(id, true);
        }
        set("followings", following);
        const auto next = doc.object().value("next_href").toString();
        if (!next.isEmpty()) { loadFollowingPage(next); return; }
        set("followingsReady", true);
        set("followingsLoading", false);
    }, "GET", {}, true, [this](QString message) {
        set("followingsLoading", false); fail(message);
    });
}
void SoundCloud::toggleFollow(const QVariantMap &user) {
    if (!signedIn()) { fail("Войдите в SoundCloud, чтобы подписаться на исполнителя."); return; }
    const auto id = urn(user);
    if (!id.startsWith("soundcloud:users:") || id == urn(m_state.value("profile").toMap())) return;
    if (!m_state.value("followingsReady").toBool()) { loadFollowings(); return; }
    auto pending = m_state.value("followingPending").toMap();
    if (pending.value(id).toBool()) return;
    const bool followed = m_state.value("followings").toMap().value(id).toBool();
    pending.insert(id, true); set("followingPending", pending);
    request(apiPath("me/followings", id), [this, id, followed](QJsonDocument) {
        auto following = m_state.value("followings").toMap();
        following.insert(id, !followed); set("followings", following);
        auto pending = m_state.value("followingPending").toMap();
        pending.remove(id); set("followingPending", pending);
        auto profile = m_state.value("profile").toMap();
        profile["followings_count"] = qMax(0, profile.value("followings_count").toInt() + (followed ? -1 : 1));
        set("profile", profile);
        auto visited = m_state.value("visitedProfile").toMap();
        if (urn(visited) == id) {
            visited["followers_count"] = qMax(0, visited.value("followers_count").toInt() + (followed ? -1 : 1));
            set("visitedProfile", visited);
        }
        m_listings.remove("/me/feed/tracks?limit=30&linked_partitioning=true");
        m_homeLoaded = false;
    }, followed ? "DELETE" : "PUT", {}, true, [this, id](QString message) {
        auto pending = m_state.value("followingPending").toMap();
        pending.remove(id); set("followingPending", pending); fail(message);
    });
}
void SoundCloud::like(const QVariantMap &item) {
    if (urn(item).startsWith("soundcloud:playlists:") || urn(item).startsWith("soundcloud:system-playlists:")) { likeCollection(item); return; }
    if (!m_state.value("signedIn").toBool()) { fail("Войдите в аккаунт, чтобы менять избранное."); return; }
    const auto id = urn(item); if (!id.startsWith("soundcloud:tracks:")) return; const bool liked = withLikeState(item).value("user_favorite").toBool();
    auto pending = m_state.value("likePending").toMap();
    if (pending.value(id).toBool()) return;
    pending[id] = true; set("likePending", pending);
    m_likeOverrides.insert(id, liked);
    const int generation = m_likeGeneration;
    ++m_likeRevision;
    request(apiPath("likes/tracks", id), [this, id, liked, generation, item](QJsonDocument) {
        if (generation != m_likeGeneration) return;
        auto pending = m_state.value("likePending").toMap(); pending.remove(id); set("likePending", pending);
        confirmEngagementCount(item, false, liked);
        m_likeOverrides.insert(id, !liked);
        if (liked) m_likedTracks.remove(id); else m_likedTracks.insert(id);
        refreshLikeStates();
        m_firstLikes = {};
        m_listings.remove("/me/likes/tracks?limit=30&linked_partitioning=true");
        savePlayback();
    }, liked ? "DELETE" : "POST", {}, true, [this, id, generation](QString message) {
        if (generation != m_likeGeneration) return;
        auto pending = m_state.value("likePending").toMap(); pending.remove(id); set("likePending", pending);
        refreshLikeStates(); fail(message);
    });
}
void SoundCloud::createPlaylist(const QString &title, bool includeCurrent) {
    if (!m_state.value("signedIn").toBool()) { fail("Сначала войдите в SoundCloud."); return; }
    if (title.trimmed().isEmpty()) { fail("Укажите название плейлиста."); return; }
    QJsonObject playlist{{"title", title.trimmed()}, {"sharing", "private"}};
    if (includeCurrent && !m_state.value("current").toMap().isEmpty()) playlist["tracks"] = QJsonArray{QJsonObject{{"urn", urn(m_state.value("current").toMap())}}};
    request("/playlists", [this](QJsonDocument) { browse("playlists", {}, true); }, "POST", {{"playlist", playlist}});
}
void SoundCloud::openExternal(const QString &value) {
    QUrl url(value);
    const bool soundcloud = url.host() == "soundcloud.com" || url.host().endsWith(".soundcloud.com");
    const bool updates = url.host() == "github.com" && url.path() == "/sykia/sange/releases/latest";
    if (url.scheme() == "https" && (soundcloud || updates)) QDesktopServices::openUrl(url);
}
void SoundCloud::copyText(const QString &text) { QGuiApplication::clipboard()->setText(text); }

void SoundCloud::checkForUpdates(bool manual) {
    if (m_state.value("updateChecking").toBool() || m_state.value("updateDownloading").toBool() || m_state.value("updateInstalling").toBool()) return;
    set("updateError", ""); set("updateManual", manual); set("updateChecking", true);
    set("updateAvailable", false); set("updateResult", "Проверяем обновления…");
    QNetworkRequest req(QUrl("https://api.github.com/repos/sykia/sange/releases/latest"));
    req.setRawHeader("Accept", "application/vnd.github+json"); req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setRawHeader("User-Agent", "Sange-update-checker"); req.setTransferTimeout(15000);
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
        set("updateUrl", "https://github.com/sykia/sange/releases/latest");
        if (QVersionNumber::compare(latest, current) <= 0) { set("updateResult", "Установлена последняя версия"); return; }
        QString suffix;
#ifdef Q_OS_ANDROID
        suffix = "-Android-universal.apk";
#elif defined(Q_OS_WIN)
        suffix = "-Windows-x64.exe";
#elif defined(Q_OS_MACOS)
        suffix = "-macOS-" + QSysInfo::buildCpuArchitecture() + ".dmg";
#elif defined(Q_OS_LINUX)
        if (QFileInfo::exists("/etc/arch-release")) suffix = "-x86_64.pkg.tar.zst";
        else if (QFileInfo::exists("/etc/debian_version")) suffix = "-amd64.deb";
        else { updateFailure("Автообновление поддерживает Arch Linux и Debian."); return; }
#else
        updateFailure("Обновление для этой платформы не поддерживается."); return;
#endif
        bool matchingAsset = false;
        for (const auto &entry : release.value("assets").toArray()) {
            const auto asset = entry.toObject(); const QString name = asset.value("name").toString();
            if (name.endsWith(suffix)) matchingAsset = true;
            const QUrl url(asset.value("browser_download_url").toString());
            const QString digest = asset.value("digest").toString();
            const auto sizeValue = asset.value("size");
            const double size = sizeValue.toDouble(-1);
            const QString lowerDigest = digest.toLower();
            if (name.isEmpty() || QFileInfo(name).fileName() != name || name == "." || name == ".." || !name.endsWith(suffix) || sizeValue.isNull() || size <= 0 || size > 1024.0*1024*1024 || qFloor(size) != size ||
                !lowerDigest.startsWith("sha256:") || lowerDigest.size() != 71 ||
                !QRegularExpression("^sha256:[0-9a-f]{64}$").match(lowerDigest).hasMatch() ||
                url.scheme() != "https" || url.host() != "github.com" || url.port() != -1 || !url.userInfo().isEmpty() ||
                !url.path().startsWith("/sykia/sange/releases/download/")) continue;
            if (sameReadyRelease && (m_updateReadyName != name || m_updateReadyDigest != lowerDigest || m_updateExpectedSize != qint64(size))) {
                m_updatePath.clear(); m_updateReadyVersion.clear(); m_updateReadyName.clear(); m_updateReadyDigest.clear();
                set("updateReady", false); sameReadyRelease = false;
            }
            m_updateAsset = asset; m_updateDigest = lowerDigest; m_updateExpectedSize = qint64(size); m_updateVersion = remote;
            set("updateResult", "Доступна версия " + remote); set("updateError", ""); return;
        }
        if (matchingAsset) {
            updateFailure("Не удалось проверить файл обновления для этой системы.");
        } else {
            set("updateAvailable", false); set("updateReady", false);
            m_updateAsset = {}; m_updateDigest.clear(); m_updateVersion.clear(); m_updateExpectedSize = -1;
            set("updateError", ""); set("updateResult", "Обновление для этой системы пока недоступно");
        }
    });
}

void SoundCloud::downloadUpdate() {
    if (!m_state.value("updateAvailable").toBool() || m_updateReply || m_state.value("updateDownloading").toBool() || m_state.value("updateInstalling").toBool() || m_updateAsset.isEmpty()) return;
    const QUrl url(m_updateAsset.value("browser_download_url").toString());
    if (url.scheme() != "https" || url.host() != "github.com" || url.port() != -1 || !url.userInfo().isEmpty() || !url.path().startsWith("/sykia/sange/releases/download/") || m_updateVersion != m_state.value("latestVersion").toString()) { set("updateError", "Источник или версия файла обновления не разрешены."); return; }
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
    m_updateReceived = 0; set("updateDownloaded", 0); set("updateTotal", m_updateExpectedSize); set("updateReady", false); set("updateError", ""); set("updateDownloading", true); set("updateResult", "Скачивание обновления…");
    QNetworkRequest req(url); req.setTransferTimeout(60000);
    // Скачиваем исходный пакет: размер и SHA-256 в релизе относятся к его байтам.
    req.setRawHeader("Accept-Encoding", "identity");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_updateReply = m_network->get(req);
    connect(m_updateReply, &QNetworkReply::metaDataChanged, this, [this] {
        if (!m_updateReply) return;
        const QUrl finalUrl = m_updateReply->url();
        if (finalUrl.scheme() != "https" || finalUrl.port() != -1 || !finalUrl.userInfo().isEmpty() ||
            !(finalUrl.host() == "github.com" || finalUrl.host().endsWith(".githubusercontent.com"))) {
            set("updateError", "Сервер перенаправил загрузку на запрещённый адрес."); cancelUpdateDownload(); return;
        }
        // Размер промежуточного ответа перенаправления не относится к файлу обновления.
        // Само перенаправление проверяется политикой Qt; адрес и размер итогового
        // ответа проверяем при получении его метаданных.
        const int status = m_updateReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) return;
        const auto length = m_updateReply->header(QNetworkRequest::ContentLengthHeader);
        if (length.isValid()) {
            bool valid = false; const qint64 n = length.toString().toLongLong(&valid);
            // Qt обозначает неизвестный размер значением -1 (например, при chunked).
            // В этом случае проверяем реальные байты и SHA-256 после загрузки.
            if (!valid || n < -1 || n == 0 || n > 1024LL*1024*1024 || (n > 0 && m_updateExpectedSize >= 0 && n != m_updateExpectedSize)) {
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
        const bool ok = m_state.value("updateError").toString().isEmpty() && reply && reply->error() == QNetworkReply::NoError && status == 200 && m_updateExpectedSize > 0 && m_updateReceived == m_updateExpectedSize &&
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
    if (result == 1) { set("updateResult", "Разрешите установку Sange в настройках Android, затем нажмите «Установить сейчас» ещё раз."); set("updateError", "Нужно разрешить установку приложений из этого источника."); }
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
            set("updateResult", "Мастер установки открыт. Завершите его; перезапустите Sange после установки или отмены.");
        }
    }
#elif defined(Q_OS_MACOS)
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(m_updatePath));
    set("updateInstalling", false);
    set("updateError", opened ? QString{} : QStringLiteral("Не удалось открыть образ обновления macOS."));
    set("updateResult", opened ? QStringLiteral("Образ открыт. Завершите Sange и перенесите новую версию в Applications, затем запустите её.")
                                : QStringLiteral("Ошибка открытия образа обновления"));
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
    if (m_crossfadeStarted) return;
    if (!m_waveMode.isEmpty() && m_queue.size() - m_queueIndex < 6 && m_state.value("waveError").toString().isEmpty()) refillWave();
    cancelPreload();
    const int generation = m_preloadGeneration;
    if (!preloadAllowed()) return;
#ifdef Q_OS_ANDROID
    if (!m_androidPreload) { if (m_startupPending) startupStage("preload_skipped"); return; }
#endif
    if ((m_startupPending && !m_startupSourceSet) || m_queueIndex < 0 || m_queue.isEmpty() || player().source().isEmpty()) return;
    const int index = nextQueueIndex(true);
    if (index < 0) return;
    const auto track = m_queue[index].toMap();
    if (track.value("access").toString() == "preview" || track.value("access").toString() == "blocked" || track.value("streamable") == false) return;
    m_preloadIndex = index; m_preloadUrn = urn(track);
    if (m_startupPending) startupStage("preload_request");
    request(apiPath("tracks", m_preloadUrn) + "/streams", [this, generation](QJsonDocument doc) {
        if (generation != m_preloadGeneration) return;
        const auto object = doc.object();
        for (const auto &candidate : fullStreams(object)) m_preloadCandidates.append(candidate.second);
        prepareStream(QUrl{}, generation);
    }, "GET", {}, true, [](QString) {}, {}, 0, true);
}
void SoundCloud::finishCrossfade(bool prepare) {
    m_crossfade.stop();
    m_preloadPlayer->stop(); m_preloadPlayer->setSource(QUrl{}); m_preloadPlayer->setAudioOutput(nullptr);
    player().setAudioOutput(nullptr); player().setAudioOutput(&m_audio);
    m_preloadPlayer->setAudioOutput(&m_preloadAudio);
    m_audio.setVolume(m_state.value("volume").toReal()); m_preloadAudio.setVolume(0);
    m_crossfadeStarted = false;
    if (prepare) prepareNext();
}
void SoundCloud::beginCrossfade() {
    if (!preloadAllowed()) return;
    if ((m_waveMode.isEmpty() && m_state.value("repeatMode").toInt() == 1) ||
        m_preloadIndex != nextQueueIndex(true) || m_crossfadeStarted || m_queue.isEmpty() || m_preloadIndex < 0 || m_preloadIndex >= m_queue.size() ||
        m_preloadPlayer->source().isEmpty() || m_preloadPlayer->error() != QMediaPlayer::NoError ||
        (m_preloadPlayer->mediaStatus() != QMediaPlayer::BufferedMedia && m_preloadPlayer->mediaStatus() != QMediaPlayer::LoadedMedia) ||
        urn(m_queue[m_preloadIndex].toMap()) != m_preloadUrn) return;
    const QVariantMap nextTrack = m_queue[m_preloadIndex].toMap();
    m_followHistory = !m_forceNext && m_historyCursor + 1 < m_history.size()
        && m_queueIds.value(m_preloadIndex) == m_history.value(m_historyCursor + 1);
    if (m_followHistory) ++m_historyCursor;
    m_queueIndex = m_preloadIndex; recordQueueEntry();
    set("queueIndex", m_queueIndex); set("queue", m_queue);
    set("current", nextTrack); set("position", 0); set("duration", nextTrack.value("duration")); set("buffering", false);
    m_forceNext = false;
    ++m_playGeneration; ++m_preloadGeneration;
    m_streamCandidates.clear(); m_streamCandidateIndex = -1; m_previewStreamAvailable = false;
    m_startupClock.start(); m_lastStartupMs = 0; m_startupInitialPosition = 0;
    m_startupPending = true; m_startupAudible = false; m_startupUsedPreload = true;
    m_startupSourceSet = true; m_startupLoadedLogged = false; m_preloadedRetryUsed = false;
    QMediaPlayer *incoming = m_preloadPlayer;
    std::swap(m_activePlayer, m_preloadPlayer);
    m_preloadUrn.clear(); m_preloadIndex = -1;
    m_crossfadeStarted = true;
    m_streamCache.suspend();
    incoming->play();
    m_streamCache.warm(incoming->source());
    m_crossfade.stop(); m_crossfade.setStartValue(0.0); m_crossfade.setEndValue(1.0); m_crossfade.start();
    savePlayback();
}
void SoundCloud::prepareStream(const QUrl &url, int generation, int redirects) {
    if (generation != m_preloadGeneration || !preloadAllowed()) return;
    if (url.scheme() != "https" || redirects > 5 || !url.userInfo().isEmpty()) {
        if (!m_preloadCandidates.isEmpty()) prepareStream(m_preloadCandidates.takeFirst(), generation);
        return;
    }
    if (url.host() != "api.soundcloud.com") {
        m_preloadPlayer->setSource(m_streamCache.source(url));
        // Подготавливаем источник без запуска декодирования и расхода батареи.
        return;
    }
    if (m_startupPending && !m_startupSourceSet) return;
    const auto wait = rateDeadline("streams") - m_requestNow();
    if (wait > 0) return;
    QNetworkRequest req(url);
    req.setPriority(QNetworkRequest::LowPriority);
    req.setRawHeader("Authorization", "OAuth " + m_access.toUtf8());
    req.setTransferTimeout(20000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_network->get(req);
    m_preloadReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, generation, redirects] {
        reply->deleteLater();
        if (generation != m_preloadGeneration) return;
        observeRate(reply, "streams");
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 429) return;
        if (reply->error() != QNetworkReply::NoError) { prepareStream(QUrl{}, generation); return; }
        auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!redirect.isEmpty()) { prepareStream(url.resolved(redirect), generation, redirects + 1); return; }
        const auto next = QJsonDocument::fromJson(reply->readAll()).object().value("url").toString();
        prepareStream(QUrl(next), generation, redirects + 1);
    });
}
