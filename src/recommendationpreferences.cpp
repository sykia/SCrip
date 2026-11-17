#include "soundcloud.h"
#include "recommendations.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>

void SoundCloud::loadRecommendationPreferences(const QString &account) {
    m_recommendationAccount = account.startsWith("soundcloud:users:") ? account : QString{};
    m_excludedTracks.clear(); m_excludedAccounts.clear(); m_exclusionUndo.clear();
    m_familiarTracks.clear(); m_waveCandidates.clear(); m_wavePreferenceWaiting = false; m_recommendationPreference = 1;
    if (!m_recommendationAccount.isEmpty()) {
        const auto data = QJsonDocument::fromJson(QSettings().value("recommendations/" + m_recommendationAccount).toByteArray()).object().toVariantMap();
        const auto read = [](const QVariant &value, const QString &prefix) {
            QVariantMap result;
            const auto map = value.toMap();
            for (auto it = map.begin(); it != map.end(); ++it)
                if (it.key().startsWith(prefix) && it.key().size() > prefix.size() && it.value().metaType().id() == QMetaType::QString)
                    result.insert(it.key(), it.value().toString().left(500));
            return result;
        };
        m_excludedTracks = read(data.value("tracks"), "soundcloud:tracks:");
        m_excludedAccounts = read(data.value("accounts"), "soundcloud:users:");
        bool validPreference = false;
        const int preference = data.value("preference", 1).toInt(&validPreference);
        if (validPreference && preference >= 0 && preference <= 2) m_recommendationPreference = preference;
    }
    publishRecommendationPreferences();
}
void SoundCloud::publishRecommendationPreferences() {
    QVariantList entries;
    const auto add = [&entries](const QVariantMap &map, bool account) {
        for (auto it = map.begin(); it != map.end(); ++it)
            entries.append(QVariantMap{{"urn", it.key()}, {"name", it.value()}, {"account", account}});
    };
    add(m_excludedTracks, false); add(m_excludedAccounts, true);
    set("recommendationExclusions", entries);
    set("recommendationPreference", m_recommendationPreference);
    set("recommendationUndo", !m_exclusionUndo.isEmpty());
}
void SoundCloud::saveRecommendationPreferences() {
    if (!m_recommendationAccount.isEmpty())
        QSettings().setValue("recommendations/" + m_recommendationAccount,
            QJsonDocument::fromVariant(QVariantMap{{"tracks", m_excludedTracks}, {"accounts", m_excludedAccounts},
                {"preference", m_recommendationPreference}}).toJson(QJsonDocument::Compact));
    publishRecommendationPreferences();
}
bool SoundCloud::recommendationExcluded(const QVariant &track) const {
    return m_excludedTracks.contains(Recommendations::id(track)) || m_excludedAccounts.contains(Recommendations::artist(track));
}
QVariantList SoundCloud::filterRecommendations(const QVariantList &tracks) const {
    QVariantList result;
    for (const auto &track : tracks) if (!recommendationExcluded(track)) result.append(track);
    return result;
}
void SoundCloud::setRecommendationPreference(int value) {
    if (m_recommendationAccount.isEmpty() || value < 0 || value > 2) return;
    m_recommendationPreference = value; saveRecommendationPreferences();
}
void SoundCloud::excludeRecommendation(const QVariantMap &track, bool account) {
    if (m_recommendationAccount.isEmpty()) return;
    const auto id = account ? Recommendations::artist(track) : Recommendations::id(track);
    if (!id.startsWith(account ? "soundcloud:users:" : "soundcloud:tracks:")) return;
    auto &map = account ? m_excludedAccounts : m_excludedTracks;
    // A duplicate action must never make an older exclusion undoable.
    m_exclusionUndo.clear();
    if (!map.contains(id)) {
        map.insert(id, account ? track.value("user").toMap().value("username", id) : track.value("title", id));
        m_exclusionUndo = id; m_exclusionUndoAccount = account;
    }
    saveRecommendationPreferences(); pruneRecommendations();
    if (!m_exclusionUndo.isEmpty()) emit recommendationExclusionAdded();
}
void SoundCloud::undoRecommendationExclusion() {
    if (m_exclusionUndo.isEmpty()) return;
    (m_exclusionUndoAccount ? m_excludedAccounts : m_excludedTracks).remove(m_exclusionUndo);
    m_exclusionUndo.clear(); saveRecommendationPreferences();
}
void SoundCloud::restoreRecommendation(const QString &id, bool account) {
    (account ? m_excludedAccounts : m_excludedTracks).remove(id);
    m_exclusionUndo.clear(); saveRecommendationPreferences();
}
void SoundCloud::clearRecommendationExclusions() {
    m_excludedTracks.clear(); m_excludedAccounts.clear(); m_exclusionUndo.clear();
    saveRecommendationPreferences();
}
void SoundCloud::pruneRecommendations(bool prepare) {
    auto shelves = m_state.value("home").toList();
    for (auto &entry : shelves) {
        auto shelf = entry.toMap();
        if (shelf.value("recommendations").toBool()) shelf["tracks"] = filterRecommendations(shelf.value("tracks").toList());
        entry = shelf;
    }
    set("home", shelves);
    syncQueueOrder();
    bool removed = false;
    for (int i = m_queue.size() - 1; i > m_queueIndex; --i) {
        if (m_queue[i].toMap().value("scripAutomaticRecommendation").toBool() && recommendationExcluded(m_queue[i])) {
            m_queue.removeAt(i); m_queueIds.removeAt(i); removed = true;
        }
    }
    if (removed) {
        if (m_crossfadeStarted) finishCrossfade(false);
        cancelPreload(); m_plannedId = -1;
        set("queue", m_queue);
        if (prepare) { savePlayback(); prepareNext(); }
    }
}
