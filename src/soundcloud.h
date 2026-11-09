#pragma once
#include <QObject>
#include <QAbstractListModel>
#include <QHash>
#include "secretstore.h"
#include <QVariantMap>
#include <QNetworkAccessManager>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVariantAnimation>
#include <QTcpServer>
#include <QTimer>
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
private:
    QVariantList m_items;
};

// Секреты доступны только хосту через системное защищённое хранилище.
class SoundCloud : public QObject {
    Q_OBJECT
    Q_PROPERTY(TrackModel* trackModel READ trackModel CONSTANT)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY signedInChanged)
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    // Каталог не инвалидируется при каждом обновлении позиции воспроизведения.
    Q_PROPERTY(QVariantMap catalogState READ state NOTIFY catalogChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY progressChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY progressChanged)
public:
    explicit SoundCloud(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr, std::shared_ptr<SecretStore> secrets = {});
    ~SoundCloud() override;
    TrackModel *trackModel() { return &m_trackModel; }
    bool signedIn() const { return m_state.value("signedIn").toBool(); }
    bool seekable() const { return m_activePlayer->isSeekable(); }
    QVariantMap state() const { return m_state; }
    qint64 position() const { return m_state.value("position").toLongLong(); }
    qint64 duration() const { return m_state.value("duration").toLongLong(); }
    Q_INVOKABLE void connectCatalog();
    Q_INVOKABLE void login();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void browserFailed();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void configure(const QString &id, const QString &secret, const QString &redirect);
    Q_INVOKABLE void browse(const QString &page, const QString &query = {}, bool force = false);
    Q_INVOKABLE void more();
    Q_INVOKABLE void loadHome(bool force = false);
    Q_INVOKABLE void playMix(const QVariantList &tracks, int index = 0);
    Q_INVOKABLE void playStation(const QVariantMap &seed);
    Q_INVOKABLE void listDestinations(const QString &next = {});
    Q_INVOKABLE void saveToPlaylist(const QString &track, const QString &playlist, const QString &newTitle);
    Q_INVOKABLE void openPlaylist(const QVariantMap &item);
    Q_INVOKABLE void play(const QVariantMap &item);
    Q_INVOKABLE void enqueue(const QVariantMap &item);
    Q_INVOKABLE void playQueue(int index);
    Q_INVOKABLE void moveQueue(int from, int to);
    Q_INVOKABLE void queueNext(int index);
    Q_INVOKABLE void removeQueue(int index);
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void pausePlayback();
    Q_INVOKABLE void resumePlayback();
    Q_INVOKABLE void stopPlayback();
    void restoreAndroidTrack(const QVariantMap &track, qint64 position);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 position);
    Q_INVOKABLE void volume(double value);
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
    Q_INVOKABLE void setShuffle(bool enabled);
    Q_INVOKABLE void setCrossfade(bool enabled);
signals:
    void changed();
    void catalogChanged();
    void progressChanged();
    void signedInChanged();
    void trackSaved();
    void authorizationRequested(const QUrl &url);
private:
    friend class SoundCloudTest;
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
    void request(const QString &path, Callback done, const QByteArray &method = "GET", const QJsonObject &body = {}, bool retry = true, std::function<void(QString)> failed = {});
    void collection(const QString &path, bool append = false);
    void profile();
    void savePlayback();
    void restorePlayback(const QVariantMap &profile);
    void startTrack(const QVariantMap &track, bool autoplay = true, qint64 position = 0);
    void tryRestoreAndroidTrack();
    void failAndroidRestore();
    void resolveStream(const QUrl &url, int generation, int redirects = 0);
    void prepareNext();
    void holdPreloadedAudio(QMediaPlayer *media);
    void prepareStream(const QUrl &url, int generation, int redirects = 0);
    void beginCrossfade();
    void callbackConnection();
    bool clearSession();
    static QString urn(const QVariantMap &item);
    bool m_systemStore = false;
    AuthStorage m_authStorage;
    QJsonObject m_connection;
    bool m_storageReady = false;
    QVariantMap m_state;
    TrackModel m_trackModel;
    struct CachedListing { QVariantList tracks; QString next; };
    QHash<QString, CachedListing> m_listings;
    QTimer m_saveTimer;
    bool m_restoreChecked = false, m_snapshotPlayback = false, m_homeLoaded = false;
    QNetworkAccessManager *m_network;
    QMediaPlayer m_player;
    QMediaPlayer m_sparePlayer;
    QMediaPlayer &player() { return *m_activePlayer; }
    QMediaPlayer *m_activePlayer = &m_player;
    QAudioOutput m_preloadAudio;
    QMediaPlayer *m_preloadPlayer = &m_sparePlayer;
    QString m_preloadUrn;
    int m_preloadGeneration = 0;
    int m_preloadIndex = -1;
    QAudioOutput m_audio;
    QVariantAnimation m_volumeFade;
    QVariantAnimation m_crossfade;
    QTcpServer m_callback;
    QTimer m_loginTimeout;
    QString m_configPath, m_sessionPath, m_clientId, m_clientSecret, m_redirect;
    QString m_access, m_refresh, m_verifier, m_oauthState, m_nextHref, m_lastPath;
    qint64 m_expires = 0;
    bool m_useWorker = true;
    bool m_userSession = false, m_refreshing = false;
    bool m_crossfadeStarted = false;
    int m_homeGeneration = 0;
    int m_epoch = 0, m_listing = 0, m_playGeneration = 0, m_queueIndex = -1;
    int m_loginEpoch = 0;
    QVariantMap m_androidRestoreTrack;
    qint64 m_restorePosition = 0;
    bool m_androidRestorePending = false, m_restoringPlayback = false;
    QList<WaitingOperation> m_waiting;
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
