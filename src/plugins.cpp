#include "plugins.h"
#include "pluginmanifest.h"
#include <QAtomicInt>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUuid>
#include <memory>

namespace {
QString storage() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins.json";
}
QString hostVersion() {
    auto v = QCoreApplication::applicationVersion();
    return PluginManifest::version(v) ? v : QStringLiteral("1.4.1");
}
QString pluginId(const QVariantMap &row) { return row["plugin"].toMap()["id"].toString(); }
QJsonObject manifest(const QVariantMap &row) { return QJsonObject::fromVariantMap(row["plugin"].toMap()); }
QStringList strings(const QJsonArray &a) {
    QStringList out;
    for (auto v : a)
        out.append(v.toString());
    return out;
}
bool hasCycle(const QVariantList &rows) {
    QHash<QString, QStringList> graph;
    for (auto v : rows) {
        auto row = v.toMap();
        if (row.value("retained").toBool())
            continue;
        QStringList deps;
        for (auto d : manifest(row)["dependencies"].toArray())
            deps.append(d.toObject()["id"].toString());
        graph[pluginId(row)] = deps;
    }
    QSet<QString> done, path;
    std::function<bool(const QString &)> visit = [&](const QString &id) {
        if (path.contains(id))
            return true;
        if (done.contains(id))
            return false;
        path.insert(id);
        for (auto dep : graph.value(id))
            if (visit(dep))
                return true;
        path.remove(id);
        done.insert(id);
        return false;
    };
    for (auto id : graph.keys())
        if (visit(id))
            return true;
    return false;
}
QAtomicInt readers;
QString readFile(const QUrl &url, QByteArray *bytes) {
    QString name;
    if (url.isLocalFile() && url.host().isEmpty()) {
        name = url.toLocalFile();
        if (!QFileInfo(name).isFile())
            return "Выберите обычный файл.";
    }
#ifdef Q_OS_ANDROID
    else if (url.scheme() == "content" && !url.authority().isEmpty())
        name = url.toString(QUrl::FullyEncoded);
#endif
    else
        return "Выберите локальный JSON-файл через системный диалог.";
    QFile file(name);
    if (!file.open(QIODevice::ReadOnly))
        return "Не удалось прочитать выбранный файл: " + file.errorString();
    while (bytes->size() <= 131072) {
        const auto chunk = file.read(qMin<qint64>(16384, 131073 - bytes->size()));
        if (chunk.isEmpty())
            break;
        bytes->append(chunk);
    }
    if (file.error() != QFileDevice::NoError)
        return "Ошибка чтения файла.";
    if (bytes->size() > 131072)
        return "Файл превышает 128 КиБ.";
    return {};
}
class ImportReader final : public QObject {
    Q_OBJECT
  public:
    QUrl url;
    explicit ImportReader(QUrl value) : url(std::move(value)) {}
    void run() {
        QByteArray bytes;
        auto error = readFile(url, &bytes);
        PluginManifest::Result result;
        if (error.isEmpty()) {
            result = PluginManifest::parse(bytes);
            error = result.error;
            if (error.isEmpty() && result.document["api"] != 2)
                error = "Поддерживается только декларативный формат плагинов.";
        }
        readers.deref();
        emit finished(
            result.document, error,
            QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
    }
  signals:
    void finished(const QJsonObject &document, const QString &error, const QString &hash);
};
} // namespace

