#include "trackmetadata.h"
#include "discordpresence.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSettings>
#include <QUuid>
#include <QUrl>
#include <QtEndian>

Q_LOGGING_CATEGORY(discordPresenceLog, "scrip.discord", QtWarningMsg)

namespace {
// Shared project application, retained from v0.5.0 shared/constants.js.
constexpr auto defaultApplicationId = "1555593977367887893";
constexpr auto projectUrl = "https://github.com/sykia/sange";
constexpr auto projectIcon = "https://raw.githubusercontent.com/sykia/sange/main/packaging/linux/scrip.png";
QString playbackTime(qint64 milliseconds)
{
    const auto seconds = milliseconds / 1000;
    return QString::number(seconds / 60) + ":" + QString::number(seconds % 60).rightJustified(2, '0');
}
QString selectedApplicationId(const QString &id)
{
    return id.trimmed().isEmpty() ? QString::fromLatin1(defaultApplicationId) : id.trimmed();
}
QString clipped(QString text, qsizetype maxBytes = 128)
{
    // Discord limits activity text to 128 UTF-8 bytes. Preserve Unicode boundaries.
    while (text.toUtf8().size() > maxBytes) {
        if (text.back().isLowSurrogate()) text.chop(2);
        else text.chop(1);
    }
    return text;
}
bool publicUrl(const QString &text)
{
    const QUrl url(text);
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty()
        && url.userInfo().isEmpty();
}
}

bool DiscordPresence::available()
{
#ifdef Q_OS_ANDROID
    return false;
#else
    return true;
#endif
}

DiscordPresence::DiscordPresence(QObject *parent) : QObject(parent)
{
    QSettings settings;
    m_enabled = settings.value("discord/enabled", false).toBool();
    m_applicationId = selectedApplicationId(settings.value("discord/applicationId").toString());
    m_retry.setSingleShot(true);
    m_retry.setInterval(15000);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(4000);
    m_publish.setSingleShot(true);
    m_publish.setInterval(1000);
    connect(&m_retry, &QTimer::timeout, this, [this] { m_endpoint = 0; connectNext(); });
    connect(&m_timeout, &QTimer::timeout, this, [this] { m_socket.abort(); connectNext(); });
    connect(&m_publish, &QTimer::timeout, this, &DiscordPresence::publish);
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        send(0, QJsonDocument(QJsonObject{{"v", 1}, {"client_id", m_applicationId}}).toJson(QJsonDocument::Compact));
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &DiscordPresence::readFrames);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this] {
        m_timeout.stop();
        m_ready = false;
        QTimer::singleShot(0, this, &DiscordPresence::connectNext);
    });
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        if (!m_ready) return;
        m_ready = false;
        m_sent = false;
        setStatus("Discord отключён. Ожидание подключения…");
        if (configured()) m_retry.start();
    });
    connect(qApp, &QCoreApplication::aboutToQuit, this, &DiscordPresence::close);
    restart();
}

DiscordPresence::~DiscordPresence() { close(); }

bool DiscordPresence::configured() const
{
    static const QRegularExpression snowflake("^[0-9]{17,20}$");
    return available() && m_enabled && snowflake.match(m_applicationId).hasMatch();
}

void DiscordPresence::setStatus(const QString &status)
{
    if (m_status == status) return;
    m_status = status;
    emit statusChanged();
}

void DiscordPresence::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    QSettings().setValue("discord/enabled", enabled);
    restart();
    emit settingsChanged();
}

void DiscordPresence::setApplicationId(const QString &id)
{
    const auto selected = selectedApplicationId(id);
    if (m_applicationId == selected) return;
    close(); // Clear the old application's presence before switching IDs.
    m_applicationId = selected;
    QSettings().setValue("discord/applicationId", m_applicationId);
    restart();
    emit settingsChanged();
}

void DiscordPresence::close()
{
    m_retry.stop();
    m_timeout.stop();
    m_publish.stop();
    if (m_ready) {
        const QJsonObject command{{"cmd", "SET_ACTIVITY"}, {"nonce", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"args", QJsonObject{{"pid", QCoreApplication::applicationPid()}, {"activity", QJsonValue::Null}}}};
        send(1, QJsonDocument(command).toJson(QJsonDocument::Compact));
        m_socket.flush();
    }
    m_ready = false;
    m_socket.abort();
    m_sent = false;
    m_input.clear();
}

