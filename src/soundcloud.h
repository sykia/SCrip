#pragma once
#include <QObject>
#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include "secretstore.h"
#include "trackmetadata.h"
#include "trackinfo.h"
#include "streamcache.h"
#include "audioeffects.h"
#include <QVariantMap>
#include <QNetworkAccessManager>
#include <QMediaPlayer>
#include <QAudioOutput>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QAudioBufferOutput>
#include <QAudioBuffer>
#endif
#include <QVariantAnimation>
#include <QTcpServer>
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <functional>
#include <memory>
#ifdef Q_OS_LINUX
struct SoundCloudStorageCompletion;
#endif

class TrackModel : public QAbstractListModel {
    Q_OBJECT
public:
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_items.size(); }
    QVariant data(const QModelIndex &index, int role) const override {
        return index.isValid() && index.row() >= 0 && index.row() < m_items.size() && role == Qt::UserRole + 1 ? m_items[index.row()] : QVariant{};
    }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole + 1, "modelData"}}; }
    void update(const QVariantList &items);
    bool moveItem(int from, int to);
    bool removeItem(int index);
private:
    QVariantList m_items;
};

// Секреты доступны только хосту через системное защищённое хранилище.
class SoundCloud : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap commentsState READ commentsState NOTIFY commentsChanged)
    Q_PROPERTY(QVariantList spectrum READ spectrum NOTIFY spectrumChanged)
    Q_PROPERTY(QVariantList equalizerGains READ equalizerGains NOTIFY soundControlsChanged)
    Q_PROPERTY(bool equalizerEnabled READ equalizerEnabled WRITE setEqualizerEnabled NOTIFY soundControlsChanged)
    Q_PROPERTY(bool equalizerAvailable READ equalizerAvailable CONSTANT)
    Q_PROPERTY(QString equalizerError READ equalizerError NOTIFY soundControlsChanged)
    Q_PROPERTY(double waveLevel READ waveLevel NOTIFY waveAudioChanged)
    Q_PROPERTY(double waveBass READ waveBass NOTIFY waveAudioChanged)
    Q_PROPERTY(double waveBeat READ waveBeat NOTIFY waveAudioChanged)
    Q_PROPERTY(TrackModel* trackModel READ trackModel CONSTANT)
    Q_PROPERTY(TrackModel* playlistEditModel READ playlistEditModel CONSTANT)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY signedInChanged)
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    // Каталог не инвалидируется при каждом обновлении позиции воспроизведения.
    Q_PROPERTY(QVariantMap catalogState READ state NOTIFY catalogChanged)
    Q_PROPERTY(QVariantMap uiState READ uiState NOTIFY catalogChanged)
    Q_PROPERTY(double volumeLevel READ volumeLevel NOTIFY volumeChanged)
    Q_PROPERTY(QVariantList collectionTracks READ collectionTracks NOTIFY collectionTracksChanged)
    Q_PROPERTY(QVariantList playlistRows READ playlistRows NOTIFY playlistRowsChanged)
    Q_PROPERTY(QVariantList queueTracks READ queueTracks NOTIFY queueTracksChanged)
    Q_PROPERTY(QVariantList homeShelves READ homeShelves NOTIFY homeShelvesChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY progressChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY progressChanged)
