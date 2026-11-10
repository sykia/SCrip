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

// Локальный HLS-прокси: запросы плеера обслуживаются сразу, фон — последовательно.
class StreamCache : public QObject {
public:
    explicit StreamCache(QObject *parent = nullptr);
    ~StreamCache() override;
    QUrl source(const QUrl &remote);
    void suspend();
    void warm(const QUrl &local);
private:
    friend class StreamCacheTest;
    struct Entry {
        QString file;
        QByteArray type;
        ~Entry();
    };
    struct Resource { QUrl remote; QString root; };
    QUrl map(const QUrl &remote, const QString &root);
    QByteArray playlist(const QByteArray &body, const Resource &resource);
    void accept();
    void fetch(const QString &key, QPointer<QTcpSocket> socket = {}, QByteArray range = {}, bool head = false);
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
    int m_serial = 0;
    int m_foreground = 0;
};
