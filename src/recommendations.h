#pragma once
#include <QVariantMap>
#include <QVariantList>
#include <QSet>
#include <utility>

// Stable local ordering of documented SoundCloud related-track results.
namespace Recommendations {
inline QString id(const QVariant &value) { return value.toMap().value("urn").toString(); }
inline QString artist(const QVariant &value) { return value.toMap().value("user").toMap().value("urn").toString(); }
inline bool playable(const QVariant &value) {
    const auto track = value.toMap();
    return id(value).startsWith("soundcloud:tracks:") && track.value("access").toString() != "blocked"
        && track.value("streamable") != false;
}
inline QVariantList seeds(const QVariantList &tracks, int limit = 3) {
    QVariantList result;
    QSet<QString> ids, artists;
    for (const auto &track : tracks) {
        const auto user = artist(track);
        if (!playable(track) || ids.contains(id(track)) || (!user.isEmpty() && artists.contains(user))) continue;
        ids.insert(id(track)); if (!user.isEmpty()) artists.insert(user);
        result.append(track);
        if (result.size() == limit) break;
    }
    return result;
}
inline QVariantList select(QVariantList pool, int limit, QSet<QString> excluded = {}, QString previous = {}) {
    QVariantList result;
    for (const auto &track : std::as_const(pool)) if (!playable(track)) excluded.insert(id(track));
    QSet<QString> artists;
    // Prefer unused artists, then avoid neighbours, finally allow an artist repeat.
    // Identity and availability constraints are never relaxed.
    for (int pass = 0; pass < 3 && result.size() < limit; ++pass) {
        bool added = true;
        while (added && result.size() < limit) {
            added = false;
            for (const auto &track : std::as_const(pool)) {
                const auto user = artist(track);
                if (!playable(track) || excluded.contains(id(track))) continue;
                if (!user.isEmpty() && ((pass == 0 && artists.contains(user)) || (pass < 2 && user == previous))) continue;
                result.append(track); excluded.insert(id(track));
                if (!user.isEmpty()) artists.insert(user);
                previous = user; added = true; break;
            }
        }
    }
    return result;
}
// Bounded local mixing: two familiar/one new, alternating, or one familiar/three new.
// Discovery never fills missing new slots with familiar tracks.
inline QVariantList mix(const QVariantList &familiar, const QVariantList &novel, int preference,
                        int limit, QSet<QString> excluded = {}) {
    const auto known = select(familiar, limit, excluded);
    QSet<QString> knownIds;
    for (const auto &track : familiar) knownIds.insert(id(track));
    QVariantList fresh;
    for (const auto &track : novel)
        if (!knownIds.contains(id(track)) && !track.toMap().value("user_favorite").toBool()) fresh.append(track);
    fresh = select(fresh, limit, excluded);
    QVariantList result;
    const int knownLimit = preference == 2 ? qMin(int(known.size()), int(fresh.size()) / 3) : int(known.size());
    int a = 0, b = 0, step = 0;
    while (result.size() < limit && (a < knownLimit || b < fresh.size())) {
        const bool preferKnown = preference == 0 ? step % 3 != 2 : preference == 2 ? step % 4 == 0 : step % 2 == 0;
        const auto track = ((preferKnown && a < knownLimit) || b >= fresh.size()) ? known[a++] : fresh[b++];
        ++step;
        if (excluded.contains(id(track))) continue;
        excluded.insert(id(track)); result.append(track);
    }
    return result;
}

}
