#include "dnsroute.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QUrlQuery>

bool DnsRoute::allowsHost(const QString &input) {
    const auto host = input.toLower();
    return host == "soundcloud.com" || host.endsWith(".soundcloud.com")
        || host == "sndcdn.com" || host.endsWith(".sndcdn.com")
        || host == "media-streaming.soundcloud.cloud" || host.endsWith(".media-streaming.soundcloud.cloud")
        || host == "scrip-auth.scrip-desktop-aca3d9.workers.dev";
}

namespace {
class RouteFactory final : public QNetworkProxyFactory {
public:
    explicit RouteFactory(std::shared_ptr<std::atomic<quint16>> port) : m_port(std::move(port)) {}
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query) override {
        const auto port = m_port->load();
        if (port && DnsRoute::allowsHost(query.peerHostName())
            && (query.peerPort() == 443 || (query.peerPort() == -1 && query.url().scheme() == "https")))
            return {QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", port)};
        // In particular, the media cache's localhost URLs must never be tunneled.
        if (query.peerHostName() == "localhost" || QHostAddress(query.peerHostName()).isLoopback())
            return {QNetworkProxy(QNetworkProxy::NoProxy)};
        return systemProxyForQuery(query);
    }
private:
    std::shared_ptr<std::atomic<quint16>> m_port;
};

class Tunnel final : public QObject {
public:
    Tunnel(QTcpSocket *client, QNetworkAccessManager *dns, QObject *parent) : QObject(parent), m_client(client), m_dns(dns) {
        setObjectName("dnsTunnel");
        client->setParent(this);
        m_upstream = new QTcpSocket(this);
        m_upstream->setProxy(QNetworkProxy::NoProxy);
        client->setReadBufferSize(128 * 1024);
        m_upstream->setReadBufferSize(128 * 1024);
        m_deadline = new QTimer(this);
        m_deadline->setSingleShot(true);
        connect(m_deadline, &QTimer::timeout, this, [this] { reject(); });
        m_deadline->start(12000);
        connect(client, &QTcpSocket::disconnected, this, &QObject::deleteLater);
        connect(m_upstream, &QTcpSocket::disconnected, this, [this] { m_client->disconnectFromHost(); });
        connect(m_upstream, &QTcpSocket::errorOccurred, this, [this] { reject(); });
        connect(client, &QTcpSocket::readyRead, this, [this] {
            if (m_connected) { pump(m_client, m_upstream); return; }
            if (m_resolving) return;
            m_header += m_client->readAll();
            if (m_header.size() > 8192) { reject(); return; }
            const auto end = m_header.indexOf("\r\n\r\n");
            if (end < 0) return;
            const auto parts = m_header.left(m_header.indexOf("\r\n")).split(' ');
            if (parts.size() != 3 || parts[0] != "CONNECT" || !parts[1].endsWith(":443")
                || (parts[2] != "HTTP/1.1" && parts[2] != "HTTP/1.0")) { reject(); return; }
            const auto host = QString::fromLatin1(parts[1].chopped(4));
            if (!DnsRoute::allowsHost(host) || host.contains('/') || host.contains('@') || host.contains(':')) { reject(); return; }
            m_header.remove(0, end + 4);
            m_resolving = true;
            resolve(host);
        });
        connect(m_upstream, &QTcpSocket::connected, this, [this] {
            m_connected = true;
            m_deadline->stop();
            m_client->write("HTTP/1.1 200 Connection Established\r\n\r\n");
            m_upstream->write(m_header);
            m_header.clear();
            pump(m_client, m_upstream);
        });
        connect(m_upstream, &QTcpSocket::readyRead, this, [this] { pump(m_upstream, m_client); });
        connect(client, &QTcpSocket::bytesWritten, this, [this] { if (m_connected) pump(m_upstream, m_client); });
        connect(m_upstream, &QTcpSocket::bytesWritten, this, [this] { if (m_connected) pump(m_client, m_upstream); });
    }
private:
    void pump(QTcpSocket *from, QTcpSocket *to) {
        const qint64 room = 128 * 1024 - to->bytesToWrite();
        if (room > 0 && from->bytesAvailable() > 0) to->write(from->read(room));
    }
    void reject() {
        m_deadline->stop();
        if (!m_connected) m_client->write("HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        m_client->disconnectFromHost();
        m_upstream->abort();
        // Also handles a peer which never completes its CONNECT request.
        QTimer::singleShot(1000, this, &QObject::deleteLater);
    }
    void resolve(const QString &host) {
        QUrl url("https://1.1.1.1/dns-query");
        QUrlQuery query; query.addQueryItem("name", host); query.addQueryItem("type", "A"); url.setQuery(query);
        QNetworkRequest request(url);
        request.setRawHeader("Accept", "application/dns-json");
        request.setTransferTimeout(8000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        auto *reply = m_dns->get(request);
        connect(this, &QObject::destroyed, reply, [reply] { reply->abort(); reply->deleteLater(); });
        connect(reply, &QNetworkReply::readyRead, this, [reply] { if (reply->bytesAvailable() > 65536) reply->abort(); });
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) { reject(); return; }
            const auto doc = QJsonDocument::fromJson(reply->readAll()).object();
            if (doc.value("Status").toInt(-1) != 0) { reject(); return; }
            for (const auto &entry : doc.value("Answer").toArray()) {
                const auto record = entry.toObject();
                QHostAddress address(record.value("data").toString());
                const auto ip = address.toIPv4Address();
                // Never let DNS turn this loopback listener into a LAN relay.
                if (record.value("type").toInt() != 1 || address.protocol() != QAbstractSocket::IPv4Protocol
                    || address.isLoopback() || address.isMulticast() || (ip >> 24) == 0 || (ip >> 24) == 10
                    || (ip >> 24) == 127 || (ip >> 16) == 0xa9fe || (ip >> 16) == 0xc0a8
                    || (ip >> 20) == 0xac1 || (ip >> 22) == 0x191 || ip >= 0xe0000000
                    || (ip & 0xffffff00) == 0xc0000000 || (ip & 0xffffff00) == 0xc0000200
                    || (ip & 0xffffff00) == 0xc0586300 || (ip & 0xfffe0000) == 0xc6120000
                    || (ip & 0xffffff00) == 0xc6336400 || (ip & 0xffffff00) == 0xcb007100) continue;
                m_upstream->connectToHost(address, 443);
                return;
            }
            reject();
        });
    }
    QTcpSocket *m_client;
    QTcpSocket *m_upstream;
    QNetworkAccessManager *m_dns;
    QTimer *m_deadline;
    QByteArray m_header;
    bool m_resolving = false;
    bool m_connected = false;
};
}

