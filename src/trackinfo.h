#pragma once
#include "trackmetadata.h"
#include <QDate>
#include <QLocale>
#include <cmath>

namespace TrackInfo {
inline QString text(const QVariant &v) {
    if (v.metaType().id() != QMetaType::QString) return {};
    const QString s = v.toString().trimmed();
    if (s.compare("null", Qt::CaseInsensitive) == 0 || s.compare("undefined", Qt::CaseInsensitive) == 0) return {};
    for (const auto c : s) if (c.category() == QChar::Other_Control && c != '\n' && c != '\t') return {};
    return s;
}
inline double number(const QVariant &v) {
    switch (v.metaType().id()) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong: case QMetaType::Double: case QMetaType::Float:
        return v.toDouble();
    default: return NAN;
    }
}
inline int integer(const QVariant &v) { const auto n = number(v); return std::isfinite(n) && n >= 0 && n <= 9999 && n == std::floor(n) ? int(n) : 0; }
inline QString releaseDate(const QVariantMap &t) {
    const int y = integer(t.value("release_year")), m = integer(t.value("release_month")), d = integer(t.value("release_day"));
    if (y < 1) return {};
    QString result = QString::number(y).rightJustified(4, '0');
    if (m < 1 || m > 12) return result;
    result += QString("-%1").arg(m, 2, 10, QChar('0'));
    if (QDate(y,m,d).isValid()) result += QString("-%1").arg(d, 2, 10, QChar('0'));
    return result;
}
inline QStringList tags(const QVariant &value) {
    const auto input = text(value).left(16384);
    QStringList result; QString token; bool quoted = false;
    auto append = [&] { const auto s = text(token); if (!s.isEmpty() && !result.contains(s) && result.size() < 128) result << s; token.clear(); };
    for (auto c : input) {
        if (c == '"') quoted = !quoted;
        else if (c.isSpace() && !quoted) append();
        else token += c;
    }
    if (!quoted) append(); // Ignore an incomplete quoted tag.
    return result;
}
inline QString license(const QVariant &v) {
    const auto s = text(v);
    static const QMap<QString, QString> names {
        {"all-rights-reserved", "Все права защищены"}, {"no-rights-reserved", "Без сохранения авторских прав"},
        {"cc-by", "Creative Commons — указание авторства"},
        {"cc-by-sa", "Creative Commons — указание авторства, сохранение условий"},
        {"cc-by-nd", "Creative Commons — указание авторства, без изменений"},
        {"cc-by-nc", "Creative Commons — указание авторства, некоммерческая"},
        {"cc-by-nc-sa", "Creative Commons — указание авторства, некоммерческая, сохранение условий"},
        {"cc-by-nc-nd", "Creative Commons — указание авторства, некоммерческая, без изменений"}};
    return s.isEmpty() ? QString() : names.value(s, "Неизвестная лицензия: " + s.left(120));
}
// Access describes API playback; sharing is independent of playback entitlement.
inline QString availability(const QVariantMap &t) {
    const auto access = text(t.value("access"));
    if (access == "blocked") return "Недоступен через API";
    if (access == "preview") return "Только фрагмент";
    if (t.value("streamable").metaType().id() == QMetaType::Bool && !t.value("streamable").toBool()) return "Недоступен через API";
    if (access == "playable") return "Полная версия";
    return {};
}
inline QString availabilityNote(const QVariantMap &t) {
    const auto status = availability(t);
    if (status == "Только фрагмент")
        return "SoundCloud предоставляет только фрагмент. Полная версия может требовать подписку Go+. Проверьте условия на странице трека в SoundCloud.";
    if (status == "Недоступен через API")
        return "SoundCloud не разрешает воспроизведение через API. Возможны ограничения региона, правообладателя или подписки. Точная причина не сообщается; проверьте страницу трека в SoundCloud.";
    return {};
}
inline QVariantList rows(const QVariantMap &t) {
    QVariantList out;
    auto add = [&](QString label, QString value) { if (!value.isEmpty()) out << QVariantMap{{"label", label}, {"value", value}}; };
    add("Исполнитель", text(TrackMetadata::displayArtist(t)));
    add("Загрузил", text(t.value("user").toMap().value("username")));
    const double duration = number(t.value("duration"));
    if (std::isfinite(duration) && duration > 0 && duration < 1e12) { const auto sec = qint64(duration / 1000); add("Длительность", QString::number(sec / 60) + ":" + QString::number(sec % 60).rightJustified(2, '0')); }
    add("Воспроизведение", availability(t));
    add("Ограничения", availabilityNote(t));
    const auto sharing = text(t.value("sharing"));
    add("Видимость", sharing == "private" ? "Приватный трек" : sharing == "public" ? "Публичный трек" : QString{});
    add("Жанр", text(t.value("genre")));
    const double bpm = number(t.value("bpm"));
    if (std::isfinite(bpm) && bpm > 0 && bpm <= 1000) add("BPM", QString::number(bpm, 'g', 6));
    add("Тональность", text(t.value("key_signature")));
    add("Лейбл", text(t.value("label_name")));
    add("Дата релиза", releaseDate(t));
    add("Лицензия", license(t.value("license")));
    add("Теги", tags(t.value("tag_list")).join(" · "));
    return out;
}
}
