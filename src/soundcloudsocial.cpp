#include "soundcloud.h"
#include <QJsonArray>
#include <QUrl>
#include <QDesktopServices>
#include <algorithm>
#include <limits>

namespace {
QString pathFor(const QString &kind, const QString &id) {
    return "/" + kind + "/" + QString::fromLatin1(QUrl::toPercentEncoding(id));
}
QVariantList entries(const QJsonDocument &doc) {
    const auto array = doc.isArray() ? doc.array() : doc.object().value("collection").toArray();
    QVariantList list;
    for (const auto &value : array) {
        auto object = value.toObject();
        for (const auto &key : {"origin", "track", "playlist", "user"}) {
            // A user field on a track/playlist is its owner, not a collection wrapper.
            if (!object.contains("urn") && object.value(key).isObject()) object = object.value(key).toObject();
        }
        if (!object.isEmpty()) list.append(object.toVariantMap());
    }
    return list;
}
QString engagementPath(const QString &kind) {
    if (kind == "playlistLikes") return "/me/likes/playlists";
    if (kind == "stationLikes") return "/me/likes/system-playlists";
    if (kind == "trackReposts") return "/me/reposts/tracks";
    if (kind == "playlistReposts") return "/me/reposts/playlists";
    return {};
}
QVariantList trackIds(const QVariantMap &playlist) {
    QVariantList ids;
    for (const auto &track : playlist.value("tracks").toList()) ids.append(track.toMap().value("urn"));
    return ids;
}
bool samePlaylist(const QVariantMap &a, const QVariantMap &b) {
    for (const auto &key : {"title", "description", "sharing", "last_modified"})
        if (a.value(key) != b.value(key)) return false;
    return trackIds(a) == trackIds(b);
}
}

void SoundCloud::resetSocialState() {
    ++m_engagementRevision;
    set("engagementCounts", QVariantMap{}); set("engagementStates", QVariantMap{});
    ++m_playlistOperationGeneration;
    m_playlistWriteBusy = false;
    set("queuePlaylistBusy", false); set("queuePlaylistError", "");
    set("savingTrack", false);
    set("loadingDestinations", false); set("destinations", QVariantList{}); set("destinationNext", "");
    ++m_peopleGeneration; ++m_linksGeneration; ++m_editGeneration; ++m_linkGeneration;
    for (auto it = m_engagementGeneration.begin(); it != m_engagementGeneration.end(); ++it) ++it.value();
    m_peoplePages.clear(); m_linkPages.clear(); m_engagementPages.clear();
    m_peopleNext.clear(); m_linksNext.clear(); m_editOriginal.clear(); m_playlistEditModel.update({});
    m_followPages.clear(); set("followings", QVariantMap{}); set("followingPending", QVariantMap{});
    set("followingsReady", false); set("followingsLoading", false);
    for (const auto &key : {"engagement", "engagementReady", "engagementLoading", "engagementPending", "engagementErrors", "playlistDraft"}) set(key, QVariantMap{});
    for (const auto &key : {"people", "profileLinks"}) set(key, QVariantList{});
    for (const auto &key : {"peopleBusy", "peopleHasMore", "profileLinksBusy", "profileLinksHasMore", "playlistEditBusy", "linkBusy"}) set(key, false);
    set("playlistEditError", ""); set("peopleError", ""); set("profileLinksError", "");
}

void SoundCloud::closePeople() {
    ++m_peopleGeneration;
    m_peopleNext.clear(); m_peoplePages.clear();
    set("peopleBusy", false); set("peopleHasMore", false);
}

