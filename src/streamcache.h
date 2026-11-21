#pragma once
#include <QObject>
#include <QCache>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QTcpSocket>
#include <QNetworkReply>
#include <memory>
#include <QThread>

// Локальный HLS-прокси: запросы плеера обслуживаются сразу, фон — последовательно.
class StreamCacheWorker : public QObject {
public:
    explicit StreamCacheWorker(QObject *parent = nullptr);
    ~StreamCacheWorker() override;
    QUrl source(const QUrl &remote);
    void suspend();
    void setBackgroundEnabled(bool enabled);
    void discard(const QUrl &local);
    void interrupt(const QUrl &local);
    void warm(const QUrl &local);
private:
    friend class StreamCacheTest;
    struct Entry {
        QString file;
        QByteArray type;
        ~Entry();
    };
    struct Resource { QUrl remote; QString root; };
    QUrl map(const QUrl &remote, const QString &root, const QString &fallbackSuffix = {});
    QByteArray playlist(const QByteArray &body, const Resource &resource);
    void accept();
    void fetch(const QString &key, QPointer<QTcpSocket> socket = {}, QByteArray range = {}, bool head = false, int attempt = 0);
    void next();
    QTcpServer m_server;
    QNetworkAccessManager m_network;
    QTemporaryDir m_directory;
    // Верхний предел на диске: место выделяется по мере загрузки, вытесняются
    // давно неиспользуемые сегменты. Источники дополнительно ограничены в source().
    QCache<QString, Entry> m_cache{512 * 1024 * 1024};
    QHash<QString, Resource> m_resources;
    QHash<QString, QString> m_keys;
    QHash<QString, QStringList> m_children;
    QTimer m_timer;
    QPointer<QNetworkReply> m_background;
    QString m_root;
    QStringList m_roots;
    QStringList m_pending;
    bool m_backgroundEnabled = true;
    int m_serial = 0;
    int m_foreground = 0;
};

// The HTTP proxy must keep running while Qt synchronously joins a decoder on
// the GUI thread (including deferred deletion after a backend error).
class StreamCache : public QObject {
public:
    explicit StreamCache(QObject *parent = nullptr);
    ~StreamCache() override;
    QUrl source(const QUrl &remote);
    void suspend();
    void setBackgroundEnabled(bool enabled);
    void discard(const QUrl &local);
    void interrupt(const QUrl &local);
    void warm(const QUrl &local);
private:
    QThread m_thread;
    StreamCacheWorker *m_worker = nullptr;
};
