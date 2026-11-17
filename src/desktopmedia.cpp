#include "desktopmedia.h"
#include "soundcloud.h"
#include <QWindow>
#include <QTimer>
#include <QCryptographicHash>
QVariantMap DesktopMedia::snapshot() const {
    const auto state = m_controller->state();
    const auto track = state.value("current").toMap();
    const bool hasTrack = !track.isEmpty();
    QString id = track.value("urn", track.value("id")).toString();
    id = hasTrack ? "/org/mpris/MediaPlayer2/track/" + QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex())
                  : "/org/mpris/MediaPlayer2/TrackList/NoTrack";
    QString cover = track.value("artwork_url").toString();
    if (cover.isEmpty()) cover = track.value("user").toMap().value("avatar_url").toString();
    QUrl artwork(cover);
    // Publish only unsigned public artwork, never arbitrary metadata URLs or streams.
    if (artwork.scheme() != "https" || !artwork.userInfo().isEmpty() || artwork.hasQuery()
        || !(artwork.host() == "sndcdn.com" || artwork.host().endsWith(".sndcdn.com"))) artwork = QUrl{};
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