// Ingest once per physical GET, before distributing coalesced callbacks. Cached
// objects keep their original provenance and are never ingested a second time.
void SoundCloud::acceptEngagementCounts(const QJsonDocument &doc) {
    auto counts = m_state.value("engagementCounts").toMap();
    std::function<void(const QJsonValue &)> visit = [&](const QJsonValue &value) {
        if (value.isArray()) {
            for (const auto &child : value.toArray()) visit(child);
        } else if (value.isObject()) {
            const auto object = value.toObject();
            const auto id = object.value("urn").toString();
            if (counts.contains(id) && object.value("reveal_stats").toBool(true)) {
                auto fields = counts.value(id).toMap();
                for (auto it = fields.begin(); it != fields.end(); ++it) {
                    const auto number = object.value(it.key());
                    if (number.isDouble() && number.toDouble() >= 0)
                        it.value() = number.toVariant();
                }
                counts[id] = fields;
            }
            for (const auto &child : object) visit(child);
        }
    };
    if (doc.isArray()) visit(doc.array()); else visit(doc.object());
    if (counts != m_state.value("engagementCounts").toMap()) {
        set("engagementCounts", counts);
        refreshLikeStates();
    }
}

void SoundCloud::confirmEngagementCount(const QVariantMap &item, bool repost, bool wasSelected) {
    const auto id = urn(item);
    auto states = m_state.value("engagementStates").toMap();
    auto own = states.value(id).toMap(); own[repost ? "repost" : "like"] = !wasSelected;
    states[id] = own; set("engagementStates", states);
    const QString field = repost ? "reposts_count" : id.startsWith("soundcloud:tracks:") ? "favoritings_count" : "likes_count";
    auto counts = m_state.value("engagementCounts").toMap();
    auto values = counts.value(id).toMap();
    const auto before = values.contains(field) ? values.value(field) : item.value(field);
    bool ok = false;
    const auto count = before.toLongLong(&ok);
    if (ok && count >= 0 && item.value("reveal_stats", true).toBool()) {
        values[field] = qMax<qint64>(0, wasSelected ? count - 1 : count == std::numeric_limits<qint64>::max() ? count : count + 1);
        counts[id] = values; set("engagementCounts", counts);
        refreshLikeStates();
    }
}