public:
    explicit SoundCloud(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr, std::shared_ptr<SecretStore> secrets = {});
    ~SoundCloud() override;
    TrackModel *trackModel() { return &m_trackModel; }
    TrackModel *playlistEditModel() { return &m_playlistEditModel; }
    bool signedIn() const { return m_state.value("signedIn").toBool(); }
    bool seekable() const { return m_activePlayer->isSeekable(); }
    QVariantMap state() const { return m_state; }
    QVariantMap uiState() const {
        auto state = m_state;
        for (const auto *key : {"tracks", "playlistTracks", "queue", "home"}) state.remove(key);
        return state;
    }

    double volumeLevel() const { return m_state.value("volume").toDouble(); }
    QVariantList collectionTracks() const { return m_state.value("tracks").toList(); }
    QVariantList playlistRows() const { return m_state.value("playlistTracks").toList(); }
    QVariantList queueTracks() const { return m_queue; }
    QVariantList homeShelves() const { return m_state.value("home").toList(); }

    qint64 position() const { return m_state.value("position").toLongLong(); }
    qint64 duration() const { return m_state.value("duration").toLongLong(); }
    QVariantMap commentsState() const { return m_comments; }
    Q_INVOKABLE QString trackAvailability(const QVariantMap &track) const {
        auto status = TrackInfo::availability(track);
        if (status == "Полная версия") status.clear();
        if (TrackInfo::text(track.value("sharing")) == "private")
            status += (status.isEmpty() ? "" : " · ") + QString("Приватный");
        return status;
    }
    Q_INVOKABLE QVariantList trackInfo(const QVariantMap &track) const { return TrackInfo::rows(track); }
    Q_INVOKABLE QString trackDescription(const QVariantMap &track) const { return TrackInfo::text(track.value("description")); }
    Q_INVOKABLE QString displayArtist(const QVariantMap &track) const { return TrackMetadata::displayArtist(track); }
    Q_INVOKABLE bool hasSeparateArtist(const QVariantMap &track) const { return TrackMetadata::hasSeparateArtist(track); }
    Q_INVOKABLE void setCommentTrack(const QVariantMap &track);
    Q_INVOKABLE void loadComments(const QString &track, bool append = false, bool force = false);
    Q_INVOKABLE void postComment(const QString &track, const QString &body, qint64 timestamp = -1);
    Q_INVOKABLE void connectCatalog();
    Q_INVOKABLE void setRelayEnabled(bool enabled);
    Q_INVOKABLE void login();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void browserFailed();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void configure(const QString &id, const QString &secret, const QString &redirect);
    Q_INVOKABLE QVariantMap validateTrackFilters(const QVariantMap &filters) const;
    Q_INVOKABLE bool applyTrackFilters(const QVariantMap &filters);
    Q_INVOKABLE void browse(const QString &page, const QString &query = {}, bool force = false);
    Q_INVOKABLE void more();
    Q_INVOKABLE void searchWholeCollection(bool playlist = false);
    Q_INVOKABLE void cancelCollectionSearch();
    Q_INVOKABLE void loadHome(bool force = false);
    Q_INVOKABLE void playMix(const QVariantList &tracks, int index = 0);
    double waveLevel() const { return m_waveLevel; }
    double waveBass() const { return m_waveBass; }
    double waveBeat() const { return m_waveBeat; }
    QVariantList spectrum() const { return m_spectrum; }
    QVariantList equalizerGains() const;
    bool equalizerEnabled() const { return m_equalizerEnabled; }
    bool equalizerAvailable() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        return qEnvironmentVariable("QT_MEDIA_BACKEND", "ffmpeg") == "ffmpeg";
#else
        return false;