DnsRoute::DnsRoute(QObject *parent) : QObject(parent), m_port(std::make_shared<std::atomic<quint16>>(0)) {
    m_worker = new QObject;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName("SangeDns");
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, [this] {
        auto *server = new QTcpServer(m_worker);
        auto *dns = new QNetworkAccessManager(m_worker);
        dns->setProxy(QNetworkProxy::NoProxy);
        connect(server, &QTcpServer::newConnection, m_worker, [server, dns, this] {
            while (server->hasPendingConnections()) {
                auto *client = server->nextPendingConnection();
                if (!m_port->load() || server->parent()->findChildren<QObject *>("dnsTunnel", Qt::FindDirectChildrenOnly).size() >= 64) {
                    client->abort(); client->deleteLater(); continue;
                }
                new Tunnel(client, dns, m_worker);
            }
        });
        if (server->listen(QHostAddress::LocalHost)) m_listenPort = server->serverPort();
    }, Qt::BlockingQueuedConnection);
    QNetworkProxyFactory::setApplicationProxyFactory(new RouteFactory(m_port));
    if (QSettings().value("network/secureDns", false).toBool()) setEnabled(true);
}
DnsRoute::~DnsRoute() {
    m_port->store(0);
    m_thread.quit();
    m_thread.wait();
}
void DnsRoute::setEnabled(bool enabled) {
    m_error.clear();
    if (enabled && !m_listenPort) { m_error = tr("Не удалось включить DNS. Перезапустите Sange."); emit changed(); return; }
    m_enabled = enabled;
    m_port->store(enabled ? m_listenPort : 0);
    if (!enabled) QMetaObject::invokeMethod(m_worker, [this] {
        const auto tunnels = m_worker->findChildren<QObject *>("dnsTunnel", Qt::FindDirectChildrenOnly);
        qDeleteAll(tunnels);
    }, Qt::BlockingQueuedConnection);
    QSettings().setValue("network/secureDns", enabled);
    emit changed();
}
