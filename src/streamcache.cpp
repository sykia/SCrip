#include "streamcache.h"
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QUuid>

namespace {
constexpr qint64 maximumObject = 8 * 1024 * 1024;
void respond(QTcpSocket *socket, int status, const QByteArray &body, const QByteArray &type = "application/octet-stream", const QByteArray &extra = {}, bool head = false) {
    if (!socket) return;
    socket->write("HTTP/1.1 " + QByteArray::number(status) + " OK\r\nConnection: close\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n" + extra + "\r\n");
    if (!head) socket->write(body);
    socket->disconnectFromHost();
}
}
StreamCache::Entry::~Entry() { QFile::remove(file); }
StreamCache::StreamCache(QObject *parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &StreamCache::next);
    connect(&m_server, &QTcpServer::newConnection, this, &StreamCache::accept);
    m_server.listen(QHostAddress::LocalHost);
}
StreamCache::~StreamCache() {
    m_timer.stop();
    for (auto *reply : m_network.findChildren<QNetworkReply *>()) disconnect(reply, nullptr, this, nullptr);
    for (auto *socket : m_server.findChildren<QTcpSocket *>()) disconnect(socket, nullptr, this, nullptr);
}
QUrl StreamCache::map(const QUrl &remote, const QString &root) {
    if ((remote.scheme() != "https" && !(remote.scheme() == "http" && remote.host() == "127.0.0.1")) || !remote.userInfo().isEmpty()) return remote;
    const QString identity = root + '\n' + remote.toString(QUrl::FullyEncoded);
    QString key = m_keys.value(identity);
    if (key.isEmpty()) {
        key = QUuid::createUuid().toString(QUuid::Id128);
        const auto suffix = QFileInfo(remote.path()).suffix();
        if (QRegularExpression(QStringLiteral("^[a-zA-Z0-9]{1,8}$")).match(suffix).hasMatch()) key += '.' + suffix;
        m_keys.insert(identity, key);
        m_resources.insert(key, {remote, root});
    }
    QUrl local(QStringLiteral("http://127.0.0.1:%1/%2").arg(m_server.serverPort()).arg(key));
    return local;
}
QUrl StreamCache::source(const QUrl &remote) {
    if (!m_server.isListening() || !m_directory.isValid()) return remote;
    const QString root = remote.toString(QUrl::FullyEncoded);
    m_roots.removeAll(root); m_roots.append(root);
    // Текущий, следующий и недавно использованные источники сохраняют свои адреса.
    while (m_roots.size() > 4) {
        const QString expired = m_roots.takeFirst();
        m_children.remove(expired);
        for (auto it = m_resources.begin(); it != m_resources.end();) {
            if (it->root != expired) { ++it; continue; }
            m_keys.remove(expired + '\n' + it->remote.toString(QUrl::FullyEncoded));
            m_cache.remove(it.key()); it = m_resources.erase(it);
        }
    }
    return map(remote, root);
}
void StreamCache::suspend() {
    m_timer.stop(); m_root.clear(); m_pending.clear();
    if (m_background) { auto reply = m_background; m_background.clear(); reply->abort(); }
}
void StreamCache::warm(const QUrl &local) {
    const auto resource = m_resources.value(local.path().mid(1));
    if (resource.root.isEmpty()) return;
    if (m_root == resource.root) return;
    suspend(); m_root = resource.root;
    m_pending = m_children.value(m_root);
    m_timer.start(2000); // Запуск и подготовка следующего трека получают фору.
}
QByteArray StreamCache::playlist(const QByteArray &body, const Resource &resource) {
    QByteArray result;
    QStringList children;
    const QRegularExpression uri(QStringLiteral("URI=\"([^\"]+)\""));
    for (QByteArray line : body.split('\n')) {
        line = line.trimmed();
        if (!line.isEmpty() && !line.startsWith('#')) {
            const QUrl local = map(resource.remote.resolved(QUrl(QString::fromUtf8(line))), resource.root);
            children.append(local.path().mid(1)); line = local.toEncoded();
        } else if (line.startsWith('#')) {
            QString text = QString::fromUtf8(line);
            auto matches = uri.globalMatch(text);
            QList<QRegularExpressionMatch> found;
            while (matches.hasNext()) found.append(matches.next());
            for (auto it = found.crbegin(); it != found.crend(); ++it) {
                const QUrl local = map(resource.remote.resolved(QUrl(it->captured(1))), resource.root);
                children.prepend(local.path().mid(1));
                text.replace(it->capturedStart(1), it->capturedLength(1), local.toString());
            }
            line = text.toUtf8();
        }
        result += line + '\n';
    }
    // Полное скачивание разрешено только для конечного плейлиста (не live).
    if (body.contains("#EXT-X-ENDLIST")) {
        auto &all = m_children[resource.root];
        for (const auto &key : children) if (!all.contains(key)) all.append(key);
        if (m_root == resource.root) {
            for (const auto &key : children) if (!m_pending.contains(key)) m_pending.append(key);
            if (!m_timer.isActive() && !m_background) m_timer.start(2000);
        }
    }
    return result;
}
void StreamCache::accept() {
    while (auto *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto input = std::make_shared<QByteArray>();
        auto *deadline = new QTimer(socket); deadline->setSingleShot(true);
        connect(deadline, &QTimer::timeout, socket, &QTcpSocket::disconnectFromHost);
        deadline->start(20000);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, input, deadline] {
            *input += socket->readAll();
            if (input->size() > 16384) { socket->disconnectFromHost(); return; }
            if (!input->contains("\r\n\r\n")) return;
            deadline->stop();
            disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
            const auto lines = input->split('\n');
            const auto request = lines.first().trimmed().split(' ');
            if (request.size() != 3 || (request[0] != "GET" && request[0] != "HEAD")) { respond(socket, 405, {}); return; }
            const QString key = QString::fromLatin1(request[1].mid(1));
            if (!m_resources.contains(key)) { respond(socket, 404, {}); return; }
            QByteArray range;
            for (const auto &line : lines) if (line.toLower().startsWith("range:")) range = line.mid(6).trimmed();
            fetch(key, socket, range, request[0] == "HEAD");
        });
    }
}
void StreamCache::fetch(const QString &key, QPointer<QTcpSocket> socket, QByteArray range, bool head) {
    if (auto *entry = m_cache.object(key)) {
        QFile file(entry->file);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray body = file.readAll(), extra = "Accept-Ranges: bytes\r\n";
            int status = 200;
            if (!range.isEmpty()) {
                const auto match = QRegularExpression(QStringLiteral("^bytes=(\\d*)-(\\d*)$")).match(QString::fromLatin1(range));
                if (!match.hasMatch() || (match.captured(1).isEmpty() && match.captured(2).isEmpty())) { respond(socket, 416, {}); return; }
                const qint64 size = body.size();
                qint64 start = match.captured(1).isEmpty() ? qMax<qint64>(0, size - match.captured(2).toLongLong()) : match.captured(1).toLongLong();
                qint64 end = match.captured(1).isEmpty() || match.captured(2).isEmpty() ? size - 1 : qMin(size - 1, match.captured(2).toLongLong());
                if (start > end || start >= size) { respond(socket, 416, {}, {}, "Content-Range: bytes */" + QByteArray::number(size) + "\r\n"); return; }
                extra += "Content-Range: bytes " + QByteArray::number(start) + '-' + QByteArray::number(end) + '/' + QByteArray::number(size) + "\r\n";
                body = body.mid(start, end - start + 1); status = 206;
            }
            respond(socket, status, body, entry->type, extra, head); return;
        }
        m_cache.remove(key);
    }
    const bool background = !socket;
    if (!background) ++m_foreground;
    if (!background && m_background) {
        auto reply = m_background; m_background.clear();
        const auto interrupted = reply->property("cacheKey").toString();
        if (!m_root.isEmpty() && !m_pending.contains(interrupted)) m_pending.prepend(interrupted);
        reply->abort();
    }
    QNetworkRequest request(m_resources.value(key).remote);
    request.setRawHeader("Accept-Encoding", "identity");
    request.setTransferTimeout(background ? 5000 : 20000);
    request.setPriority(background ? QNetworkRequest::LowPriority : QNetworkRequest::HighPriority);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!range.isEmpty()) request.setRawHeader("Range", range);
    auto *reply = head ? m_network.head(request) : m_network.get(request);
    if (background) { m_background = reply; reply->setProperty("cacheKey", key); }
    else if (socket) connect(socket, &QTcpSocket::disconnected, reply, &QNetworkReply::abort);
    struct Download { QByteArray body; bool headers = false; bool manifest = false; bool tooLarge = false; };
    auto state = std::make_shared<Download>();
    auto drain = [=] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status < 200 || status >= 300) return;
        const QByteArray type = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
        state->manifest = type.contains("mpegurl") || reply->url().path().endsWith(".m3u8");
        if (!state->manifest && socket && !state->headers) {
            state->headers = true;
            QByteArray headers = "HTTP/1.0 " + QByteArray::number(status) + " OK\r\nConnection: close\r\nContent-Type: " + type + "\r\n";
            for (const auto &name : {QByteArray("Content-Length"), QByteArray("Content-Range"), QByteArray("Accept-Ranges")})
                if (reply->hasRawHeader(name)) headers += name + ": " + reply->rawHeader(name) + "\r\n";
            socket->write(headers + "\r\n");
        }
        const QByteArray chunk = reply->readAll();
        if (!state->manifest && socket && !head) socket->write(chunk);
        if (!state->tooLarge) {
            state->body += chunk;
            if (state->body.size() > maximumObject) { state->body.clear(); state->tooLarge = true; if (background || state->manifest) reply->abort(); }
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, drain);
    connect(reply, &QNetworkReply::finished, this, [=, this] {
        drain();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        QByteArray type = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
        if (ok && state->manifest && !state->tooLarge) { state->body = playlist(state->body, {reply->url(), m_resources.value(key).root}); type = "application/vnd.apple.mpegurl"; }
        if (ok && !head && range.isEmpty() && status == 200 && !state->tooLarge) {
            const QString path = m_directory.filePath(QString::number(++m_serial));
            QFile file(path);
            if (file.open(QIODevice::WriteOnly) && file.write(state->body) == state->body.size()) {
                file.close(); m_cache.insert(key, new Entry{path, type}, qMax(1, int(state->body.size())));
            } else { file.close(); QFile::remove(path); }
        }
        if (socket) {
            if (!state->headers) respond(socket, ok ? status : 502, ok ? state->body : QByteArray{}, type, {}, head);
            else socket->disconnectFromHost();
        }
        if (m_background == reply) m_background.clear();
        reply->deleteLater();
        if (!background) --m_foreground;
        if (!m_root.isEmpty() && !m_timer.isActive()) m_timer.start(500);
    });
}
void StreamCache::next() {
    if (m_root.isEmpty() || m_background) return;
    if (m_foreground > 0) { m_timer.start(500); return; }
    while (!m_pending.isEmpty()) {
        const QString key = m_pending.takeFirst();
        if (m_resources.value(key).root != m_root || m_cache.contains(key)) continue;
        fetch(key); return;
    }
}