#endif
    }
    QString equalizerError() const { return m_equalizerError; }
    Q_INVOKABLE void setEqualizerEnabled(bool enabled);
    Q_INVOKABLE void setEqualizerGain(int band, double db);
    Q_INVOKABLE void resetEqualizer();
    Q_INVOKABLE void setTrackVisualActive(bool enabled);
    Q_INVOKABLE void setWaveVisualActive(bool enabled);
    Q_INVOKABLE void startWave(const QString &mode);
    Q_INVOKABLE void stopWave();
    Q_INVOKABLE void retryWave();
    Q_INVOKABLE void excludeRecommendation(const QVariantMap &track, bool account = false);
    Q_INVOKABLE void undoRecommendationExclusion();
    Q_INVOKABLE void restoreRecommendation(const QString &id, bool account);
    Q_INVOKABLE void clearRecommendationExclusions();
    Q_INVOKABLE void setRecommendationPreference(int value);
    Q_INVOKABLE void chooseWavePreference(int value);
    Q_INVOKABLE void playStation(const QVariantMap &seed);
    Q_INVOKABLE void listDestinations(const QString &next = {});
    Q_INVOKABLE void saveToPlaylist(const QString &track, const QString &playlist, const QString &newTitle);
    Q_INVOKABLE void loadPlaylist(const QVariantMap &item, bool append = false);
    Q_INVOKABLE void openPlaylist(const QVariantMap &item);
    Q_INVOKABLE void play(const QVariantMap &item);
    Q_INVOKABLE void enqueue(const QVariantMap &item);
    Q_INVOKABLE QString enqueueTracks(const QVariantList &tracks);
    Q_INVOKABLE void saveTracksToPlaylist(const QVariantList &tracks, const QString &playlist, const QString &newTitle, const QString &token);
    Q_INVOKABLE void playQueue(int index);
    Q_INVOKABLE void moveQueue(int from, int to);
    Q_INVOKABLE void queueNext(int index);
    Q_INVOKABLE void removeQueue(int index);
    Q_INVOKABLE void undoQueueEdit();
    void rememberQueueEdit();
    Q_INVOKABLE void deduplicateQueue();
    Q_INVOKABLE void clearQueueAfterCurrent();
    Q_INVOKABLE void saveQueueAsPlaylist(const QString &title);
    Q_INVOKABLE bool queueHasDuplicates() const;
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void pausePlayback();
    Q_INVOKABLE void resumePlayback();
    Q_INVOKABLE void stopPlayback();
    void restoreAndroidTrack(const QVariantMap &track, qint64 position);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 position);
    Q_INVOKABLE void volume(double value);
    Q_INVOKABLE void closePeople();
    Q_INVOKABLE void loadPeople(const QVariantMap &user, const QString &kind, bool append = false);
    Q_INVOKABLE void loadProfileLinks(const QVariantMap &user, bool append = false);
    Q_INVOKABLE void loadEngagement(const QString &kind);
    Q_INVOKABLE void toggleRepost(const QVariantMap &item);
    Q_INVOKABLE void editPlaylist(const QVariantMap &item);
    Q_INVOKABLE void movePlaylistTrack(int from, int to);
    Q_INVOKABLE void removePlaylistTrack(int index);
    Q_INVOKABLE void savePlaylist(const QString &title, const QString &description, bool privatePlaylist);
    Q_INVOKABLE void deletePlaylist();
    Q_INVOKABLE void cancelPlaylistEdit();
    Q_INVOKABLE void openStation(const QString &urn, const QVariantMap &seed = {});
    Q_INVOKABLE void openProfileWebsite(const QString &url);
    Q_INVOKABLE void openLink(const QString &url);
    Q_INVOKABLE void loadFollowings();
    Q_INVOKABLE void toggleFollow(const QVariantMap &user);
    Q_INVOKABLE void like(const QVariantMap &item);
    Q_INVOKABLE void createPlaylist(const QString &title, bool includeCurrent);
    Q_INVOKABLE void openExternal(const QString &url);
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void checkForUpdates(bool manual = true);
    Q_INVOKABLE void downloadUpdate();
    Q_INVOKABLE void cancelUpdateDownload();
    Q_INVOKABLE void installUpdate();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void setRepeat(bool enabled);
    Q_INVOKABLE void setRepeatMode(int mode);
    Q_INVOKABLE void setShuffle(bool enabled);
    Q_INVOKABLE void setCrossfade(bool enabled);
    Q_INVOKABLE void setPlaybackQuality(int quality);
    Q_INVOKABLE void setPreloadEnabled(bool enabled);
    Q_INVOKABLE void setPreloadWifiOnly(bool enabled);
    bool canGoNext() const;
    bool canResumePlayback() const;
    bool canGoPrevious() const;
    bool preloadAllowed() const;

signals:
    void resourceResolved(const QVariantMap &item);
    void queuePlaylistSaved();
    void selectionSaved(const QString &token, bool success, const QString &error);
    void playlistUpdated(const QVariantMap &item);
    void playlistDeleted(const QString &urn);
    void commentsChanged();
    void commentPosted(const QString &track);
    void spectrumChanged();
    void soundControlsChanged();
    void waveAudioChanged();
    void recommendationExclusionAdded();
    void changed();
    void catalogChanged();
    void progressChanged();
    void volumeChanged();
    void collectionTracksChanged();
    void playlistRowsChanged();
    void queueTracksChanged();
    void homeShelvesChanged();
    void playbackSeeked();
    void signedInChanged();
    void trackSaved();
    void authorizationRequested(const QUrl &url);