void DiscordPresence::restart()
{
    close();
    m_rejected = false;
    if (!available()) { setStatus("Discord RPC доступен на Linux и Windows"); return; }
    if (!m_enabled) { setStatus("Выключено"); return; }
    if (!configured()) { setStatus("Укажите Application ID приложения Sange в Discord"); return; }
    m_endpoints.clear();
#ifdef Q_OS_WIN
    for (int i = 0; i < 10; ++i) m_endpoints << QString("discord-ipc-%1").arg(i);
#else
    QStringList roots;
    for (const char *name : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        const auto root = qEnvironmentVariable(name);
        if (!root.isEmpty()) roots << root;
    }
    roots << "/tmp";
    roots.removeDuplicates();
    for (const auto &root : roots) {
        // Native, Flatpak and Snap Discord installations.
        for (const auto &sub : {QString(), QString("/app/com.discordapp.Discord"), QString("/snap.discord")})
            for (int i = 0; i < 10; ++i)
                m_endpoints << QDir::cleanPath(root + sub + QString("/discord-ipc-%1").arg(i));
    }
#endif
    m_endpoint = 0;
    connectNext();
}

void DiscordPresence::connectNext()
{
    if (!configured() || m_ready || m_rejected || m_retry.isActive()
        || m_socket.state() != QLocalSocket::UnconnectedState) return;
    m_input.clear();
    m_sent = false;
    if (m_endpoint >= m_endpoints.size()) {
        setStatus("Ожидание настольного Discord…");
        m_retry.start();
        return;
    }
    setStatus("Подключение к Discord…");
    m_socket.connectToServer(m_endpoints.at(m_endpoint++));
    if (m_socket.state() != QLocalSocket::UnconnectedState) m_timeout.start();
}

void DiscordPresence::send(quint32 opcode, const QByteArray &payload)
{
    QByteArray frame(8, '\0');
    qToLittleEndian(opcode, frame.data());
    qToLittleEndian(quint32(payload.size()), frame.data() + 4);
    frame.append(payload);
    m_socket.write(frame);
}

void DiscordPresence::readFrames()
{
    m_input += m_socket.readAll();
    while (m_input.size() >= 8) {
        const auto opcode = qFromLittleEndian<quint32>(m_input.constData());
        const auto length = qFromLittleEndian<quint32>(m_input.constData() + 4);
        if (length > 1024 * 1024) { close(); setStatus("Некорректный ответ Discord"); m_retry.start(); return; }
        if (m_input.size() < 8 + qsizetype(length)) return;
        const auto payload = m_input.mid(8, length);
        m_input.remove(0, 8 + length);
        if (opcode == 3) { send(4, payload); continue; }
        if (opcode == 2) {
            close();
            setStatus("Discord закрыл подключение. Повторная попытка…");
            m_retry.start();
            return;
        }
        if (opcode != 1) continue;
        const auto message = QJsonDocument::fromJson(payload).object();
        if (message.value("evt") == "ERROR") {
            close();
            m_rejected = true;
            setStatus("Discord отклонил RPC. Проверьте Application ID и включите интеграцию заново.");
            return;
        }
        if (message.value("cmd") == "SET_ACTIVITY") {
            const auto accepted = message.value("data").toObject();
            // Opt-in diagnostics deliberately exclude track titles, artists and URLs.
            qCDebug(discordPresenceLog) << "Activity accepted; name:" << accepted.value("name").toString()
                << "type:" << accepted.value("type").toInt()
                << "timeline:" << accepted.value("timestamps").toObject().contains("end")
                << "buttons:" << accepted.value("buttons").toArray().size();
        }
        if (message.value("evt") == "READY" && !m_ready) {
            m_timeout.stop();
            m_ready = true;
            setStatus("Подключено к Discord");
            publish();
        }
    }
}

