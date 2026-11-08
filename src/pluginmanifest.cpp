#include "pluginmanifest.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <cmath>

namespace {
bool fields(const QJsonObject &o, const QStringList &allowed, const QStringList &required = {}) {
    for (const auto &key : o.keys())
        if (!allowed.contains(key))
            return false;
    for (const auto &key : required)
        if (!o.contains(key))
            return false;
    return true;
}
bool text(const QJsonValue &v, int max, bool empty = false) {
    if (!v.isString() || v.toString().size() > max || (!empty && v.toString().trimmed().isEmpty()))
        return false;
    for (auto c : v.toString())
        if (!c.isPrint())
            return false;
    return true;
}
bool integer(const QJsonValue &v, double min, double max) {
    return v.isDouble() && std::isfinite(v.toDouble()) && std::floor(v.toDouble()) == v.toDouble() &&
           v.toDouble() >= min && v.toDouble() <= max;
}
bool id(const QJsonValue &v) {
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9.-]{0,47}\\z"));
    return v.isString() && pattern.match(v.toString()).hasMatch();
}
bool range(const QJsonValue &v) {
    const auto o = v.toObject();
    return v.isObject() && fields(o, {"min", "maxExclusive"}, {"min", "maxExclusive"}) &&
           PluginManifest::version(o["min"].toString()) &&
           PluginManifest::version(o["maxExclusive"].toString()) &&
           PluginManifest::compareVersions(o["min"].toString(), o["maxExclusive"].toString()) < 0;
}
bool dataDepth(const QJsonValue &value, int depth) {
    if (depth > 8)
        return false;
    if (value.isArray())
        for (auto child : value.toArray())
            if (!dataDepth(child, depth + 1))
                return false;
    if (value.isObject())
        for (auto child : value.toObject())
            if (!dataDepth(child, depth + 1))
                return false;
    return true;
}
QString theme(const QJsonObject &o) {
    static const QRegularExpression color("^#[0-9a-fA-F]{6}\\z");
    if (!fields(o, {"accent", "covers", "coverRadius"}) || o.isEmpty())
        return "Неизвестное поле темы.";
    if (o.contains("accent") && (!o["accent"].isString() || !color.match(o["accent"].toString()).hasMatch()))
        return "Некорректный цвет.";
    if (o.contains("coverRadius") && !integer(o["coverRadius"], 0, 24))
        return "Некорректный радиус.";
    if (o.contains("covers") && !QStringList{"artwork", "artist"}.contains(o["covers"].toString()))
        return "Некорректный вид обложки.";
    return {};
}
const QStringList destinations{"likes", "history", "feed", "playlists", "profile", "search"};

// Только структурная проверка до QJsonDocument: обнаруживает потерянные при разборе
// дубликаты ключей и ограничивает глубину. Синтаксис проверяет сам Qt.
class Structure {
    const QByteArray &b;
    qsizetype p = 0;
    void ws() {
        while (p < b.size() && QByteArray(" \t\r\n").contains(b[p]))
            ++p;
    }
    QByteArray string() {
        const auto start = p++;
        while (p < b.size()) {
            const char c = b[p++];
            if (c == '"')
                return b.mid(start, p - start);
            if (c == '\\') {
                if (p == b.size())
                    return {};
                ++p;
            }
        }
        return {};
    }
    bool value(int depth) {
        ws();
        if (p >= b.size() || depth > 16)
            return false;
        if (b[p] == '"')
            return !string().isEmpty();
        if (b[p] == '{' || b[p] == '[') {
            const bool object = b[p++] == '{';
            const char close = object ? '}' : ']';
            QSet<QString> seen;
            ws();
            if (p < b.size() && b[p] == close) {
                ++p;
                return true;
            }
            while (p < b.size()) {
                if (object) {
                    ws();
                    if (p >= b.size() || b[p] != '"')
                        return false;
                    auto key = string();
                    if (key.isEmpty())
                        return false;
                    auto decoded = QJsonDocument::fromJson("[" + key + "]");
                    if (decoded.isNull())
                        return false;
                    const auto name = decoded.array()[0].toString();
                    if (seen.contains(name))
                        return false;
                    seen.insert(name);
                    ws();
                    if (p >= b.size() || b[p++] != ':')
                        return false;
                }
                if (!value(depth + 1))
                    return false;
                ws();
                if (p >= b.size())
                    return false;
                if (b[p] == close) {
                    ++p;
                    return true;
                }
                if (b[p++] != ',')
                    return false;
            }
            return false;
        }
        const auto start = p;
        while (p < b.size() && !QByteArray(",]} \t\r\n").contains(b[p]))
            ++p;
        return p > start;
    }