private:
    friend class SoundCloudTest;
    QVariantMap m_comments;
    int m_commentsGeneration = 0;
    QString m_commentsNext;
    QSet<QString> m_commentsPages;
    void updateComments(const QString &key, const QVariant &value);
    using Callback = std::function<void(QJsonDocument)>;
    void set(const QString &key, const QVariant &value);
    void fail(const QString &message);
    bool loadConfig();
    bool saveSession(const QString &access, const QString &refresh, qint64 expires, bool userSession);
    void storageFailure();
#ifdef Q_OS_LINUX
    void finishStorageLoad(bool ok, const QJsonObject &connection, const QJsonObject &session, bool autoConnect);
    std::shared_ptr<SoundCloudStorageCompletion> m_storageCompletion;
#endif
    using TokenFailure = std::function<void(QString)>;
    enum class TokenFlow { AuthorizationCode, Refresh, ClientCredentials };
    struct WaitingOperation { std::function<void()> continuation; TokenFailure failed; };
    void ensureToken(std::function<void()> continuation, TokenFailure failed = {});
    void token(const QUrlQuery &form, bool basic, TokenFlow flow, std::function<void()> done, TokenFailure failed = {}, int loginEpoch = -1);
    QUrl apiTransport(const QUrl &url) const;
    void request(const QString &path, Callback done, const QByteArray &method = "GET", const QJsonObject &body = {}, bool retry = true, std::function<void(QString)> failed = {}, std::function<void()> started = {}, int redirects = 0, bool background = false);
    struct ReadWaiter { Callback done; TokenFailure failed; };
    QHash<QString, std::shared_ptr<QList<ReadWaiter>>> m_reads;
    quint64 m_engagementRevision = 0;
    void acceptEngagementCounts(const QJsonDocument &doc);
    QHash<QString, qint64> m_rateUntil;
    std::function<qint64()> m_requestNow = [] { return QDateTime::currentMSecsSinceEpoch(); };
    std::function<void(qint64, std::function<void()>)> m_requestLater;
    QString rateGroup(const QUrl &url, const QByteArray &method = "GET") const;
    qint64 rateDeadline(const QString &group) const;
    void observeRate(QNetworkReply *reply, const QString &groupOverride = {});
    void deferRequest(qint64 delay, std::function<void()> operation);
    void requestAttempt(const QString &path, Callback done, const QByteArray &method, const QJsonObject &body,
                        bool retry, TokenFailure failed, std::function<void()> started, int redirects,
                        int attempt, bool background);
    void collection(const QString &path, bool append = false);
    void readLikes(Callback done, TokenFailure failed = {});
    void relatedSources(const QVariantList &sources, std::function<bool()> current, Callback done, TokenFailure failed);
    QJsonDocument m_firstLikes;
    void profile();
    void loadEngagementPage(const QString &kind, const QString &path, int generation);
    void likeCollection(const QVariantMap &item);
    void resetSocialState();
    int m_playlistOperationGeneration = 0;
    bool m_playlistWriteBusy = false;
    void validatePlaylistSnapshot(const QString &id, const QVariantMap &original, std::function<void(QVariantMap)> done, TokenFailure failed);
    void fetchPlaylistSnapshot(const QString &id, std::function<void(QVariantMap)> done, TokenFailure failed);
    void fetchPlaylistTracks(const QString &path, QVariantMap playlist, QVariantList tracks, QSet<QString> pages,
                             std::function<void(QVariantMap)> done, TokenFailure failed);
    void invalidatePlaylists();
    void finishQueueEdit();
    int m_peopleGeneration = 0, m_linksGeneration = 0, m_editGeneration = 0, m_linkGeneration = 0;
    QHash<QString, int> m_engagementGeneration;
    QHash<QString, QSet<QString>> m_engagementPages;
    QSet<QString> m_peoplePages, m_linkPages;
    QString m_peopleNext, m_linksNext, m_pendingLink;
    QVariantMap m_editOriginal;

    void loadFollowingPage(const QString &path);
    QSet<QString> m_followPages;
    void confirmEngagementCount(const QVariantMap &item, bool repost, bool wasSelected);
    QVariantMap withLikeState(QVariantMap track) const;
    QVariantList withLikeStates(QVariantList tracks) const;
    void refreshLikeStates();
    void syncLikes(const QString &path = "/me/likes/tracks?limit=200&linked_partitioning=true");
    void applyPendingSeek();
    void savePlayback();
    void savePlaybackPosition();
    void restorePlayback(const QVariantMap &profile);
    void startTrack(const QVariantMap &track, bool autoplay = true, qint64 position = 0, bool recovery = false);
    void tryRestoreAndroidTrack();
    void failAndroidRestore();
    void resolveStream(const QUrl &url, int generation, int redirects = 0);
    void nextStream(int generation, const char *reason);
    void startupStage(const char *stage);
    void recoverPlayback();
    void checkPlaybackConnection();
    void skipFailedPlayback();
    QTimer m_playbackRecoveryTimer, m_playbackWatchdog;
    bool m_playbackRecovering = false;
    int m_playbackRetries = 0, m_playbackIdleSeconds = 0, m_connectionFailures = 0;
    qint64 m_recoveryPosition = 0, m_watchdogPosition = -1;
    QSet<QString> m_failedPlaybackTracks;
    void prepareNext();
    void cancelPreload();
    void updatePreloadPolicy();
    QList<QPair<QString, QUrl>> fullStreams(const QJsonObject &object) const;
    QPointer<QNetworkReply> m_preloadReply;

    void loadRecommendationPreferences(const QString &account);
    void saveRecommendationPreferences();
    void publishRecommendationPreferences();
    bool recommendationExcluded(const QVariant &track) const;
    QVariantList filterRecommendations(const QVariantList &tracks) const;
    void pruneRecommendations(bool prepare = true);
    QString m_recommendationAccount;
    QVariantMap m_excludedTracks, m_excludedAccounts;
    QString m_exclusionUndo;
    bool m_exclusionUndoAccount = false;
    int m_recommendationPreference = 1;
    QVariantList m_familiarTracks, m_waveCandidates;
    bool m_wavePreferenceWaiting = false;
    void refillWave();
    void scheduleWaveRefill();
    void restoreWaveState(const QVariantMap &snapshot);
    void updateAudioRouting();
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    void processAudio(QMediaPlayer *media, const QAudioBuffer &buffer);
#endif
    AudioEffects m_playerEffects, m_spareEffects;
    std::array<double, 5> m_equalizerGains{};
    bool m_equalizerEnabled = false, m_trackVisualActive = false;
    QString m_equalizerError;
    QVariantList m_spectrum, m_spectrumTarget;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    void analyzeWaveAudio(const QAudioBuffer &buffer);
    QAudioBufferOutput m_waveOutput, m_waveSpareOutput;