void SoundCloud::loadPeople(const QVariantMap &user, const QString &kind, bool append) {
    const auto id = urn(user);
    const bool track = id.startsWith("soundcloud:tracks:");
    const bool playlist = id.startsWith("soundcloud:playlists:");
    const bool engagement = (track && kind == "favoriters") || ((track || playlist) && kind == "reposters");
    if (!engagement && (!id.startsWith("soundcloud:users:") || (kind != "followers" && kind != "followings" && kind != "related"))) return;
    const auto identity = id + ":" + kind;
    if (append && m_state.value("peopleIdentity").toString() != identity) return;
    if (append && (m_state.value("peopleBusy").toBool() || m_peopleNext.isEmpty())) return;
    const int generation = append ? m_peopleGeneration : ++m_peopleGeneration;
    const auto path = append ? m_peopleNext : pathFor(track ? "tracks" : playlist ? "playlists" : "users", id) + "/" + kind + "?limit=50" + (kind == "reposters" ? "" : "&linked_partitioning=true");
    if (!append) { set("peopleIdentity", identity); set("peopleHasMore", false); m_peoplePages.clear(); m_peopleNext.clear(); set("people", QVariantList{}); }
    if (m_peoplePages.contains(path)) { set("peopleHasMore", false); return; }
    set("peopleBusy", true); set("peopleError", "");
    request(path, [this, generation, path, append](QJsonDocument doc) {
        if (generation != m_peopleGeneration) return;
        if (!doc.isArray() && !doc.object().value("collection").isArray()) {
            set("peopleError", "SoundCloud вернул некорректный список пользователей."); set("peopleBusy", false); return;
        }
        m_peoplePages.insert(path);
        auto people = append ? m_state.value("people").toList() : QVariantList{};
        QSet<QString> seen;
        for (const auto &p : people) seen.insert(urn(p.toMap()));
        for (const auto &p : entries(doc)) {
            const auto id = urn(p.toMap());
            if (id.startsWith("soundcloud:users:") && !seen.contains(id)) { seen.insert(id); people.append(p); }
        }
        m_peopleNext = doc.object().value("next_href").toString();
        if (m_peoplePages.contains(m_peopleNext)) m_peopleNext.clear();
        set("people", people); set("peopleHasMore", !m_peopleNext.isEmpty()); set("peopleBusy", false);
    }, "GET", {}, true, [this, generation](QString error) {
        if (generation != m_peopleGeneration) return;
        set("peopleError", error); set("peopleBusy", false);
    });
}
void SoundCloud::loadProfileLinks(const QVariantMap &user, bool append) {
    const auto id = urn(user);
    if (!id.startsWith("soundcloud:users:")) return;
    if (append && (m_state.value("profileLinksBusy").toBool() || m_linksNext.isEmpty())) return;
    const int generation = append ? m_linksGeneration : ++m_linksGeneration;
    const auto path = append ? m_linksNext : pathFor("users", id) + "/web-profiles?limit=50&linked_partitioning=true";
    if (!append) { m_linkPages.clear(); m_linksNext.clear(); set("profileLinks", QVariantList{}); set("profileLinksUser", id); }
    if (m_linkPages.contains(path)) { set("profileLinksHasMore", false); return; }
    set("profileLinksBusy", true); set("profileLinksError", "");
    request(path, [this, generation, path, append](QJsonDocument doc) {
        if (generation != m_linksGeneration) return;
        m_linkPages.insert(path);
        auto links = append ? m_state.value("profileLinks").toList() : QVariantList{};
        QSet<QString> seen;
        for (const auto &link : links) seen.insert(link.toMap().value("url").toString());
        for (const auto &entry : entries(doc)) {
            const auto link = entry.toMap();
            const auto url = QUrl(link.value("url").toString());
            if ((url.scheme() == "https" || url.scheme() == "http") && !url.host().isEmpty() && url.userInfo().isEmpty() && !seen.contains(url.toString())) {
                seen.insert(url.toString()); links.append(link);
            }
        }
        m_linksNext = doc.object().value("next_href").toString();
        if (m_linkPages.contains(m_linksNext)) m_linksNext.clear();
        set("profileLinks", links); set("profileLinksHasMore", !m_linksNext.isEmpty()); set("profileLinksBusy", false);
    }, "GET", {}, true, [this, generation](QString error) {
        if (generation != m_linksGeneration) return;
        set("profileLinksError", error); set("profileLinksBusy", false);
    });
}

