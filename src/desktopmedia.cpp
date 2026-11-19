#include "desktopmedia.h"
#include "soundcloud.h"
#include <QWindow>
#include <QTimer>
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QImage>
#include <QImageReader>
#include <QBuffer>
#include <QFile>
QVariantMap DesktopMedia::snapshot() const {
    const auto state = m_controller->state();
    const auto track = state.value("current").toMap();
    const bool hasTrack = !track.isEmpty();
    QString id = track.value("urn", track.value("id")).toString();
    id = hasTrack ? "/org/mpris/MediaPlayer2/track/" + QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex())
                  : "/org/mpris/MediaPlayer2/TrackList/NoTrack";
    QUrl artwork;
#ifdef Q_OS_LINUX
    artwork = QUrl(m_artworkFile);
#else
    QString cover = track.value("artwork_url").toString();
    if (cover.isEmpty()) cover = track.value("user").toMap().value("avatar_url").toString();
    artwork = QUrl(cover);
    // Publish only unsigned public artwork, never arbitrary metadata URLs or streams.
    if (artwork.scheme() != "https" || !artwork.userInfo().isEmpty() || artwork.hasQuery()
        || !(artwork.host() == "sndcdn.com" || artwork.host().endsWith(".sndcdn.com"))) artwork = QUrl{};
#endif
    return {{"trackId", id}, {"title", track.value("title")}, {"artist", TrackMetadata::displayArtist(track)},
        {"artwork", artwork.toString()}, {"duration", hasTrack ? qMax(m_controller->duration(), track.value("duration").toLongLong()) : 0},
        {"position", hasTrack ? m_controller->position() : 0}, {"hasTrack", hasTrack}, {"canPlay", m_controller->canResumePlayback()},
        {"status", state.value("playbackActive").toBool() ? "Playing" : hasTrack ? "Paused" : "Stopped"},
        {"canNext", m_controller->canGoNext()}, {"canPrevious", m_controller->canGoPrevious()},
        {"canSeek", hasTrack && m_controller->seekable()}};
}
void DesktopMedia::command(const QString &action, qint64 position, const QString &track) {
    const auto s = snapshot();
    if ((action == "next" && s.value("canNext").toBool()) || (action == "previous" && s.value("canPrevious").toBool())) {
        const bool active = m_controller->state().value("playbackActive").toBool();
        if (action == "next") m_controller->next(); else m_controller->previous();
        if (!active) m_controller->pausePlayback();
    }
    else if (action == "play" && s.value("canPlay").toBool()) m_controller->resumePlayback();
    else if (action == "toggle" && s.value("canPlay").toBool()) m_controller->togglePlayback();
    else if (action == "stop") m_controller->stopPlayback();
    else if (s.value("hasTrack").toBool()) {
        if (action == "pause") m_controller->pausePlayback();
        else if (action == "seek" && s.value("canSeek").toBool()
                 && (track.isEmpty() || track == s.value("trackId").toString()) && position >= 0
                 && position <= s.value("duration").toLongLong()) m_controller->seek(position);
    }
}
void DesktopMedia::raise() { if (m_window) { m_window->showNormal(); m_window->raise(); m_window->requestActivate(); } }

void DesktopMedia::setArtworkNetwork(QNetworkAccessManager *network) {
    if (m_artworkNetwork) return;
    m_artworkNetwork = network;
    connect(m_controller, &SoundCloud::catalogChanged, this, &DesktopMedia::updateArtwork);
    updateArtwork();
}
void DesktopMedia::updateArtwork() {
#ifdef Q_OS_LINUX
    const auto track = m_controller->state().value("current").toMap();
    QString source = track.value("artwork_url").toString();
    if (source.isEmpty()) source = track.value("user").toMap().value("avatar_url").toString();
    QUrl url(source);
    if (url.scheme() != "https" || !url.userInfo().isEmpty() || url.hasQuery()
        || url.port(443) != 443
        || !(url.host() == "sndcdn.com" || url.host().endsWith(".sndcdn.com"))) source.clear();
    if (source == m_artworkSource) return;
    m_artworkSource = source;
    if (m_artworkReply) m_artworkReply->abort();
    m_artworkFile.clear();
    emit updated();
    if (source.isEmpty() || !m_artworkDirectory.isValid()) return;
    url = QUrl(source.replace("-large.", "-t500x500."));
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_artworkNetwork->get(request);
    m_artworkReply = reply;
    constexpr qint64 limit = 8 * 1024 * 1024;
    reply->setReadBufferSize(limit + 1);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > limit) reply->abort();
    });
    const QString expected = m_artworkSource;
    connect(reply, &QNetworkReply::finished, this, [this, reply, expected] {
        reply->deleteLater();
        if (expected != m_artworkSource || reply->error() != QNetworkReply::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200
            || reply->bytesAvailable() > limit) return;
        QByteArray bytes = reply->readAll();
        QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const QSize size = reader.size();
        if (!size.isValid() || size.width() > 4096 || size.height() > 4096) return;
        const QImage image = reader.read();
        if (image.isNull()) return;
        const QString path = m_artworkDirectory.filePath(QString::fromLatin1(
            QCryptographicHash::hash(expected.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".png");
        if (!image.save(path, "PNG")) return;
        m_artworkFile = QUrl::fromLocalFile(path).toString();
        emit updated();
    });
#endif
}