#endif
    QTimer m_waveAudioTimer;
    QElapsedTimer m_waveBeatClock;
    bool m_waveVisualActive = false;
    double m_waveLevel = 0, m_waveBass = 0, m_waveBeat = 0;
    double m_waveTargetLevel = 0, m_waveTargetBass = 0, m_waveTargetBeat = 0;
    double m_waveLowPass = 0, m_waveBaseline = 0;

    int m_waveGeneration = 0, m_waveSeedIndex = 0, m_waveEmptyPages = 0;
    QString m_waveMode, m_waveNext, m_waveRelatedNext;
    QTimer m_waveRetryTimer;
    int m_waveRetryAttempts = 0;
    bool m_waveLoadSeeds = false;
    QVariantList m_waveSeeds;
    QSet<QString> m_waveSeen;
    bool m_waveWaiting = false;
    void holdPreloadedAudio(QMediaPlayer *media);
    void prepareStream(const QUrl &url, int generation, int redirects = 0);
    void beginCrossfade();
    void finishCrossfade(bool prepare = true);
    QUrl beginLogin(bool openBrowser);
    QString m_retryToken;
    void callbackConnection();
    bool clearSession();
    static QString urn(const QVariantMap &item);
    bool m_systemStore = false;
    AuthStorage m_authStorage;
    QJsonObject m_connection;
    bool m_storageReady = false;
    QVariantMap m_state;
    TrackModel m_trackModel, m_playlistEditModel;
    struct CachedListing { QVariantList tracks; QString next; };
    QHash<QString, CachedListing> m_listings;
    QTimer m_saveTimer;
    QTimer m_volumeSaveTimer;
    bool m_restoreChecked = false, m_snapshotPlayback = false, m_homeLoaded = false;
    QNetworkAccessManager *m_network;
    StreamCache m_streamCache;
    QMediaPlayer m_player;
    QMediaPlayer m_sparePlayer;
    QMediaPlayer &player() { return *m_activePlayer; }
    QMediaPlayer *m_activePlayer = &m_player;
    QAudioOutput m_preloadAudio;
    QMediaPlayer *m_preloadPlayer = &m_sparePlayer;
    QString m_preloadUrn;
    int m_preloadGeneration = 0;
    int m_preloadIndex = -1;
    QList<QUrl> m_preloadCandidates;
    QList<QPair<QString, QUrl>> m_streamCandidates;
    int m_streamCandidateIndex = -1;
    QElapsedTimer m_startupClock;
    qint64 m_lastStartupMs = 0;
    qint64 m_startupInitialPosition = 0;
    bool m_startupPending = false;
    bool m_startupAudible = false;
    bool m_startupSourceSet = false;
    bool m_startupLoadedLogged = false;
    bool m_startupUsedPreload = false;
    bool m_preloadedRetryUsed = false;
    bool m_previewStreamAvailable = false;
    bool m_androidPreload = false;
    QAudioOutput m_audio;
    QVariantAnimation m_volumeFade;
    QVariantAnimation m_crossfade;
    QTcpServer m_callback;
    QTimer m_loginTimeout;
    QString m_configPath, m_sessionPath, m_clientId, m_clientSecret, m_redirect;
    QUrlQuery m_trackFilters;
    QVariantMap m_commentTrack;
    bool m_collectionSearch = false;
    bool m_searchPlaylist = false;
    int m_searchPagesRemaining = 0;
    QVariantMap m_searchPlaylistItem;
    QSet<QString> m_playlistPages;
    QVariantList m_undoQueue;
    QVariantMap m_undoOrder;
    QList<int> m_undoAfterIds;
    QString m_access, m_refresh, m_verifier, m_oauthState, m_nextHref, m_lastPath;
    qint64 m_expires = 0;
    bool m_useWorker = true;
    bool m_userSession = false, m_refreshing = false;
    bool m_crossfadeStarted = false;
    int m_homeGeneration = 0;
    int m_likeRevision = 0;
    int m_likeGeneration = 0;
    QSet<QString> m_likedTracks, m_likePages;
    QHash<QString, bool> m_likeOverrides;
    bool m_likesComplete = false;
    qint64 m_pendingSeek = -1;
    int m_playlistGeneration = 0;
    QString m_playlistNext;
    QSet<QString> m_collectionPages;
    int m_profileBrowseGeneration = 0;
    int m_epoch = 0, m_listing = 0, m_playGeneration = 0, m_queueIndex = -1;
    int m_loginEpoch = 0;
    QVariantMap m_androidRestoreTrack;
    qint64 m_restorePosition = 0;
    bool m_androidRestorePending = false, m_restoringPlayback = false;
    QList<WaitingOperation> m_waiting;
    void syncQueueOrder();
    void resetQueueOrder();
    void recordQueueEntry();
    int nextQueueIndex(bool natural = false);
    void advancePlayback(bool natural);
    QVariantMap queueOrderSnapshot() const;
    void restoreQueueOrder(const QVariantMap &snapshot);
    QList<int> m_queueIds, m_history;
    QSet<int> m_visited;
    int m_nextQueueId = 1, m_historyCursor = -1, m_plannedId = -1;
    bool m_followHistory = false;
    bool m_forceNext = false;
    QVariantList m_queue;
    QNetworkReply *m_updateReply = nullptr;
    QSaveFile *m_updateFile = nullptr;
    std::unique_ptr<QCryptographicHash> m_updateHash;
    QTimer m_updateProgressTimer;
    QJsonObject m_updateAsset;
    QString m_updateDigest, m_updatePath;
    QString m_updateVersion, m_updateReadyVersion, m_updateReadyName;
    QString m_updateReadyDigest;
    qint64 m_updateExpectedSize = -1, m_updateReceived = 0;
};