void SoundCloud::loadEngagement(const QString &kind) {
    const auto path = engagementPath(kind);
    if (!signedIn() || path.isEmpty() || m_state.value("engagementReady").toMap().value(kind).toBool() || m_state.value("engagementLoading").toMap().value(kind).toBool()) return;
    auto loading = m_state.value("engagementLoading").toMap(); loading[kind] = true; set("engagementLoading", loading);
    auto errors = m_state.value("engagementErrors").toMap(); errors.remove(kind); set("engagementErrors", errors);
    m_engagementPages[kind].clear();
    auto state = m_state.value("engagement").toMap(); state[kind] = QVariantMap{}; set("engagement", state);
    loadEngagementPage(kind, path + "?limit=200&linked_partitioning=true", ++m_engagementGeneration[kind]);
}
void SoundCloud::loadEngagementPage(const QString &kind, const QString &path, int generation) {
    if (m_engagementPages[kind].contains(path)) {
        auto loading = m_state.value("engagementLoading").toMap(); loading[kind] = false; set("engagementLoading", loading);
        auto errors = m_state.value("engagementErrors").toMap(); errors[kind] = "Не удалось прочитать все страницы. Повторите загрузку."; set("engagementErrors", errors);
        return;
    }
    m_engagementPages[kind].insert(path);
    request(path, [this, kind, generation](QJsonDocument doc) {
        if (m_engagementGeneration.value(kind) != generation) return;
        if (!doc.isArray() && !doc.object().value("collection").isArray()) {
            auto loading = m_state.value("engagementLoading").toMap(); loading[kind] = false; set("engagementLoading", loading);
            auto errors = m_state.value("engagementErrors").toMap(); errors[kind] = "SoundCloud вернул неполный ответ. Повторите загрузку."; set("engagementErrors", errors);
            return;
        }
        auto state = m_state.value("engagement").toMap(); auto known = state.value(kind).toMap();
        for (const auto &entry : entries(doc)) { const auto id = urn(entry.toMap()); if (!id.isEmpty()) known[id] = true; }
        state[kind] = known; set("engagement", state);
        const auto next = doc.object().value("next_href").toString();
        if (!next.isEmpty()) { loadEngagementPage(kind, next, generation); return; }
        auto ready = m_state.value("engagementReady").toMap(); ready[kind] = true; set("engagementReady", ready);
        auto loading = m_state.value("engagementLoading").toMap(); loading[kind] = false; set("engagementLoading", loading);
    }, "GET", {}, true, [this, kind, generation](QString error) {
        if (m_engagementGeneration.value(kind) != generation) return;
        auto loading = m_state.value("engagementLoading").toMap(); loading[kind] = false; set("engagementLoading", loading);
        auto errors = m_state.value("engagementErrors").toMap(); errors[kind] = error; set("engagementErrors", errors);
    });
}
void SoundCloud::likeCollection(const QVariantMap &item) {
    if (!signedIn()) { fail("Войдите в SoundCloud, чтобы сохранять подборки."); return; }
    const auto id = urn(item);
    const auto kind = id.startsWith("soundcloud:system-playlists:") ? QString("stationLikes") : QString("playlistLikes");
    if (!id.startsWith("soundcloud:system-playlists:") && !id.startsWith("soundcloud:playlists:")) return;
    if (!m_state.value("engagementReady").toMap().value(kind).toBool()) { loadEngagement(kind); return; }
    auto pending = m_state.value("engagementPending").toMap(); const auto key = kind + ":" + id;
    if (pending.value(key).toBool()) return;
    const bool liked = m_state.value("engagement").toMap().value(kind).toMap().value(id).toBool();
    pending[key] = true; set("engagementPending", pending); clearError();
    const int generation = m_playlistOperationGeneration;
    request(pathFor("likes/playlists", id), [this, kind, id, key, liked, item, generation](QJsonDocument) {
        if (generation != m_playlistOperationGeneration) return;
        confirmEngagementCount(item, false, liked);
        auto state = m_state.value("engagement").toMap(); auto known = state.value(kind).toMap(); known[id] = !liked; state[kind] = known; set("engagement", state);
        auto pending = m_state.value("engagementPending").toMap(); pending.remove(key); set("engagementPending", pending);
        m_listings.clear();
        if (m_lastPath.startsWith(engagementPath(kind))) browse(kind == "stationLikes" ? "likedStations" : "likedPlaylists", {}, true);
    }, liked ? "DELETE" : "POST", {}, true, [this, key, generation](QString error) {
        if (generation != m_playlistOperationGeneration) return;
        auto pending = m_state.value("engagementPending").toMap(); pending.remove(key); set("engagementPending", pending); fail(error);
    });
}
void SoundCloud::toggleRepost(const QVariantMap &item) {
    if (!signedIn()) { fail("Войдите в SoundCloud, чтобы делать репосты."); return; }
    const auto id = urn(item);
    const bool track = id.startsWith("soundcloud:tracks:");
    if (!track && !id.startsWith("soundcloud:playlists:")) return;
    const auto kind = track ? QString("trackReposts") : QString("playlistReposts");
    if (!m_state.value("engagementReady").toMap().value(kind).toBool()) { loadEngagement(kind); return; }
    auto pending = m_state.value("engagementPending").toMap(); const auto key = kind + ":" + id;
    if (pending.value(key).toBool()) return;
    const bool reposted = m_state.value("engagement").toMap().value(kind).toMap().value(id).toBool();
    pending[key] = true; set("engagementPending", pending); clearError();
    const int generation = m_playlistOperationGeneration;
    request(pathFor(track ? "reposts/tracks" : "reposts/playlists", id), [this, kind, id, key, reposted, item, generation, track](QJsonDocument) {
        if (generation != m_playlistOperationGeneration) return;
        confirmEngagementCount(item, true, reposted);
        auto state = m_state.value("engagement").toMap(); auto known = state.value(kind).toMap(); known[id] = !reposted; state[kind] = known; set("engagement", state);
        auto pending = m_state.value("engagementPending").toMap(); pending.remove(key); set("engagementPending", pending);
        m_listings.clear(); m_homeLoaded = false;
        if (m_lastPath.startsWith(engagementPath(kind))) browse(track ? "reposts" : "repostedPlaylists", {}, true);
    }, reposted ? "DELETE" : "POST", {}, true, [this, key, generation](QString error) {
        if (generation != m_playlistOperationGeneration) return;
        auto pending = m_state.value("engagementPending").toMap(); pending.remove(key); set("engagementPending", pending); fail(error);
    });
}