QJsonValue DiscordPresence::activity(const QVariantMap &state, qint64 nowSeconds)
{
    const auto track = state.value("current").toMap();
    if (track.isEmpty()) return QJsonValue::Null;
    const bool buffering = state.value("buffering").toBool();
    const bool playing = state.value("playing").toBool() && !buffering;
    auto artist = TrackMetadata::displayArtist(track);
    if (artist.isEmpty()) artist = "Неизвестный исполнитель";
    auto title = track.value("title").toString().trimmed();
    if (title.isEmpty()) title = "Без названия";
    auto duration = state.value("duration").toLongLong();
    if (duration <= 0) duration = qMax(qint64(0), track.value("duration").toLongLong());
    auto position = qMax(qint64(0), state.value("position").toLongLong());
    if (duration > 0) position = qMin(position, duration);
    QString prefix;
    if (!playing) {
        const auto time = playbackTime(position) + (duration > 0 ? " / " + playbackTime(duration) : QString());
        prefix = (buffering ? "Загрузка · " : "Пауза · ") + time + " · ";
    }
    const QString branding = " · Sange";
    const auto subtitle = prefix + clipped(artist, 128 - (prefix + branding).toUtf8().size()) + branding;
    QJsonObject result{{"name", "Sange"}, {"type", 2}, {"details", clipped(title)},
        {"state", clipped(subtitle)}, {"instance", false}};
    if (playing) {
        const qint64 start = nowSeconds - position / 1000;
        QJsonObject timestamps{{"start", start}};
        // Listening activities use both endpoints for the native track progress UI.
        // Round up the duration so sub-second tracks still have a nonempty range.
        if (duration > 0) timestamps.insert("end", start + duration / 1000 + (duration % 1000 != 0));
        result.insert("timestamps", timestamps);
    }
    const auto link = track.value("permalink_url").toString();
    const bool hasTrackLink = publicUrl(link) && QUrl(link).host() == "soundcloud.com";
    if (hasTrackLink) result.insert("details_url", link);
    QJsonObject assets{{"small_image", projectIcon}, {"small_text", "Sange · SoundCloud player · GitHub"},
        {"small_url", projectUrl}};
    QString cover = track.value("artwork_url").toString();
    if (!publicUrl(cover)) cover = track.value("user").toMap().value("avatar_url").toString();
    if (publicUrl(cover)) {
        cover.replace("-large.", "-t500x500.");
        assets.insert("large_image", cover);
        auto caption = title + " — " + artist;
        if (duration > 0) caption += " · " + playbackTime(duration);
        assets.insert("large_text", clipped(caption));
        if (hasTrackLink) assets.insert("large_url", link);
    } else {
        assets.insert("large_image", projectIcon);
        assets.insert("large_text", "Sange · SoundCloud player");
        assets.insert("large_url", projectUrl);
    }
    result.insert("assets", assets);
    QJsonArray buttons;
    if (hasTrackLink) buttons.append(QJsonObject{{"label", "Слушать в SoundCloud"}, {"url", link}});
    buttons.append(QJsonObject{{"label", "Скачать Sange · GitHub"}, {"url", "https://github.com/sykia/sange/releases/latest"}});
    result.insert("buttons", buttons);
    return result;
}

void DiscordPresence::updatePlayer(const QVariantMap &state)
{
    m_player = state;
    if (m_ready && !m_publish.isActive()) m_publish.start();
}

void DiscordPresence::publish()
{
    if (!m_ready) return;
    auto next = activity(m_player, QDateTime::currentSecsSinceEpoch());
    const auto track = m_player.value("current").toMap();
    const auto trackKey = track.value("urn", track.value("id", track.value("permalink_url",
        track.value("title").toString() + " / " + TrackMetadata::displayArtist(track)))).toString();
    if (trackKey == m_lastTrackKey && next.isObject() && m_lastActivity.isObject()) {
        auto object = next.toObject();
        const auto previous = m_lastActivity.toObject().value("timestamps").toObject();
        const auto timestamps = object.value("timestamps").toObject();
        // Avoid updates caused only by clock rounding or normal position ticks.
        const bool sameDuration = previous.contains("end") == timestamps.contains("end")
            && (!previous.contains("end") || previous.value("end").toInteger() - previous.value("start").toInteger()
                == timestamps.value("end").toInteger() - timestamps.value("start").toInteger());
        if (!previous.isEmpty() && !timestamps.isEmpty() && sameDuration
            && qAbs(previous.value("start").toInteger() - timestamps.value("start").toInteger()) <= 2) {
            object.insert("timestamps", previous);
            next = object;
        }
    }
    if (m_sent && next == m_lastActivity) return;
    m_lastActivity = next;
    m_lastTrackKey = trackKey;
    m_sent = true;
    send(1, QJsonDocument(QJsonObject{{"cmd", "SET_ACTIVITY"},
        {"nonce", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"args", QJsonObject{{"pid", QCoreApplication::applicationPid()}, {"activity", next}}}}).toJson(QJsonDocument::Compact));
}
