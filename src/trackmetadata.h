#pragma once
#include <QVariantMap>

namespace TrackMetadata {
// Keep the uploader object intact: artist credits are display metadata only.
inline QString displayArtist(const QVariantMap &track) {
    const auto value = track.value("metadata_artist");
    if (value.metaType().id() == QMetaType::QString) {
        const auto name = value.toString().trimmed();
        bool valid = !name.isEmpty(), visible = false;
        for (const auto ch : name) {
            if (ch.category() == QChar::Other_Control) valid = false;
            if (!ch.isSpace() && ch.category() != QChar::Other_Format) visible = true;
        }
        if (valid && visible) return name;
    }
    return track.value("user").toMap().value("username").toString().trimmed();
}
inline bool hasSeparateArtist(const QVariantMap &track) {
    return displayArtist(track) != track.value("user").toMap().value("username").toString().trimmed();
}
}