void SoundCloud::fetchPlaylistSnapshot(const QString &id, std::function<void(QVariantMap)> done, TokenFailure failed) {
    const int generation = m_playlistOperationGeneration;
    request(pathFor("playlists", id), [this, id, done, failed, generation](QJsonDocument doc) {
        if (generation != m_playlistOperationGeneration) return;
        const auto count = doc.object().value("track_count");
        if (!count.isDouble() || count.toDouble() < 0 || count.toDouble() != count.toInt(-1)) {
            failed("SoundCloud вернул неполный состав плейлиста."); return;
        }
        auto playlist = doc.object().toVariantMap();
        auto owner = urn(playlist.value("user").toMap());
        if (owner.isEmpty()) owner = playlist.value("user_urn").toString();
        if (urn(playlist) != id || owner.isEmpty() || owner != urn(m_state.value("profile").toMap()) || !signedIn()) {
            failed("Редактировать можно только собственные плейлисты."); return;
        }
        fetchPlaylistTracks(pathFor("playlists", id) + "/tracks?limit=200&linked_partitioning=true", playlist, {}, {}, done, failed);
    }, "GET", {}, true, failed);
}
void SoundCloud::fetchPlaylistTracks(const QString &path, QVariantMap playlist, QVariantList tracks, QSet<QString> pages,
                                    std::function<void(QVariantMap)> done, TokenFailure failed) {
    const QUrl pageUrl(path.startsWith('/') ? "https://api.soundcloud.com" + path : path);
    const QUrl tracksUrl("https://api.soundcloud.com" + pathFor("playlists", urn(playlist)) + "/tracks");
    const auto legacyPath = "/playlists/" + urn(playlist).section(':', -1) + "/tracks";
    if (pageUrl.path() != tracksUrl.path() && pageUrl.path() != legacyPath) { failed("Не удалось прочитать страницы плейлиста."); return; }
    const auto pageKey = pageUrl.toString();
    if (pages.contains(pageKey)) { failed("SoundCloud вернул повторную страницу. Изменения не сохранены."); return; }
    pages.insert(pageKey);
    const int generation = m_playlistOperationGeneration;
    request(path, [this, playlist, tracks, pages, done, failed, generation](QJsonDocument doc) mutable {
        if (generation != m_playlistOperationGeneration) return;
        if (!doc.isArray() && (!doc.object().value("collection").isArray()
            || (doc.object().contains("next_href") && !doc.object().value("next_href").isNull() && !doc.object().value("next_href").isString()))) {
            failed("Не удалось прочитать состав плейлиста."); return;
        }
        const auto array = doc.isArray() ? doc.array() : doc.object().value("collection").toArray();
        for (const auto &value : array) {
            const auto entry = value.toObject().toVariantMap();
            if (!urn(entry).startsWith("soundcloud:tracks:")) { failed("Не удалось прочитать состав плейлиста. Изменения не сохранены."); return; }
            tracks.append(entry);
        }
        const auto next = doc.object().value("next_href").toString();
        if (!next.isEmpty()) { fetchPlaylistTracks(next, playlist, tracks, pages, done, failed); return; }
        if (!playlist.contains("track_count") || playlist.value("track_count").toInt() != tracks.size()) {
            failed("SoundCloud вернул неполный состав плейлиста. Изменения не сохранены."); return;
        }
        playlist["tracks"] = tracks; done(playlist);
    }, "GET", {}, true, failed);
}
void SoundCloud::validatePlaylistSnapshot(const QString &id, const QVariantMap &original,
                                         std::function<void(QVariantMap)> done, TokenFailure failed) {
    fetchPlaylistSnapshot(id, [original, done, failed](QVariantMap latest) {
        if (!samePlaylist(latest, original)) { failed("Плейлист изменился. Откройте его заново."); return; }
        done(latest);
    }, failed);
}
void SoundCloud::editPlaylist(const QVariantMap &item) {
    if (m_state.value("playlistEditBusy").toBool()) return;
    const auto id = urn(item);
    if (!signedIn() || !id.startsWith("soundcloud:playlists:")) { fail("Войдите в SoundCloud и выберите свой плейлист."); return; }
    const int generation = ++m_editGeneration;
    m_editOriginal.clear(); m_playlistEditModel.update({}); set("playlistDraft", QVariantMap{}); set("playlistEditError", ""); set("playlistEditBusy", true);
    fetchPlaylistSnapshot(id, [this, generation](QVariantMap playlist) {
        if (generation != m_editGeneration) return;
        m_editOriginal = playlist; m_playlistEditModel.update(playlist.value("tracks").toList()); set("playlistDraft", playlist); set("playlistEditBusy", false);
    }, [this, generation](QString error) {
        if (generation != m_editGeneration) return;
        set("playlistEditError", error); set("playlistEditBusy", false);
    });
}
void SoundCloud::movePlaylistTrack(int from, int to) {
    if (m_state.value("playlistEditBusy").toBool()) return;
    auto draft = m_state.value("playlistDraft").toMap(); auto tracks = draft.value("tracks").toList();
    if (from < 0 || to < 0 || from >= tracks.size() || to >= tracks.size() || from == to) return;
    if (!m_playlistEditModel.moveItem(from, to)) return;
    tracks.move(from, to); draft["tracks"] = tracks; draft["track_count"] = tracks.size(); set("playlistDraft", draft);
}
void SoundCloud::removePlaylistTrack(int index) {
    if (m_state.value("playlistEditBusy").toBool()) return;
    auto draft = m_state.value("playlistDraft").toMap(); auto tracks = draft.value("tracks").toList();
    if (index < 0 || index >= tracks.size()) return;
    if (!m_playlistEditModel.removeItem(index)) return;
    tracks.removeAt(index); draft["tracks"] = tracks; draft["track_count"] = tracks.size(); set("playlistDraft", draft);
}
void SoundCloud::invalidatePlaylists() {
    m_listings.clear(); m_homeLoaded = false; ++m_playlistGeneration;
    set("playlistHasMore", false); set("playlistBusy", false);
}
void SoundCloud::savePlaylist(const QString &title, const QString &description, bool privatePlaylist) {
    if (m_playlistWriteBusy || m_state.value("playlistEditBusy").toBool() || m_editOriginal.isEmpty() || !signedIn()) return;
    if (title.trimmed().isEmpty()) { set("playlistEditError", "Укажите название плейлиста."); return; }
    const auto draft = m_state.value("playlistDraft").toMap(); const auto id = urn(m_editOriginal);
    const int generation = m_editGeneration;
    m_playlistWriteBusy = true;
    set("playlistEditBusy", true); set("playlistEditError", "");
    auto failed = [this, generation](QString error) {
        if (generation != m_editGeneration) return;
        m_playlistWriteBusy = false;
        set("playlistEditError", error); set("playlistEditBusy", false);
    };
    validatePlaylistSnapshot(id, m_editOriginal, [this, draft, id, title, description, privatePlaylist, generation, failed](QVariantMap) {
        if (generation != m_editGeneration) return;
        QJsonArray tracks;
        for (const auto &track : draft.value("tracks").toList()) tracks.append(QJsonObject{{"urn", urn(track.toMap())}});
        const QJsonObject body{{"playlist", QJsonObject{{"title", title.trimmed()}, {"description", description}, {"sharing", privatePlaylist ? "private" : "public"}, {"tracks", tracks}}}};
        request(pathFor("playlists", id), [this, draft, title, description, privatePlaylist, generation](QJsonDocument doc) mutable {
            if (generation != m_editGeneration) return;
            auto updated = doc.object().toVariantMap();
            if (updated.isEmpty()) updated = draft;
            updated["title"] = title.trimmed(); updated["description"] = description; updated["sharing"] = privatePlaylist ? "private" : "public";
            updated["tracks"] = draft.value("tracks"); updated["track_count"] = draft.value("tracks").toList().size();
            m_playlistWriteBusy = false;
            invalidatePlaylists(); set("playlistTracks", withLikeStates(draft.value("tracks").toList()));
            m_editOriginal = updated; set("playlistDraft", updated); set("playlistEditBusy", false);
            emit playlistUpdated(updated);
        }, "PUT", body, true, failed);
    }, failed);
}
void SoundCloud::deletePlaylist() {
    if (m_playlistWriteBusy || m_state.value("playlistEditBusy").toBool() || m_editOriginal.isEmpty() || !signedIn()) return;
    const auto id = urn(m_editOriginal); const int generation = m_editGeneration;
    m_playlistWriteBusy = true;
    set("playlistEditBusy", true); set("playlistEditError", "");
    auto failed = [this, generation](QString error) {
        if (generation != m_editGeneration) return;
        m_playlistWriteBusy = false;
        set("playlistEditError", error); set("playlistEditBusy", false);
    };
    validatePlaylistSnapshot(id, m_editOriginal, [this, id, generation, failed](QVariantMap) {
        if (generation != m_editGeneration) return;
        request(pathFor("playlists", id), [this, id, generation](QJsonDocument) {
            if (generation != m_editGeneration) return;
            m_playlistWriteBusy = false;
            invalidatePlaylists(); set("playlistTracks", QVariantList{});
            set("playlistEditBusy", false); cancelPlaylistEdit(); emit playlistDeleted(id);
        }, "DELETE", {}, true, failed);
    }, failed);
}
void SoundCloud::cancelPlaylistEdit() {
    if (m_state.value("playlistEditBusy").toBool() && !m_editOriginal.isEmpty()) return;
    ++m_editGeneration; m_editOriginal.clear(); m_playlistEditModel.update({}); set("playlistDraft", QVariantMap{}); set("playlistEditError", ""); set("playlistEditBusy", false);
}