  public:
    explicit Structure(const QByteArray &bytes) : b(bytes) {}
    bool check() {
        if (!value(0))
            return false;
        ws();
        return p == b.size();
    }
};
QString legacy(const QJsonObject &o) {
    if (!fields(o, {"api", "id", "name", "groups"}, {"api", "id", "name", "groups"}) || !id(o["id"]) ||
        !text(o["name"], 48) || o["name"].toString().contains('<') || o["name"].toString().contains('>') ||
        !o["groups"].isArray())
        return "Некорректный манифест v1.";
    const auto groups = o["groups"].toArray();
    if (groups.isEmpty() || groups.size() > 6)
        return "Допустимо 1–6 групп.";
    for (auto v : groups) {
        if (!v.isObject())
            return "Некорректная группа.";
        auto g = v.toObject();
        if (!fields(g, {"label", "accent", "coverRadius", "covers", "page", "text", "buttons"}, {"label"}) ||
            !text(g["label"], 48))
            return "Некорректная группа.";
        for (auto key : {"label", "text"})
            if (g.contains(key) && (g[key].toString().contains('<') || g[key].toString().contains('>')))
                return "Недопустимый текст v1.";
        QJsonObject t;
        for (auto k : {"accent", "covers", "coverRadius"})
            if (g.contains(k))
                t[k] = g[k];
        if (!t.isEmpty() && !theme(t).isEmpty())
            return theme(t);
        if (g.contains("page") && !QStringList{"home", "profile"}.contains(g["page"].toString()))
            return "Неизвестная страница.";
        if ((g.contains("text") || g.contains("buttons")) && !g.contains("page"))
            return "Не указана страница.";
        if (g.contains("text") && !text(g["text"], 180))
            return "Некорректный текст.";
        if (g.contains("buttons") && (!g["buttons"].isArray() || g["buttons"].toArray().size() > 4))
            return "Некорректные кнопки.";
        for (auto b : g["buttons"].toArray()) {
            auto button = b.toObject();
            if (!b.isObject() || !fields(button, {"label", "action"}, {"label", "action"}) ||
                !text(button["label"], 32) || button["label"].toString().contains('<') ||
                button["label"].toString().contains('>') ||
                !destinations.contains(button["action"].toString()))
                return "Недопустимое действие v1.";
        }
    }
    return {};
}
} // namespace
namespace PluginManifest {
bool version(const QString &text, QList<int> *parts) {
    static const QRegularExpression re("^(0|[1-9][0-9]{0,4})\\.(0|[1-9][0-9]{0,4})\\.(0|[1-9][0-9]{0,4})\\z");
    if (!re.match(text).hasMatch())
        return false;
    QList<int> values;
    for (auto part : text.split('.')) {
        const int n = part.toInt();
        if (n > 65535)
            return false;
        values.append(n);
    }
    if (parts)
        *parts = values;
    return true;
}
int compareVersions(const QString &a, const QString &b) {
    QList<int> x, y;
    if (!version(a, &x) || !version(b, &y))
        return 0;
    for (int i = 0; i < 3; ++i)
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    return 0;
}
bool compatible(const QJsonObject &r, const QString &v) {
    return version(v) && compareVersions(v, r["min"].toString()) >= 0 &&
           compareVersions(v, r["maxExclusive"].toString()) < 0;
}
bool settingValue(const QJsonObject &s, const QJsonValue &v) {
    const auto type = s["type"].toString();
    if (type == "boolean")
        return v.isBool();
    if (type == "integer")
        return integer(v, s["min"].toDouble(), s["max"].toDouble());
    if (type == "enum")
        return v.isString() && s["options"].toArray().contains(v);
    return false;
}
QString commandPermission(const QString &c) {
    if (QStringList{"player.pause", "player.resume", "player.next", "player.previous", "player.seek",
                    "player.repeat", "player.shuffle"}
            .contains(c))
        return "player.control";
    if (c == "navigation.open")
        return "navigation";
    if (c == "timer.start" || c == "timer.cancel")
        return "timers";
    if (c == "storage.set" || c == "storage.remove")
        return "storage";
    return {};
}
Result parse(const QByteArray &bytes) {
    if (bytes.size() > 131072)
        return {{}, "Файл превышает 128 КиБ."};
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder(bytes);
    Q_UNUSED(decoded);
    if (decoder.hasError() || !Structure(bytes).check())
        return {{}, "Неверный JSON, повторный ключ или глубина больше 16."};
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {{}, "Ожидается JSON-объект."};
    return {doc.object(), validate(doc.object())};
}
QString validate(const QJsonObject &o) {
    if (o["api"] != 2)
        return "Неподдерживаемая версия API.";
    if (o["kind"] != "declarative")
        return "unsupported-runtime: исполняемые расширения не поддерживаются; изолированная среда "
               "отсутствует.";
    const QStringList required{"api",          "kind",        "id",       "name",          "version",
                               "requiresHost", "permissions", "settings", "contributions", "rules"};
    auto allowed = required;
    allowed.append({"description", "dependencies"});
    if (!fields(o, allowed, required) || !id(o["id"]) || !text(o["name"], 48) ||
        !version(o["version"].toString()) || !range(o["requiresHost"]) ||
        (o.contains("description") && !text(o["description"], 1000, true)))
        return "Некорректные поля манифеста v2.";
    for (auto key : {"permissions", "settings", "contributions", "rules"})
        if (!o[key].isArray())
            return "Ожидается массив.";
    const QStringList permissions{"ui.panels",  "ui.theme", "player.observe", "player.control",
                                  "navigation", "storage",  "timers"};
    QSet<QString> grants;
    for (auto p : o["permissions"].toArray()) {
        if (!p.isString() || !permissions.contains(p.toString()) || grants.contains(p.toString()))
            return "Неизвестное или повторное разрешение.";
        grants.insert(p.toString());
    }
    if (o.contains("dependencies") &&
        (!o["dependencies"].isArray() || o["dependencies"].toArray().size() > 8))
        return "Некорректные зависимости.";
    QSet<QString> deps;
    for (auto d : o["dependencies"].toArray()) {
        auto v = d.toObject();
        auto r = v;
        r.remove("id");
        if (!d.isObject() || !fields(v, {"id", "min", "maxExclusive"}, {"id", "min", "maxExclusive"}) ||
            !id(v["id"]) || v["id"] == o["id"] || deps.contains(v["id"].toString()) || !range(r))
            return "Некорректная зависимость.";
        deps.insert(v["id"].toString());
    }
    if (o["settings"].toArray().size() > 16 || o["contributions"].toArray().size() > 8 ||
        o["rules"].toArray().size() > 16)
        return "Превышен лимит раздела.";
    QHash<QString, QJsonObject> settings;
    for (auto v : o["settings"].toArray()) {
        auto s = v.toObject();
        const auto type = s["type"].toString();
        QStringList keys{"id", "type", "label", "default"};
        if (type == "integer")
            keys.append({"min", "max"});
        else if (type == "enum")
            keys.append("options");
        else if (type != "boolean")
            return "Неизвестный тип настройки.";
        if (!v.isObject() || !fields(s, keys, keys) || !id(s["id"]) ||
            settings.contains(s["id"].toString()) || !text(s["label"], 48))
            return "Некорректная настройка.";
        if (type == "integer" &&
            (!integer(s["min"], -2147483647, 2147483647) || !integer(s["max"], -2147483647, 2147483647) ||
             s["min"].toDouble() > s["max"].toDouble()))
            return "Некорректные пределы настройки.";
        if (type == "enum") {
            auto a = s["options"].toArray();
            QSet<QString> unique;
            if (!s["options"].isArray() || a.isEmpty() || a.size() > 16)
                return "Некорректные варианты настройки.";
            for (auto item : a) {
                if (!text(item, 48) || unique.contains(item.toString()))
                    return "Некорректный вариант.";
                unique.insert(item.toString());
            }
        }
        if (!settingValue(s, s["default"]))
            return "Некорректное значение по умолчанию.";
        settings.insert(s["id"].toString(), s);
    }
    QSet<QString> timerIds;
    auto action = [&](const QJsonValue &v, bool button) -> QString {
        const auto a = v.toObject();
        const auto cmd = a["command"].toString();
        const auto perm = commandPermission(cmd);
        if (!v.isObject() || perm.isEmpty() || !grants.contains(perm))
            return "Команда не разрешена манифестом.";
        QStringList keys{"command"};
        if (cmd == "player.seek") {
            keys.append("positionMs");
            if (!integer(a["positionMs"], 0, 9007199254740991.0))
                return "Некорректная позиция.";
        }
        if (cmd == "player.repeat" || cmd == "player.shuffle") {
            keys.append("enabled");
            if (!a["enabled"].isBool())
                return "Ожидается bool.";
        }
        if (cmd == "navigation.open") {
            keys.append("destination");
            if (!button || !destinations.contains(a["destination"].toString()))
                return "Недопустимая навигация.";
        }
        if (cmd.startsWith("timer.")) {
            keys.append("timerId");
            if (!id(a["timerId"]))
                return "Некорректный таймер.";
            timerIds.insert(a["timerId"].toString());
            if (cmd == "timer.start") {
                keys.append("minutesSetting");
                auto st = settings.value(a["minutesSetting"].toString());
                if (!button || st["type"] != "integer" || st["min"].toDouble() < 1 ||
                    st["max"].toDouble() > 180)
                    return "Таймер требует настройку 1–180 минут и нажатие.";
            }
        }
        if (cmd.startsWith("storage.")) {
            keys.append("key");
            if (!id(a["key"]))
                return "Некорректный ключ данных.";
            if (cmd == "storage.set") {
                keys.append("value");
                if (!a.contains("value") || !dataDepth(a["value"], 1))
                    return "Значение данных отсутствует или превышает глубину 8.";
            }
        }
        if (!fields(a, keys, keys))
            return "Некорректные аргументы команды.";
        return {};
    };
    QSet<QString> contributions;
    int themes = 0;
    for (auto v : o["contributions"].toArray()) {
        auto c = v.toObject();
        auto type = c["type"].toString();
        if (!v.isObject() || !id(c["id"]) || contributions.contains(c["id"].toString()))
            return "Некорректный ID вклада.";
        contributions.insert(c["id"].toString());
        if (c.contains("enabledSetting") && (!settings.contains(c["enabledSetting"].toString()) ||
                                             settings[c["enabledSetting"].toString()]["type"] != "boolean"))
            return "Неизвестный переключатель вклада.";
        if (type == "theme") {
            if (!grants.contains("ui.theme") || ++themes > 6 ||
                !fields(c, {"id", "type", "values", "enabledSetting"}, {"id", "type", "values"}) ||
                !c["values"].isObject() || !theme(c["values"].toObject()).isEmpty())
                return "Некорректный вклад темы.";
        } else if (type == "panel") {
            if (!grants.contains("ui.panels") ||
                !fields(c, {"id", "type", "slot", "title", "text", "buttons", "enabledSetting"},
                        {"id", "type", "slot", "title"}) ||
                !QStringList{"home.panels", "profile.panels", "player.tools"}.contains(
                    c["slot"].toString()) ||
                !text(c["title"], 48) || (c.contains("text") && !text(c["text"], 1000, true)))
                return "Некорректная панель.";
            if (c.contains("buttons") && (!c["buttons"].isArray() || c["buttons"].toArray().size() > 4))
                return "Некорректные кнопки.";
            for (auto b : c["buttons"].toArray()) {
                auto bt = b.toObject();
                if (!b.isObject() || !fields(bt, {"label", "action"}, {"label", "action"}) ||
                    !text(bt["label"], 32))
                    return "Некорректная кнопка.";
                auto err = action(bt["action"], true);
                if (!err.isEmpty())
                    return err;
            }
        } else
            return "Неизвестный вклад.";
    }
    QSet<QString> rules, expired;
    const QHash<QString, QHash<QString, int>> events{
        {"player.trackChanged",
         {{"urn", QJsonValue::String},
          {"title", QJsonValue::String},
          {"artist", QJsonValue::String},
          {"durationMs", QJsonValue::Double}}},
        {"player.playbackChanged", {{"playing", QJsonValue::Bool}, {"buffering", QJsonValue::Bool}}},
        {"player.progress", {{"positionMs", QJsonValue::Double}, {"durationMs", QJsonValue::Double}}},
        {"player.queueChanged", {{"count", QJsonValue::Double}, {"index", QJsonValue::Double}}},
        {"timer.expired", {{"timerId", QJsonValue::String}}}};
    for (auto v : o["rules"].toArray()) {
        auto r = v.toObject();
        const auto ev = r["event"].toString();
        if (!v.isObject() ||
            !fields(r, {"id", "event", "conditions", "actions"}, {"id", "event", "conditions", "actions"}) ||
            !id(r["id"]) || rules.contains(r["id"].toString()) || !events.contains(ev) ||
            !grants.contains(ev == "timer.expired" ? "timers" : "player.observe") ||
            !r["conditions"].isArray() || r["conditions"].toArray().size() > 4 || !r["actions"].isArray() ||
            r["actions"].toArray().isEmpty() || r["actions"].toArray().size() > 4)
            return "Некорректное правило.";
        rules.insert(r["id"].toString());
        for (auto condition : r["conditions"].toArray()) {
            auto c = condition.toObject();
            auto field = c["field"].toString();
            if (!condition.isObject() || !fields(c, {"field", "equals"}, {"field", "equals"}) ||
                !events[ev].contains(field) || c["equals"].type() != events[ev][field])
                return "Некорректное условие.";
            if (ev == "timer.expired" && field == "timerId")
                expired.insert(c["equals"].toString());
        }
        for (auto a : r["actions"].toArray()) {
            auto err = action(a, false);
            if (!err.isEmpty())
                return err;
        }
    }
    for (auto timer : timerIds)
        if (!expired.contains(timer))
            return "Для таймера требуется правило timer.expired с его ID.";
    return {};
}
QJsonObject migrateLegacy(const QJsonObject &document) {
    if (!document.contains("api") || document["api"] != 1 ||
        QJsonDocument(document).toJson(QJsonDocument::Compact).size() > 32768 || !legacy(document).isEmpty())
        return {};
    QJsonArray permissions, settings, contributions;
    bool hasTheme = false, hasPanels = false, hasNavigation = false;
    const auto groups = document["groups"].toArray();
    for (qsizetype i = 0; i < groups.size(); ++i) {
        const auto group = groups[i].toObject();
        const auto index = QString::number(i);
        const auto settingId = "legacy-group-" + index;
        settings.append(QJsonObject{{"id", settingId}, {"type", "boolean"},
                                    {"label", group["label"]}, {"default", true}});
        QJsonObject values;
        for (auto key : {"accent", "covers", "coverRadius"})
            if (group.contains(key)) values[key] = group[key];
        if (!values.isEmpty()) {
            hasTheme = true;
            contributions.append(QJsonObject{{"id", "legacy-theme-" + index}, {"type", "theme"},
                                             {"values", values}, {"enabledSetting", settingId}});
        }
        if (!group["page"].isUndefined()) {
            hasPanels = true;
            QJsonArray buttons;
            for (const auto value : group["buttons"].toArray()) {
                hasNavigation = true;
                buttons.append(QJsonObject{
                    {"label", value.toObject()["label"]},
                    {"action", QJsonObject{{"command", "navigation.open"},
                                            {"destination", value.toObject()["action"]}}}});
            }
            QJsonObject panel{{"id", "legacy-panel-" + index}, {"type", "panel"},
                              {"slot", group["page"].toString() + ".panels"},
                              {"title", group["label"]}, {"enabledSetting", settingId}};
            if (group.contains("text")) panel["text"] = group["text"];
            if (!buttons.isEmpty()) panel["buttons"] = buttons;
            contributions.append(panel);
        }
    }
    if (hasPanels) permissions.append("ui.panels");
    if (hasTheme) permissions.append("ui.theme");
    if (hasNavigation) permissions.append("navigation");
    return {{"api", 2}, {"kind", "declarative"}, {"id", document["id"]},
            {"name", document["name"]}, {"version", "1.0.0"},
            {"requiresHost", QJsonObject{{"min", "0.0.0"}, {"maxExclusive", "65535.65535.65535"}}},
            {"permissions", permissions}, {"settings", settings},
            {"contributions", contributions}, {"rules", QJsonArray{}}};
}
} // namespace PluginManifest