Plugins::Plugins(QObject *parent) : QObject(parent) {
    m_clock.start();
    m_progressTimer.setInterval(1000);
    connect(&m_progressTimer, &QTimer::timeout, this, [this] {
        bool subscribed = false;
        for (auto value : entries) {
            auto row = value.toMap();
            if (!permitted(pluginId(row), "player.observe"))
                continue;
            for (auto r : manifest(row)["rules"].toArray())
                if (r.toObject()["event"] == "player.progress") subscribed = true;
        }
        if (!subscribed) return;
        emit progressSnapshotRequested();
        enqueueEvent("player.progress", {{"positionMs", m_player.value("position").toLongLong()},
                                          {"durationMs", m_player.value("duration").toLongLong()}});
    });
    m_progressTimer.start();

    auto normalize = [this](QVariantMap row) -> QVariantMap {
        auto p = manifest(row);
        if (!row.contains("retained")) row["retained"] = false;
        if (p["api"] == 1) {
            const auto flags = row.value("flags").toList();
            if (flags.size() != p["groups"].toArray().size()) return {};
            for (const auto &flag : flags)
                if (flag.metaType().id() != QMetaType::Bool) return {};
            const auto upgraded = PluginManifest::migrateLegacy(p);
            if (upgraded.isEmpty()) return {};
            QVariantMap settings;
            for (qsizetype i = 0; i < upgraded["settings"].toArray().size(); ++i) {
                const auto id = "legacy-group-" + QString::number(i);
                settings[id] = i < flags.size() ? flags[i].toBool() : true;
            }
            QVariantList grants;
            for (auto value : upgraded["permissions"].toArray()) grants.append(value.toString());
            row["plugin"] = upgraded.toVariantMap();
            row["settings"] = settings;
            row["orphanSettings"] = QVariantMap{};
            row["grants"] = grants;
            row["data"] = row.value("data", QVariantMap{});
            row.remove("flags");
        }
        return row;
    };
    auto validRow = [this](const QVariantMap &row) {
        const auto p = manifest(row);
        if (p["api"] != 2 || !PluginManifest::validate(p).isEmpty() ||
            row.value("enabled").metaType().id() != QMetaType::Bool ||
            (row.contains("retained") && row["retained"].metaType().id() != QMetaType::Bool) ||
            row.value("settings").metaType().id() != QMetaType::QVariantMap ||
            row.value("grants").metaType().id() != QMetaType::QVariantList || !dataValid(row)) return false;
        const auto requested = strings(p["permissions"].toArray());
        QSet<QString> granted;
        for (auto g : row["grants"].toList()) {
            if (g.metaType().id() != QMetaType::QString || !requested.contains(g.toString()) ||
                granted.contains(g.toString())) return false;
            granted.insert(g.toString());
        }
        const auto values = QJsonObject::fromVariantMap(row["settings"].toMap());
        if (values.size() != p["settings"].toArray().size()) return false;
        for (auto setting : p["settings"].toArray()) {
            const auto definition = setting.toObject();
            if (!PluginManifest::settingValue(definition, values[definition["id"].toString()])) return false;
        }
        return true;
    };
    const auto canonicalPath = storage();
    if (QFileInfo::exists(canonicalPath)) {
        QFile file(canonicalPath);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
            m_error = "Не удалось прочитать реестр плагинов. Восстановите файл из резервной копии.";
            return;
        }
        const auto bytes = file.readAll();
        m_diskHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject() || doc.object()["format"] != 2 ||
            !doc.object()["entries"].isArray()) {
            m_error = "Повреждён реестр плагинов. Автоматический откат запрещён; сохраните копию файла.";
            return;
        }
        m_revision = quint64(qMax<qint64>(0, doc.object()["registryRevision"].toInteger()));
        const auto rows = doc.object()["entries"].toArray();
        if (rows.size() > 24) {
            m_error = "Реестр плагинов превышает лимит или содержит недопустимые записи.";
            return;
        }
        for (const auto value : rows) {
            const auto row = value.toObject().toVariantMap();
            if (!value.isObject() || !validRow(row) || indexOf(pluginId(row)) >= 0) {
                entries.clear();
                m_error = "Реестр плагинов содержит недопустимые записи. Плагины не активированы.";
                return;
            }
            entries.append(row);
        }
        if (hasCycle(entries)) {
            entries.clear();
            m_error = "В реестре обнаружен цикл зависимостей. Плагины не активированы.";
            return;
        }
        reconcile();
        return;
    }

    const QStringList oldPaths{
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins-v1.json",
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins-v2.json"};
    QList<QByteArray> sourceBytes;
    QList<bool> sourceExists;
    for (const auto &path : oldPaths) {
        QFile source(path);
        sourceExists.append(source.exists());
        if (!source.exists()) { sourceBytes.append(QByteArray{}); continue; }
        if (!source.open(QIODevice::ReadOnly) || source.size() > 16 * 1024 * 1024) {
            m_error = "Не удалось прочитать старый реестр плагинов; исходные данные сохранены.";
            return;
        }
        sourceBytes.append(source.readAll());
    }
    if (!sourceExists[0] && !sourceExists[1]) return;

    bool corrupt = false;
    auto appendSource = [&](const QByteArray &bytes, bool oldFormat, bool exists) {
        if (!exists) return;
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError ||
            (oldFormat ? !doc.isArray() : (!doc.isObject() || doc.object()["format"] != 2 ||
                                           !doc.object()["entries"].isArray()))) {
            corrupt = true;
            return;
        }
        if (!oldFormat) m_revision = quint64(qMax<qint64>(0, doc.object()["registryRevision"].toInteger()));
        const auto rows = oldFormat ? doc.array() : doc.object()["entries"].toArray();
        if (rows.size() > 24 || entries.size() + rows.size() > 48) { corrupt = true; return; }
        for (const auto value : rows) {
            auto row = normalize(value.toObject().toVariantMap());
            if (!value.isObject() || row.isEmpty() || !validRow(row)) { corrupt = true; continue; }
            const auto duplicate = indexOf(pluginId(row));
            if (duplicate >= 0) {
                if (entries[duplicate].toMap() != row) corrupt = true;
                continue;
            }
            entries.append(row);
        }
    };
    appendSource(sourceBytes[1], false, sourceExists[1]);
    appendSource(sourceBytes[0], true, sourceExists[0]);
    if (corrupt || entries.size() > 24 || hasCycle(entries)) {
        entries.clear();
        m_error = "Старые реестры содержат повреждённые данные, конфликтующие копии или зависимости; "
                  "исходники не изменены.";
        return;
    }

    for (qsizetype i = 0; i < oldPaths.size(); ++i) {
        if (!sourceExists[i]) continue;
        const auto backupPath = oldPaths[i] + ".bak";
        if (QFileInfo::exists(backupPath)) {
            QFile backup(backupPath);
            if (!backup.open(QIODevice::ReadOnly) || backup.readAll() != sourceBytes[i]) {
                m_error = "Резервная копия старого реестра отличается; миграция остановлена без изменений.";
                entries.clear();
                return;
            }
        } else {
            QSaveFile backup(backupPath);
            if (!backup.open(QIODevice::WriteOnly) || backup.write(sourceBytes[i]) != sourceBytes[i].size() ||
                !backup.commit()) {
                m_error = "Не удалось создать резервную копию старого реестра; миграция остановлена.";
                entries.clear();
                return;
            }
        }
    }
    const auto error = save(entries);
    if (!error.isEmpty()) {
        entries.clear();
        m_error = "Ошибка атомарной миграции реестра: " + error;
        return;
    }
    reconcile();
}
int Plugins::indexOf(const QString &id) const {
    for (int i = 0; i < entries.size(); ++i)
        if (pluginId(entries[i].toMap()) == id)
            return i;
    return -1;
}
bool Plugins::dataValid(const QVariantMap &row) const {
    const auto data = row.value("data", QVariantMap{});
    const auto orphan = row.value("orphanSettings", QVariantMap{});
    if (data.metaType().id() != QMetaType::QVariantMap || orphan.metaType().id() != QMetaType::QVariantMap ||
        data.toMap().size() > 128)
        return false;
    static const QRegularExpression key("^[a-z][a-z0-9.-]{0,47}\\z");
    for (auto k : data.toMap().keys())
        if (!key.match(k).hasMatch())
            return false;
    const auto bytes =
        QJsonDocument(QJsonObject{{"data", QJsonObject::fromVariantMap(data.toMap())},
                                  {"orphanSettings", QJsonObject::fromVariantMap(orphan.toMap())}})
            .toJson(QJsonDocument::Compact);
    if (bytes.size() > 256 * 1024)
        return false;
    std::function<bool(const QJsonValue &, int)> depth = [&](const QJsonValue &v, int n) {
        if (n > 8)
            return false;
        if (v.isArray())
            for (auto x : v.toArray()) {
                if (!depth(x, n + 1))
                    return false;
            }
        if (v.isObject())
            for (auto x : v.toObject()) {
                if (!depth(x, n + 1))
                    return false;
            }
        return true;
    };
    return depth(QJsonObject::fromVariantMap(data.toMap()), 0) &&
           depth(QJsonObject::fromVariantMap(orphan.toMap()), 0);
}
QString Plugins::save(const QVariantList &next) {
    if (!m_error.isEmpty())
        return m_error;
    const auto bytes = QJsonDocument(QJsonObject{{"format", 2},
                                                 {"registryRevision", qint64(m_revision + 1)},
                                                 {"entries", QJsonArray::fromVariantList(next)}})
                           .toJson(QJsonDocument::Compact);
    if (next.size() > 24 || bytes.size() > 16 * 1024 * 1024)
        return "Превышена квота реестра.";
    QSaveFile file(storage());
    if (!QDir().mkpath(QFileInfo(file).absolutePath()))
        return "Не удалось создать каталог плагинов.";
    QLockFile lock(storage() + ".lock");
    if (!lock.tryLock(0))
        return "Реестр занят другим экземпляром приложения.";
    QFile current(storage());
    if (current.exists()) {
        if (!current.open(QIODevice::ReadOnly) || current.size() > 16 * 1024 * 1024)
            return "Не удалось проверить текущий реестр.";
        if (QCryptographicHash::hash(current.readAll(), QCryptographicHash::Sha256) != m_diskHash)
            return "Реестр изменён другим экземпляром. Перезапустите приложение перед изменением плагинов.";
    } else if (!m_diskHash.isEmpty())
        return "Реестр удалён другим экземпляром. Перезапустите приложение.";
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return "Не удалось сохранить изменения на диске.";
    for (auto oldValue : entries) {
        auto old = oldValue.toMap();
        const auto id = pluginId(old);
        QVariantMap replacement;
        for (auto v : next)
            if (pluginId(v.toMap()) == id)
                replacement = v.toMap();
        if (replacement.isEmpty() || old["plugin"] != replacement["plugin"] ||
            old["enabled"] != replacement["enabled"] || old["grants"] != replacement["grants"])
            stop(id);
    }
    m_diskHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    entries = next;
    ++m_revision;
    reconcile();
    emit changed();
    return {};
}
QString Plugins::reason(const QString &id) const {
    QSet<QString> visiting;
    return reason(id, visiting);
}
QString Plugins::reason(const QString &id, QSet<QString> &visiting) const {
    int i = indexOf(id);
    if (i < 0)
        return "Не установлен: " + id;
    const auto row = entries[i].toMap();
    const auto p = manifest(row);
    if (row.value("retained").toBool())
        return "Конфигурация сохранена; пакет удалён.";
    if (!row["enabled"].toBool())
        return "Отключён пользователем.";
    if (m_errors.contains(id))
        return m_errors.value(id);
    for (auto perm : strings(p["permissions"].toArray()))
        if (!row["grants"].toList().contains(perm))
            return "Требуется разрешение: " + perm;
    if (!PluginManifest::compatible(p["requiresHost"].toObject(), hostVersion()))
        return "Требуется SCrip ≥ " + p["requiresHost"].toObject()["min"].toString() + " и < " +
               p["requiresHost"].toObject()["maxExclusive"].toString() + "; установлен " + hostVersion();
    if (visiting.contains(id))
        return "Цикл зависимостей.";
    visiting.insert(id);
    for (auto dep : p["dependencies"].toArray()) {
        auto d = dep.toObject();
        const auto depId = d["id"].toString();
        int j = indexOf(depId);
        if (j < 0 || manifest(entries[j].toMap())["api"] != 2 ||
            !PluginManifest::compatible(d, manifest(entries[j].toMap())["version"].toString())) {
            visiting.remove(id);
            return "Недоступна совместимая зависимость: " + depId;
        }
        auto problem = reason(depId, visiting);
        if (!problem.isEmpty()) {
            visiting.remove(id);
            return depId + ": " + problem;
        }
    }
    visiting.remove(id);
    return {};
}
void Plugins::stop(const QString &id) {
    ++m_generation[id];
    for (int i = m_events.size() - 1; i >= 0; --i)
        if (m_events[i].id == id)
            m_events.removeAt(i);
    const auto timers = m_timers.take(id);
    for (auto timer : timers) {
        timer.object->stop();
        timer.object->deleteLater();
    }
}
void Plugins::reconcile() {
    // Сначала останавливаются зависимые: топологический порядок завершения.
    QSet<QString> done;
    std::function<void(const QString &)> visit = [&](const QString &id) {
        if (done.contains(id))
            return;
        done.insert(id);
        for (auto value : entries)
            for (auto d : manifest(value.toMap())["dependencies"].toArray())
                if (d.toObject()["id"] == id)
                    visit(pluginId(value.toMap()));
        if (!reason(id).isEmpty())
            stop(id);
    };
    for (auto value : entries)
        visit(pluginId(value.toMap()));
}
bool Plugins::permitted(const QString &id, const QString &permission) const {
    const auto i = indexOf(id);
    if (i < 0 || !reason(id).isEmpty())
        return false;
    return entries[i].toMap()["grants"].toList().contains(permission);
}
QVariantList Plugins::installed() const {
    QVariantList result;
    QHash<QString, QString> owners;
    const auto activePanels = panels();
    for (auto value : activePanels) {
        auto panel = value.toMap();
        for (auto key : {"accent", "covers", "coverRadius"})
            if (panel.contains(key))
                owners[key] = panel["pluginId"].toString();
    }
    for (auto value : entries) {
        auto row = value.toMap();
        const auto id = pluginId(row);
        const auto p = manifest(row);
        const auto why = reason(id);
        QStringList themeStatus;
        for (auto value : activePanels) {
            auto panel = value.toMap();
            if (panel["pluginId"] != id)
                continue;
            for (auto key : {"accent", "covers", "coverRadius"})
                if (panel.contains(key))
                    themeStatus.append(QString(key) + (owners.value(key) == id
                                                           ? ": применяется"
                                                           : ": перекрыт плагином " + owners.value(key)));
        }
        row["themeStatus"] = themeStatus.join("; ");
        QString status = "active";
        if (row.value("retained").toBool())
            status = "removed";
        else if (!row["enabled"].toBool())
            status = "disabled";
        else if (m_errors.contains(id))
            status = "error";
        else if (why.startsWith("Требуется разрешение:"))
            status = "awaiting-permissions";
        else if (!why.isEmpty())
            status = "blocked";
        row["status"] = status;
        row["reason"] = why;
        row["lastError"] = m_logs.value(id).isEmpty() ? QString() : m_logs.value(id).last();
        row["log"] = m_logs.value(id);
        row["version"] = p["version"].toString();
        QVariantList settingRows;
        for (auto s : p["settings"].toArray()) {
            auto setting = s.toObject().toVariantMap();
            setting["value"] = row["settings"].toMap().value(setting["id"].toString());
            settingRows.append(setting);
        }
        row["settingRows"] = settingRows;
        QVariantList permissionRows;
        const QHash<QString, QString> descriptions{
            {"ui.panels", "Панели и кнопки"},
            {"ui.theme", "Изменение оформления"},
            {"player.observe", "Названия и исполнители треков, состояние плеера (персональные данные)"},
            {"player.control", "Управление воспроизведением"},
            {"navigation", "Переходы между страницами"},
            {"storage", "Собственные данные"},
            {"timers", "Одноразовые таймеры"}};
        for (auto permission : strings(p["permissions"].toArray()))
            permissionRows.append(QVariantMap{{"id", permission},
                                              {"label", descriptions.value(permission)},
                                              {"granted", row["grants"].toList().contains(permission)}});
        row["permissionRows"] = permissionRows;
        QVariantList timerRows;
        const auto timers = m_timers.value(id);
        for (auto it = timers.cbegin(); it != timers.cend(); ++it)
            timerRows.append(QVariantMap{
                {"id", it.key()},
                {"remainingMinutes", qMax<qint64>(0, (it->deadline - m_clock.elapsed() + 59999) / 60000)}});
        row["timers"] = timerRows;
        result.append(row);
    }
    return result;
}
QString Plugins::candidate(const QJsonObject &p, QVariantList *next, QStringList *changes) const {
    *next = entries;
    const auto id = p["id"].toString();
    int i = indexOf(id);
    auto old = i < 0 ? QVariantMap{} : entries[i].toMap();
    auto previous = manifest(old);
    if (i < 0 && entries.size() >= 24)
        return "Достигнут предел 24 записей, включая сохранённые конфигурации.";
    if (i >= 0 && old.value("retained").toBool() &&
        PluginManifest::compareVersions(p["version"].toString(), previous["version"].toString()) < 0)
        return "Понижение сохранённой версии запрещено.";
    if (i >= 0 && !old.value("retained").toBool()) {
        if (PluginManifest::compareVersions(p["version"].toString(), previous["version"].toString()) <= 0)
            return "Новая версия должна быть больше установленной.";
        changes->append("Замена установленного пакета. Совпадение ID не подтверждает автора.");
    }
    if (i >= 0) {
        const auto oldPermissions = strings(previous["permissions"].toArray());
        const auto newPermissions = strings(p["permissions"].toArray());
        for (auto permission : newPermissions)
            if (!oldPermissions.contains(permission))
                changes->append("Новое разрешение: " + permission);
        for (auto permission : oldPermissions)
            if (!newPermissions.contains(permission))
                changes->append("Удалено разрешение: " + permission);
    }
    QVariantMap row{
        {"plugin", p.toVariantMap()}, {"enabled", old.value("enabled", false)}, {"retained", false}};
    {
        QVariantMap values;
        auto orphan = old["orphanSettings"].toMap();
        const auto oldValues = old["settings"].toMap();
        for (auto it = oldValues.begin(); it != oldValues.end(); ++it)
            orphan[it.key()] = it.value();
        for (auto value : p["settings"].toArray()) {
            auto s = value.toObject();
            const auto key = s["id"].toString();
            auto saved = QJsonValue::fromVariant(orphan.value(key));
            if (orphan.contains(key) && PluginManifest::settingValue(s, saved))
                values[key] = saved.toVariant();
            else {
                values[key] = s["default"].toVariant();
                if (orphan.contains(key))
                    changes->append("Настройка «" + key + "» заменена значением по умолчанию.");
            }
            orphan.remove(key);
        }
        row["settings"] = values;
        row["orphanSettings"] = orphan;
        row["data"] = old.value("data", QVariantMap{});
        QVariantList grants;
        for (auto permission : strings(p["permissions"].toArray()))
            if (old["grants"].toList().contains(permission))
                grants.append(permission);
        row["grants"] = grants;
        if (!dataValid(row))
            return "Данные и сохранённые настройки превышают квоту 256 КиБ или глубину 8.";
    }
    if (i < 0)
        next->append(row);
    else
        (*next)[i] = row;
    if (hasCycle(*next))
        return "Цикл зависимостей запрещён.";
    // Обновление зависимости не должно незаметно блокировать включённые пакеты.
    if (i >= 0)
        for (auto value : *next) {
            auto dependent = value.toMap();
            if (!dependent["enabled"].toBool() || dependent.value("retained").toBool())
                continue;
            for (auto d : manifest(dependent)["dependencies"].toArray()) {
                auto dep = d.toObject();
                if (dep["id"] == id &&
                    !PluginManifest::compatible(dep, p["version"].toString()))
                    return "Сначала отключите зависимый плагин: " + pluginId(dependent);
            }
        }
    return {};
}
QVariantMap Plugins::prepareInstall(const QUrl &url) {
    if (!m_error.isEmpty())
        return {{"error", m_error}};
    for (auto key : m_prepared.keys())
        if (m_prepared[key].expires < m_clock.elapsed())
            m_prepared.remove(key);
    if (m_pending.size() + m_prepared.size() >= 2 || readers.loadRelaxed() >= 2)
        return {{"error", "Дождитесь завершения или отмените предыдущую установку."}};
    const auto operation = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pending.insert(operation);
    readers.ref();
    auto reader = std::shared_ptr<ImportReader>(new ImportReader(url),
                                                [](ImportReader *value) { value->deleteLater(); });
    connect(
        reader.get(), &ImportReader::finished, this,
        [this, operation](const QJsonObject &document, const QString &error, const QString &hash) {
            if (!m_pending.remove(operation))
                return;
            QString problem = error;
            QVariantList next;
            QStringList changes;
            if (problem.isEmpty())
                problem = candidate(document, &next, &changes);
            if (!problem.isEmpty()) {
                emit installPrepared({{"operationId", operation}, {"error", problem}});
                return;
            }
            m_prepared.insert(operation, {document, m_revision, m_clock.elapsed() + 300000, hash});
            const auto i = indexOf(document["id"].toString());
            emit installPrepared(
                {{"operationId", operation},
                 {"plugin", document.toVariantMap()},
                 {"permissions", document["permissions"].toArray().toVariantList()},
                 {"changes", changes},
                 {"previousVersion", i < 0 ? QString() : installed()[i].toMap()["version"].toString()},
                 {"sha256", hash},
                 {"error", QString()}});
        },
        Qt::QueuedConnection);
    QThreadPool::globalInstance()->start([reader] { reader->run(); });
    return {{"operationId", operation}, {"error", QString()}};
}
void Plugins::cancelInstall(const QString &id) {
    m_prepared.remove(id);
    m_pending.remove(id);
}
QString Plugins::commitInstall(const QString &operation, const QStringList &accepted) {
    if (!m_prepared.contains(operation))
        return "Предпросмотр отсутствует или отменён.";
    const auto prepared = m_prepared.take(operation);
    if (prepared.revision != m_revision || prepared.expires < m_clock.elapsed())
        return "Предпросмотр устарел. Выберите файл повторно.";
    const auto requested = strings(prepared.document["permissions"].toArray());
    if (QSet<QString>(accepted.begin(), accepted.end()) != QSet<QString>(requested.begin(), requested.end()))
        return "Для установки согласуйте все запрошенные разрешения или отмените операцию.";
    QVariantList next;
    QStringList changes;
    auto error = candidate(prepared.document, &next, &changes);
    if (!error.isEmpty())
        return error;
    int i = -1;
    for (int n = 0; n < next.size(); ++n)
        if (pluginId(next[n].toMap()) == prepared.document["id"].toString())
            i = n;
    auto row = next[i].toMap();
    if (prepared.document["api"] == 2)
        row["grants"] = QVariantList(requested.begin(), requested.end());
    next[i] = row;
    error = save(next);
    if (error.isEmpty()) {
        stop(prepared.document["id"].toString());
        m_budgets.remove(prepared.document["id"].toString());
        m_errors.remove(prepared.document["id"].toString());
        emit changed();
    }
    return error;
}
QString Plugins::enable(const QString &id, bool value) {
    int i = indexOf(id);
    if (i < 0 || entries[i].toMap().value("retained").toBool())
        return "Плагин не установлен.";
    auto next = entries;
    auto row = next[i].toMap();
    row["enabled"] = value;
    next[i] = row;
    auto result = save(next);
    if (result.isEmpty()) {
        m_errors.remove(id);
        m_budgets.remove(id);
        stop(id);
        reconcile();
        emit changed();
    }
    return result;
}
QString Plugins::setPermission(const QString &id, const QString &permission, bool value) {
    int i = indexOf(id);
    if (i < 0 || !strings(manifest(entries[i].toMap())["permissions"].toArray()).contains(permission))
        return "Неизвестное разрешение.";
    auto next = entries;
    auto row = next[i].toMap();
    auto grants = row["grants"].toList();
    grants.removeAll(permission);
    if (value)
        grants.append(permission);
    row["grants"] = grants;
    next[i] = row;
    auto error = save(next);
    if (error.isEmpty()) {
        stop(id);
        reconcile();
        emit changed();
    }
    return error;
}
QString Plugins::setSetting(const QString &id, const QString &key, const QVariant &value) {
    int i = indexOf(id);
    if (i < 0)
        return "Плагин не найден.";
    bool found = false;
    for (auto s : manifest(entries[i].toMap())["settings"].toArray())
        if (s.toObject()["id"] == key)
            found = PluginManifest::settingValue(s.toObject(), QJsonValue::fromVariant(value));
    if (!found)
        return "Недопустимое значение настройки.";
    auto next = entries;
    auto row = next[i].toMap();
    auto settings = row["settings"].toMap();
    settings[key] = value;
    row["settings"] = settings;
    next[i] = row;
    return save(next);
}
QStringList Plugins::dependents(const QString &id) const {
    QSet<QString> seen{id};
    QStringList result;
    bool added = true;
    while (added) {
        added = false;
        for (auto value : entries) {
            auto row = value.toMap();
            auto own = pluginId(row);
            if (seen.contains(own))
                continue;
            for (auto d : manifest(row)["dependencies"].toArray())
                if (seen.contains(d.toObject()["id"].toString())) {
                    seen.insert(own);
                    result.append(own);
                    added = true;
                    break;
                }
        }
    }
    return result;
}
QString Plugins::remove(const QString &id, bool keep) {
    int i = indexOf(id);
    if (i < 0)
        return "Плагин не найден.";
    auto next = entries;
    if (keep) {
        auto row = next[i].toMap();
        row["enabled"] = false;
        row["retained"] = true;
        next[i] = row;
    } else
        next.removeAt(i);
    // Диалог UI перечисляет зависимых и явно подтверждает их отключение.
    const auto affected = dependents(id);
    for (int n = 0; n < next.size(); ++n)
        if (affected.contains(pluginId(next[n].toMap()))) {
            auto row = next[n].toMap();
            row["enabled"] = false;
            next[n] = row;
        }
    auto error = save(next);
    if (error.isEmpty()) {
        stop(id);
        m_errors.remove(id);
        emit changed();
    }
    return error;
}
QString Plugins::clearData(const QString &id) {
    int i = indexOf(id);
    if (i < 0)
        return "Плагин не найден.";
    auto next = entries;
    auto row = next[i].toMap();
    row["data"] = QVariantMap{};
    row["orphanSettings"] = QVariantMap{};
    next[i] = row;
    return save(next);
}
void Plugins::clearError(const QString &id) {
    m_logs.remove(id);
    if (indexOf(id) >= 0 && !entries[indexOf(id)].toMap()["enabled"].toBool())
        m_errors.remove(id);
    emit changed();
}
void Plugins::recordError(const QString &id, const QString &message, bool fatal) {
    auto &log = m_logs[id];
    log.append(message);
    while (log.size() > 20)
        log.removeFirst();
    if (fatal) {
        m_errors[id] = message;
        stop(id);
        reconcile();
    }
    emit changed();
}
QVariantList Plugins::panels() const {
    QVariantList result;
    for (auto value : entries) {
        auto row = value.toMap();
        const auto id = pluginId(row);
        if (!reason(id).isEmpty())
            continue;
        const auto p = manifest(row);
            for (auto contribution : p["contributions"].toArray()) {
                auto c = contribution.toObject();
                if (c.contains("enabledSetting") &&
                    !row["settings"].toMap().value(c["enabledSetting"].toString()).toBool())
                    continue;
                if (c["type"] == "theme") {
                    auto t = c["values"].toObject().toVariantMap();
                    t["pluginId"] = id;
                    result.append(t);
                    continue;
                }
                auto panel = c.toVariantMap();
                panel["page"] = c["slot"] == "home.panels"      ? "home"
                                : c["slot"] == "profile.panels" ? "profile"
                                                                : "player";
                panel["label"] = c["title"].toString();
                panel["pluginId"] = id;
                panel["contributionId"] = c["id"].toString();
                QStringList timers;
                const auto own = m_timers.value(id);
                for (auto it = own.cbegin(); it != own.cend(); ++it)
                    timers.append(it.key() + ": до " +
                                  QDateTime::currentDateTime()
                                      .addMSecs(qMax<qint64>(0, it->deadline - m_clock.elapsed()))
                                      .toString("HH:mm"));
                panel["timerText"] = timers.join(", ");
                result.append(panel);
            }
    }
    return result;
}
QVariantMap Plugins::theme() const {
    QVariantMap result;
    for (auto value : panels()) {
        auto p = value.toMap();
        for (auto key : {"accent", "covers", "coverRadius"})
            if (p.contains(key))
                result[key] = p[key];
    }
    return result;
}
QString Plugins::invokeButton(const QString &id, const QString &contribution, int button) {
    if (!reason(id).isEmpty())
        return "Плагин не активен.";
    auto row = entries[indexOf(id)].toMap();
    auto p = manifest(row);
    for (auto value : p["contributions"].toArray()) {
        auto c = value.toObject();
        if (c["id"] != contribution || c["type"] != "panel")
            continue;
        if (c.contains("enabledSetting") && !row["settings"].toMap()[c["enabledSetting"].toString()].toBool())
            return "Панель отключена.";
        auto buttons = c["buttons"].toArray();
        if (button < 0 || button >= buttons.size())
            return "Кнопка не найдена.";
        return execute(id, buttons[button].toObject()["action"].toObject(), true, 0);
    }
    return "Панель не найдена.";
}
QString Plugins::execute(const QString &id, const QJsonObject &a, bool button, int depth) {
    const auto cmd = a["command"].toString();
    const auto permission = PluginManifest::commandPermission(cmd);
    QString error, code = "unavailable";
    if (!reason(id).isEmpty()) {
        error = "Плагин не активен.";
        code = "inactive";
    } else if (permission.isEmpty() || !permitted(id, permission)) {
        error = "Разрешение отозвано.";
        code = "permission-denied";
    } else if (depth > 4) {
        error = "Превышена глубина цепочки событий.";
        code = "rate-limited";
    }
    auto allow = [this](Budget &budget, int limit) {
        const auto now = m_clock.elapsed();
        if (now - budget.start >= 1000)
            budget = {now, 0};
        return ++budget.count <= limit;
    };
    if (error.isEmpty() && (!allow(m_budgets[id], 8) || !allow(m_globalBudget, 64))) {
        error = "Превышен лимит действий.";
        code = "rate-limited";
    }
    if (error.isEmpty()) {
        const int oldDepth = m_depth;
        m_depth = depth + 1;
        if (cmd.startsWith("player.")) {
            QStringList caused;
            if (cmd == "player.pause" || cmd == "player.resume")
                caused.append("player.playbackChanged");
            if (cmd == "player.next" || cmd == "player.previous")
                caused.append({"player.trackChanged", "player.queueChanged", "player.playbackChanged"});
            if (cmd == "player.seek")
                caused.append("player.progress");
            for (auto name : caused)
                m_causes[name] = {depth + 1, m_clock.elapsed() + 30000};
            if (m_playerCommand)
                error = m_playerCommand(cmd, a);
            else
                error = "Плеер недоступен.";
            if (!error.isEmpty())
                for (auto name : caused)
                    m_causes.remove(name);
        } else if (cmd == "navigation.open") {
            if (button)
                emit navigationRequested(a["destination"].toString());
            else
                error = "Навигация требует нажатия.";
        } else if (cmd.startsWith("timer.")) {
            const auto timerId = a["timerId"].toString();
            auto &own = m_timers[id];
            if (own.contains(timerId)) {
                auto old = own.take(timerId);
                old.object->stop();
                old.object->deleteLater();
            }
            if (cmd == "timer.start") {
                if (!button || own.size() >= 2)
                    error = "Не более двух таймеров; запуск только кнопкой.";
                else {
                    const auto minutes = entries[indexOf(id)]
                                             .toMap()["settings"]
                                             .toMap()[a["minutesSetting"].toString()]
                                             .toInt();
                    auto *timer = new QTimer(this);
                    timer->setSingleShot(true);
                    const auto generation = m_generation[id];
                    own.insert(timerId, {timer, generation, m_clock.elapsed() + qint64(minutes) * 60000});
                    connect(timer, &QTimer::timeout, this, [this, id, timerId, generation, timer] {
                        if (m_generation.value(id) != generation || !permitted(id, "timers"))
                            return;
                        m_timers[id].remove(timerId);
                        timer->deleteLater();
                        enqueueEvent("timer.expired", {{"timerId", timerId}, {"owner", id}});
                        emit changed();
                    });
                    timer->start(minutes * 60000);
                }
            }
            emit changed();
        } else if (cmd.startsWith("storage.")) {
            const int i = indexOf(id);
            auto next = entries;
            auto row = next[i].toMap();
            auto data = row["data"].toMap();
            if (cmd == "storage.set")
                data[a["key"].toString()] = a["value"].toVariant();
            else
                data.remove(a["key"].toString());
            row["data"] = data;
            if (!dataValid(row)) {
                error = "Превышена квота данных.";
                code = "storage-error";
            } else {
                next[i] = row;
                error = save(next);
                if (!error.isEmpty())
                    code = "storage-error";
            }
        }
        m_depth = oldDepth;
    }
    if (!error.isEmpty())
        recordError(id, error, code == "rate-limited");
    emit commandFinished({{"requestId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                          {"pluginId", id},
                          {"ok", error.isEmpty()},
                          {"code", error.isEmpty() ? QString() : code},
                          {"message", error}});
    return error;
}
void Plugins::enqueueEvent(const QString &name, const QJsonObject &payload) {
    int causeDepth = m_depth;
    if (m_causes.contains(name)) {
        const auto cause = m_causes.take(name);
        if (cause.second >= m_clock.elapsed())
            causeDepth = qMax(causeDepth, cause.first);
    }
    for (auto value : entries) {
        const auto row = value.toMap();
        const auto id = pluginId(row);
        if (!reason(id).isEmpty() || manifest(row)["api"] != 2)
            continue;
        if (name == "timer.expired" && payload["owner"] != id)
            continue;
        if (!permitted(id, name == "timer.expired" ? "timers" : "player.observe"))
            continue;
        bool subscribed = false;
        for (auto r : manifest(row)["rules"].toArray())
            if (r.toObject()["event"] == name)
                subscribed = true;
        if (!subscribed)
            continue;
        if (name == "player.progress")
            for (int i = m_events.size() - 1; i >= 0; --i)
                if (m_events[i].id == id && m_events[i].name == name)
                    m_events.removeAt(i);
        int count = 0;
        for (const auto &event : m_events)
            if (event.id == id)
                ++count;
        if (count >= 32) {
            recordError(id, "Переполнена очередь событий.", true);
            continue;
        }
        auto data = payload;
        data.remove("owner");
        m_events.enqueue({id, name, data, m_generation.value(id), causeDepth});
    }
    if (!m_drainPending && !m_events.isEmpty()) {
        m_drainPending = true;
        QTimer::singleShot(0, this, &Plugins::drain);
    }
}
void Plugins::drain() {
    m_drainPending = false;
    // Не более восьми действий за один проход event loop.
    if (m_events.isEmpty())
        return;
    auto event = m_events.dequeue();
    if (m_generation.value(event.id) == event.generation && reason(event.id).isEmpty() &&
        permitted(event.id, event.name == "timer.expired" ? "timers" : "player.observe")) {
        const auto p = manifest(entries[indexOf(event.id)].toMap());
        QJsonArray actions;
        for (auto value : p["rules"].toArray()) {
            auto rule = value.toObject();
            if (rule["event"] != event.name)
                continue;
            bool matches = true;
            for (auto c : rule["conditions"].toArray()) {
                auto condition = c.toObject();
                if (event.payload[condition["field"].toString()] != condition["equals"])
                    matches = false;
            }
            if (matches)
                for (auto a : rule["actions"].toArray())
                    actions.append(a);
        }
        const int end = qMin(event.offset + 8, int(actions.size()));
        for (int i = event.offset; i < end; ++i) {
            if (m_generation.value(event.id) != event.generation)
                break;
            execute(event.id, actions[i].toObject(), false, event.depth);
        }
        if (end < actions.size() && m_generation.value(event.id) == event.generation) {
            event.offset = end;
            m_events.prepend(event);
        }
    }
    if (!m_events.isEmpty() && !m_drainPending) {
        m_drainPending = true;
        QTimer::singleShot(0, this, &Plugins::drain);
    }
}
void Plugins::updatePlayer(const QVariantMap &state) {
    const auto old = m_player;
    // Передаём только поля Plugin API: профиль, URL и произвольные данные API
    // не должны попадать в контекст правил и диагностику плагинов.
    const auto track = state.value("current").toMap();
    QVariantList queue;
    for (const auto &item : state.value("queue").toList())
        queue.append(QVariantMap{{"urn", item.toMap().value("urn")}});
    m_player = {{"signedIn", state.value("signedIn").toBool()},
        {"playing", state.value("playing")}, {"buffering", state.value("buffering")},
        {"queue", queue}, {"queueIndex", state.value("queueIndex")},
        {"position", state.value("position")}, {"duration", state.value("duration")},
        {"current", QVariantMap{{"urn", track.value("urn")}, {"title", track.value("title")},
            {"duration", track.value("duration", state.value("duration"))},
            {"user", QVariantMap{{"username", track.value("user").toMap().value("username")}}}}}};
    if (old.isEmpty())
        return; // Начальный снимок не запускает автоматизацию.
    if (old.value("signedIn").toBool() && !m_player.value("signedIn").toBool()) {
        for (auto value : entries)
            stop(pluginId(value.toMap()));
        m_events.clear();
        m_causes.clear();
        emit changed();
        return;
    }
    if (old.value("current").toMap().value("urn") != m_player.value("current").toMap().value("urn")) {
        auto t = m_player.value("current").toMap();
        enqueueEvent("player.trackChanged",
                     {{"urn", t.value("urn").toString()},
                      {"title", t.value("title").toString()},
                      {"artist", t.value("user").toMap().value("username").toString()},
                      {"durationMs", t.value("duration", state.value("duration")).toLongLong()}});
    }
    if (old.value("playing") != m_player.value("playing") || old.value("buffering") != m_player.value("buffering"))
        enqueueEvent("player.playbackChanged", {{"playing", m_player.value("playing").toBool()},
                                                {"buffering", m_player.value("buffering").toBool()}});
    if (old.value("queue") != m_player.value("queue") || old.value("queueIndex") != m_player.value("queueIndex"))
        enqueueEvent("player.queueChanged", {{"count", m_player.value("queue").toList().size()},
                                             {"index", m_player.value("queueIndex").toInt()}});
}
#include "plugins.moc"