void SoundCloud::openLink(const QString &input) {
    QUrl url(input.trimmed());
    if (url.scheme() == "scrip" || url.scheme() == "soundcloud") {
        if (url.host() == "open") url = QUrl(QUrlQuery(url).queryItemValue("url", QUrl::FullyDecoded));
        else if (url.scheme() == "soundcloud" && (url.host() == "users" || url.host() == "tracks" || url.host() == "playlists")) {
            const auto id = url.path().mid(1);
            if (id.isEmpty() || id.contains('/') || !std::all_of(id.begin(), id.end(), [](QChar c) { return c.isDigit(); })) { fail("Некорректная ссылка SoundCloud."); return; }
            if (!m_state.value("connected").toBool()) { m_pendingLink = input; return; }
            request("/" + url.host() + "/soundcloud%3A" + url.host() + "%3A" + id, [this](QJsonDocument doc) { emit resourceResolved(doc.object().toVariantMap()); }); return;
        }
    }
    const auto host = url.host().toLower();
    if ((url.scheme() != "https" && url.scheme() != "http") ||
        (host != "soundcloud.com" && host != "www.soundcloud.com" && host != "m.soundcloud.com" && host != "on.soundcloud.com") ||
        !url.userInfo().isEmpty() || (url.port() != -1 && url.port() != 443 && url.port() != 80)) {
        fail("Укажите ссылку на трек, плейлист, станцию или профиль SoundCloud."); return;
    }
    url.setScheme("https"); url.setPort(-1);
    if (!m_state.value("connected").toBool()) { m_pendingLink = input; fail("Подключите SoundCloud, чтобы открыть ссылку."); return; }
    const int generation = ++m_linkGeneration;
    set("linkBusy", true); clearError();
    QUrlQuery query; query.addQueryItem("url", url.toString(QUrl::FullyEncoded));
    request("/resolve?" + query.query(QUrl::FullyEncoded), [this, generation](QJsonDocument doc) {
        if (generation != m_linkGeneration) return;
        set("linkBusy", false);
        const auto item = doc.object().toVariantMap(); const auto id = urn(item);
        if (!id.startsWith("soundcloud:tracks:") && !id.startsWith("soundcloud:users:") && !id.startsWith("soundcloud:playlists:") && !id.startsWith("soundcloud:system-playlists:")) {
            fail("Эта страница SoundCloud пока не поддерживается. Откройте ссылку на трек, подборку или профиль."); return;
        }
        emit resourceResolved(item);
    }, "GET", {}, true, [this, generation](QString error) {
        if (generation != m_linkGeneration) return;
        set("linkBusy", false); fail(error);
    });
}

