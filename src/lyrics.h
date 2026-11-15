#pragma once
#include <QObject>
#include <QVariantMap>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QNetworkReply>
#include <QHash>
#include <QDateTime>
#include <QUrlQuery>
#include <QSet>
#include <functional>

// Independent public lyrics transport: no SoundCloud credentials are sent.
class Lyrics : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
public:
    explicit Lyrics(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
    ~Lyrics() override;
    QVariantMap state() const { return m_state; }
    Q_INVOKABLE void lookup(const QString &title, const QString &artist, qint64 duration, const QString &trackKey = {});
    Q_INVOKABLE bool setCustomText(const QString &text);
    Q_INVOKABLE void removeCustomText();
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void cancel();
    static QVariantList parseLrc(const QString &text);
signals:
    void changed();
private:
    void fetch(const QUrl &url, std::function<void(const QByteArray &)> consume);
    void searchProviders(const QString &query);
    void addResults(const QVariantList &records);
    void loadRecord(QVariantMap record, QVariantList remaining = {});
    void finish();
    void apply(const QVariantMap &record);
    QNetworkAccessManager *m_network;
    QList<QPointer<QNetworkReply>> m_replies;
    int m_pending = 0;
    bool m_hadError = false;

    QSet<QString> m_seen;
    QVariantMap m_state;
    QHash<QString, QByteArray> m_cache;
    quint64 m_generation = 0;
    QHash<QString, QDateTime> m_retryAfter;
    QString m_trackKey;
    QString m_title, m_artist;
    qint64 m_duration = 0;
    QString m_queryTitle, m_queryArtist;
};