void SoundCloud::openProfileWebsite(const QString &value) {
    const QUrl url(value);
    if ((url.scheme() != "https" && url.scheme() != "http") || url.host().isEmpty() || !url.userInfo().isEmpty()) return;
    bool listed = false;
    for (const auto &link : m_state.value("profileLinks").toList())
        if (link.toMap().value("url").toString() == value) { listed = true; break; }
    if (!listed) return;
    const auto host = url.host().toLower();
    if (host == "soundcloud.com" || host == "www.soundcloud.com" || host == "m.soundcloud.com" || host == "on.soundcloud.com") openLink(value);
    else QDesktopServices::openUrl(url);
}

void SoundCloud::openStation(const QString &id, const QVariantMap &seed) {
    if (!id.startsWith("soundcloud:system-playlists:")) return;
    const int generation = ++m_linkGeneration; set("linkBusy", true); clearError();
    request(pathFor("system-playlists", id), [this, generation, id](QJsonDocument doc) {
        if (generation != m_linkGeneration) return;
        set("linkBusy", false);
        const auto item = doc.object().toVariantMap();
        if (urn(item) != id) { fail("SoundCloud не вернул станцию."); return; }
        emit resourceResolved(item);
    }, "GET", {}, true, [this, generation, seed](QString error) {
        if (generation != m_linkGeneration) return;
        set("linkBusy", false);
        if (urn(seed).startsWith("soundcloud:tracks:")) {
            emit resourceResolved({{"urn", urn(seed)}, {"preview_type", "station"}, {"seed", seed},
                {"title", "Подборка SCrip · " + seed.value("title").toString()}, {"artwork_url", seed.value("artwork_url")},
                {"description", "Станция SoundCloud недоступна. Здесь можно послушать похожие треки."}});
        } else fail(error);
    });
}
