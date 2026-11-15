#include "lyrics.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSettings>
#include <QCryptographicHash>
#include <QTimer>
#include <QTextDocumentFragment>
#include <algorithm>
#include <memory>

namespace {
QUrl endpoint(const QString &base, std::initializer_list<QPair<QString, QString>> params = {})
{
    QUrl url(base);
    QUrlQuery query;
    for (const auto &param : params) query.addQueryItem(param.first, param.second);
    url.setQuery(query);
    return url;
}
QString normalized(QString text)
{
    text = text.normalized(QString::NormalizationForm_KC).toCaseFolded();
    text.replace(QRegularExpression("[^\\p{L}\\p{N}]+"), "");
    return text;
}
QString cleanTitle(QString text)
{
    // Remove upload/production labels, retaining remix, live and speed variants.
    static const QRegularExpression labels(
        R"(\s*[\[(](?:(?:prod\.?|p\.|produced by)\s*[^\])]*|(?:official\s+)?(?:music\s+)?(?:video|audio|lyrics?|visuali[sz]er)|hq|hd|4k)[\])]\s*)",
        QRegularExpression::CaseInsensitiveOption);
    text.remove(labels);
    text.remove(QRegularExpression(R"(\.(?:mp3|wav|flac)$)", QRegularExpression::CaseInsensitiveOption));
    return text.simplified();
}
QString removeExcludedHtml(QString html)
{
    // Genius marks site controls and its contributor/translation header as excluded.
    static const QRegularExpression excluded(R"(<([a-z][a-z0-9]*)\b[^>]*data-exclude-from-selection\s*=\s*["']true["'][^>]*>)", QRegularExpression::CaseInsensitiveOption);
    while (true) {
        const auto root = excluded.match(html);
        if (!root.hasMatch()) break;
        if (root.captured().endsWith("/>")) { html.remove(root.capturedStart(), root.capturedLength()); continue; }
        const QRegularExpression tags("</?" + QRegularExpression::escape(root.captured(1)) + R"(\b[^>]*>)", QRegularExpression::CaseInsensitiveOption);
        auto nested = tags.globalMatch(html, root.capturedEnd());
        int depth = 1;
        qsizetype end = html.size();
        while (nested.hasNext()) {
            const auto tag = nested.next();
            if (tag.captured().startsWith("</")) --depth;
            else if (!tag.captured().endsWith("/>")) ++depth;
            if (depth == 0) { end = tag.capturedEnd(); break; }
        }
        html.remove(root.capturedStart(), end - root.capturedStart());
    }
    return html;
}
QString names(const QJsonArray &artists)
{
    QStringList result;
    for (const auto &artist : artists) result.append(artist.toObject()["name"].toString());
    return result.join(", ");
}
QVariantMap record(const QString &source, const QString &id, const QString &title,
                   const QString &artist, double duration, const QString &url)
{
    return {{"source", source}, {"id", id}, {"trackName", title}, {"artistName", artist},
            {"duration", duration}, {"sourceUrl", url}};
}
QJsonObject object(const QByteArray &bytes) { return QJsonDocument::fromJson(bytes).object(); }
bool hasLyrics(const QVariantMap &record)
{
    return !record.value("syncedLyrics").toString().trimmed().isEmpty()
        || !record.value("plainLyrics").toString().trimmed().isEmpty();
}
}

Lyrics::Lyrics(QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), m_network(network ? network : new QNetworkAccessManager(this)) {}

Lyrics::~Lyrics()
{
    ++m_generation;
    for (const auto &reply : m_replies) if (reply) {
        QObject::disconnect(reply, nullptr, this, nullptr);
        reply->abort(); reply->deleteLater();
    }
}

void Lyrics::cancel()
{
    ++m_generation;
    const auto replies = m_replies;
    m_replies.clear(); m_pending = 0;
    for (const auto &reply : replies) if (reply) reply->abort();
    m_state["busy"] = false;
    emit changed();
}
void Lyrics::lookup(const QString &title, const QString &artist, qint64 duration, const QString &trackKey)
{
    cancel(); m_state.clear(); m_seen.clear(); m_hadError = false;
    m_trackKey = trackKey.isEmpty() ? QString{} : QString::fromLatin1(QCryptographicHash::hash(trackKey.toUtf8(), QCryptographicHash::Sha256).toHex());
    m_title = title; m_artist = artist; m_duration = duration;
    m_queryTitle = cleanTitle(title); m_queryArtist = artist.trimmed();
    static const QRegularExpression separator(R"(\s+[-–—]\s+)");
    const auto split = separator.match(m_queryTitle);
    if (split.hasMatch()) {
        m_queryArtist = m_queryTitle.left(split.capturedStart()).trimmed();
        m_queryTitle = m_queryTitle.mid(split.capturedEnd()).trimmed();
    }
    if (!m_trackKey.isEmpty()) {
        const auto saved = QSettings().value("lyrics/" + m_trackKey).toString();
        if (!saved.isEmpty()) { setCustomText(saved); return; }
    }
    if (m_queryTitle.isEmpty()) { finish(); return; }
    if (m_queryArtist.isEmpty()) { searchProviders(m_queryTitle); return; }
    auto url = endpoint("https://lrclib.net/api/get", {{"track_name", m_queryTitle}, {"artist_name", m_queryArtist}});
    if (duration >= 1000 && duration <= 3600000) {
        QUrlQuery params(url); params.addQueryItem("duration", QString::number(duration / 1000.0, 'f', 2)); url.setQuery(params);
    }
    fetch(url, [this](const QByteArray &bytes) {
        const auto result = object(bytes).toVariantMap();
        if (hasLyrics(result) || result.value("instrumental").toBool()) apply(result);
        else searchProviders(m_queryTitle);
    });
}
void Lyrics::search(const QString &query)
{
    if (query.trimmed().isEmpty()) return;
    cancel(); m_state.clear(); m_seen.clear(); m_hadError = false;
    m_queryTitle = cleanTitle(query); m_queryArtist.clear(); m_duration = 0;
    searchProviders(query.trimmed());
}
void Lyrics::fetch(const QUrl &url, std::function<void(const QByteArray &)> consume)
{
    static const QSet<QString> hosts{"lrclib.net", "api.lyrics.ovh", "music.163.com", "c.y.qq.com", "genius.com", "lyrics.kugou.com"};
    if (url.scheme() != "https" || !hosts.contains(url.host()) || !url.userInfo().isEmpty() || (url.port() != -1 && url.port() != 443)) return;
    const auto generation = m_generation;
    ++m_pending; m_state["busy"] = true;
    m_state["message"] = "Ищем текст в музыкальных базах…"; emit changed();
    auto done = [this, generation, consume](const QByteArray &bytes) {
        if (generation != m_generation) return;
        consume(bytes);
        if (generation != m_generation) return;
        --m_pending;
        finish();
    };
    if (m_cache.contains(url.toString())) {
        const auto bytes = m_cache.value(url.toString());
        QTimer::singleShot(0, this, [done, bytes] { done(bytes); }); return;
    }
    if (QDateTime::currentDateTimeUtc() < m_retryAfter.value(url.host())) {
        m_hadError = true;
        QTimer::singleShot(0, this, [done] { done({}); }); return;
    }
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", ("SCrip/" + QCoreApplication::applicationVersion() + " (https://github.com/sykia/SCrip)").toUtf8());
    req.setRawHeader("Accept", url.host() == "genius.com" ? "application/json, text/html" : "application/json");
    if (url.host() == "c.y.qq.com") req.setRawHeader("Referer", "https://y.qq.com/");
    if (url.host() == "music.163.com") req.setRawHeader("Referer", "https://music.163.com/");
    req.setTransferTimeout(8000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    auto *reply = m_network->get(req); m_replies.append(reply);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes] {
        if (bytes->size() + reply->bytesAvailable() > 2 * 1024 * 1024) reply->abort();
        else bytes->append(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, done, url, bytes] {
        reply->deleteLater();
        if (generation != m_generation) return;
        m_replies.removeAll(reply);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429 || status == 503) {
            bool ok = false; int seconds = reply->rawHeader("Retry-After").toInt(&ok);
            if (!ok) seconds = int(QDateTime::currentDateTimeUtc().secsTo(QDateTime::fromString(QString::fromLatin1(reply->rawHeader("Retry-After")), Qt::RFC2822Date)));
            m_retryAfter[url.host()] = QDateTime::currentDateTimeUtc().addSecs(qBound(1, seconds > 0 ? seconds : 30, 86400) + QRandomGenerator::global()->bounded(4));
        }
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            if (status != 404) m_hadError = true;
            done({}); return;
        }
        bytes->append(reply->readAll());
        if (!url.path().endsWith("-lyrics") && QJsonDocument::fromJson(*bytes).isNull()) {
            m_hadError = true; done({}); return;
        }
        if (bytes->size() > 2 * 1024 * 1024) { m_hadError = true; done({}); return; }
        if (m_cache.size() >= 64) m_cache.clear();
        m_cache.insert(url.toString(), *bytes);
        done(*bytes);
    });
}
void Lyrics::finish()
{
    m_state["busy"] = m_pending > 0;
    if (m_pending > 0) { emit changed(); return; }
    m_state["sourceUnavailable"] = m_hadError;
    if (!m_state.value("lines").toList().isEmpty()) m_state["message"] = "";
    else if (!m_state.value("instrumental").toBool()) m_state["message"] = "Текст не найден";
    emit changed();
}
void Lyrics::searchProviders(const QString &query)
{
    fetch(endpoint("https://lrclib.net/api/search", {{"q", query}}), [this](const QByteArray &bytes) {
        const auto doc = QJsonDocument::fromJson(bytes);
        if (!bytes.isEmpty() && !doc.isArray()) m_hadError = true;
        QVariantList records;
        for (const auto &value : doc.array()) {
            auto item = value.toObject().toVariantMap();
            if (!hasLyrics(item)) continue;
            item["source"] = "LRCLIB";
            item["sourceUrl"] = QString("https://lrclib.net/api/get/%1").arg(item.value("id").toLongLong());
            records.append(item);
        }
        addResults(records);
    });
    fetch(endpoint("https://music.163.com/api/search/get", {{"s", query}, {"type", "1"}, {"limit", "8"}, {"offset", "0"}}), [this](const QByteArray &bytes) {
        QVariantList records;
        for (const auto &value : object(bytes)["result"].toObject()["songs"].toArray()) {
            const auto song = value.toObject(); const auto id = song["id"].toVariant().toString();
            records.append(record("NetEase", id, song["name"].toString(), names(song["artists"].toArray()), song["duration"].toDouble() / 1000, "https://music.163.com/#/song?id=" + id));
        }
        addResults(records);
    });
    fetch(endpoint("https://c.y.qq.com/soso/fcgi-bin/client_search_cp", {{"w", query}, {"format", "json"}, {"n", "8"}, {"p", "1"}}), [this](const QByteArray &bytes) {
        QVariantList records;
        for (const auto &value : object(bytes)["data"].toObject()["song"].toObject()["list"].toArray()) {
            const auto song = value.toObject(); const auto id = song["songmid"].toString();
            records.append(record("QQ Music", id, song["songname"].toString(), names(song["singer"].toArray()), song["interval"].toDouble(), "https://y.qq.com/n/ryqq/songDetail/" + id));
        }
        addResults(records);
    });
    fetch(endpoint("https://genius.com/api/search/multi", {{"q", query}}), [this](const QByteArray &bytes) {
        QVariantList records;
        for (const auto &section : object(bytes)["response"].toObject()["sections"].toArray()) {
            if (section.toObject()["type"].toString() != "song") continue;
            for (const auto &hit : section.toObject()["hits"].toArray()) {
                const auto song = hit.toObject()["result"].toObject();
                const QUrl url(song["url"].toString());
                if (url.scheme() != "https" || url.host() != "genius.com" || !url.path().endsWith("-lyrics")) continue;
                records.append(record("Genius", song["id"].toVariant().toString(), song["title"].toString(), song["primary_artist"].toObject()["name"].toString(), 0, url.toString()));
            }
        }
        addResults(records);
    });
    fetch(endpoint("https://lyrics.kugou.com/search", {{"keyword", query}, {"ver", "1"}, {"client", "pc"}, {"timelength", QString::number(m_duration)}}), [this](const QByteArray &bytes) {
        QVariantList records;
        for (const auto &value : object(bytes)["candidates"].toArray()) {
            const auto song = value.toObject();
            auto item = record("KuGou", song["id"].toString(), song["song"].toString(), song["singer"].toString(), song["duration"].toDouble() / 1000, "https://www.kugou.com/");
            item["accessKey"] = song["accesskey"].toString();
            if (!item.value("accessKey").toString().isEmpty()) records.append(item);
        }
        addResults(records);
    });
    const auto encoded = QString::fromLatin1(QUrl::toPercentEncoding(query));
    fetch(QUrl("https://api.lyrics.ovh/suggest/" + encoded), [this](const QByteArray &bytes) {
        QVariantList records;
        for (const auto &value : object(bytes)["data"].toArray()) {
            const auto song = value.toObject();
            auto item = record("Lyrics.ovh", song["id"].toVariant().toString(), song["title"].toString(), song["artist"].toObject()["name"].toString(), song["duration"].toDouble(), "https://lyrics.ovh/");
            records.append(item);
        }
        addResults(records);
    });
}
void Lyrics::addResults(const QVariantList &records)
{
    // Search APIs also return unrelated recommendations. Never display or load those.
    QVariantList matches;
    for (const auto &value : records) {
        const auto item = value.toMap();
        if (normalized(cleanTitle(item.value("trackName").toString())) != normalized(m_queryTitle)) continue;
        if (item.value("id").toString().isEmpty()) continue;
        matches.append(item);
    }
    auto score = [this, &matches](const QVariantMap &item) {
        const auto artist = normalized(item.value("artistName").toString());
        const auto expectedArtist = normalized(m_queryArtist);
        const bool artistMatches = !expectedArtist.isEmpty() && artist == expectedArtist;
        const double duration = item.value("duration").toDouble();
        const bool durationMatches = duration > 0 && m_duration > 0 && qAbs(duration - m_duration / 1000.0) <= 3;
        // A same-title live/remix upload with a different length must not get studio timing.
        if (duration > 0 && m_duration > 0 && qAbs(duration - m_duration / 1000.0) > 10) return 0;
        return (artistMatches ? 4 : 0) + (durationMatches ? 2 : 0)
            + (expectedArtist.isEmpty() && matches.size() == 1 ? 1 : 0);
    };
    std::stable_sort(matches.begin(), matches.end(), [&score](const QVariant &a, const QVariant &b) { return score(a.toMap()) > score(b.toMap()); });
    QVariantList attempts;
    for (const auto &value : matches) {
        const auto item = value.toMap();
        if (score(item) == 0 || m_state.value("synced").toBool()) continue;
        const QString key = item.value("source").toString() + ":" + item.value("id").toString();
        if (m_seen.contains(key)) continue;
        m_seen.insert(key);
        attempts.append(item);
        if (attempts.size() >= 2) break;
    }
    if (!attempts.isEmpty()) {
        const auto first = attempts.takeFirst().toMap();
        loadRecord(first, attempts);
    }
}
void Lyrics::loadRecord(QVariantMap item, QVariantList remaining)
{
    std::function<void(const QString &, bool)> consume = [this, item, remaining](const QString &text, bool timed) mutable {
        if (!text.trimmed().isEmpty()) item[timed && !parseLrc(text).isEmpty() ? "syncedLyrics" : "plainLyrics"] = text;
        if (!hasLyrics(item)) {
            if (!remaining.isEmpty() && !m_state.value("synced").toBool()) {
                const auto next = remaining.takeFirst().toMap();
                loadRecord(next, remaining);
            }
            return;
        }
        if (!m_state.value("lines").toList().isEmpty()
            && (parseLrc(item.value("syncedLyrics").toString()).isEmpty() || m_state.value("synced").toBool())) return;
        apply(item);
    };
    if (hasLyrics(item)) { consume({}, false); return; }
    const auto source = item.value("source").toString();
    const auto id = item.value("id").toString();
    if (source == "NetEase") {
        fetch(endpoint("https://music.163.com/api/song/lyric", {{"id", id}, {"lv", "-1"}, {"kv", "-1"}, {"tv", "-1"}}), [consume](const QByteArray &bytes) mutable {
            const auto data = object(bytes);
            if (data["uncollected"].toBool() || data["nolyric"].toBool()) { consume({}, true); return; }
            const auto text = data["lrc"].toObject()["lyric"].toString();
            if (text.contains(QString::fromUtf8("纯音乐")) || text.contains(QString::fromUtf8("暂无歌词"))) { consume({}, true); return; }
            consume(text, true);
        });
    } else if (source == "QQ Music") {
        fetch(endpoint("https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg", {{"songmid", id}, {"format", "json"}, {"nobase64", "1"}, {"g_tk", "5381"}}), [consume](const QByteArray &bytes) mutable {
            const auto data = object(bytes);
            if (data["code"].toInt(-1) != 0) { consume({}, true); return; }
            auto html = data["lyric"].toString();
            html.replace(QRegularExpression(R"((?:&#(?:0*10|x0*a);|\r?\n))", QRegularExpression::CaseInsensitiveOption), "<br>");
            const auto text = QTextDocumentFragment::fromHtml(html).toPlainText();
            consume(text, true);
        });
    } else if (source == "Lyrics.ovh") {
        const auto artist = QString::fromLatin1(QUrl::toPercentEncoding(item.value("artistName").toString()));
        const auto title = QString::fromLatin1(QUrl::toPercentEncoding(item.value("trackName").toString()));
        fetch(QUrl("https://api.lyrics.ovh/v1/" + artist + "/" + title), [consume](const QByteArray &bytes) mutable { consume(object(bytes)["lyrics"].toString(), false); });
    } else if (source == "KuGou") {
        fetch(endpoint("https://lyrics.kugou.com/download", {{"id", id}, {"accesskey", item.value("accessKey").toString()}, {"ver", "1"}, {"client", "pc"}, {"fmt", "lrc"}, {"charset", "utf8"}}), [consume](const QByteArray &bytes) {
            const auto data = QByteArray::fromBase64Encoding(object(bytes)["content"].toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            consume(data ? QString::fromUtf8(data.decoded) : QString{}, true);
        });
    } else if (source == "Genius") {
        fetch(QUrl(item.value("sourceUrl").toString()), [consume](const QByteArray &bytes) mutable {
            const auto html = QString::fromUtf8(bytes);
            static const QRegularExpression containers(R"(<div\b[^>]*data-lyrics-container\s*=\s*["']true["'][^>]*>)", QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression divs(R"(</?div\b[^>]*>)", QRegularExpression::CaseInsensitiveOption);
            auto matches = containers.globalMatch(html); QStringList sections;
            while (matches.hasNext()) {
                const auto container = matches.next();
                auto tags = divs.globalMatch(html, container.capturedEnd());
                int depth = 1, end = -1;
                while (tags.hasNext()) {
                    const auto tag = tags.next();
                    depth += tag.captured().startsWith("</") ? -1 : 1;
                    if (depth == 0) { end = tag.capturedStart(); break; }
                }
                if (end < 0) continue;
                auto fragment = removeExcludedHtml(html.mid(container.capturedEnd(), end - container.capturedEnd()));
                fragment.remove(QRegularExpression(R"(<(?:script|style)\b[^>]*>.*?</(?:script|style)>)", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
                sections.append(QTextDocumentFragment::fromHtml(fragment).toPlainText().trimmed());
            }
            auto text = sections.join("\n\n");
            text.remove(QRegularExpression(R"(^\s*\[(?:Текст песни|Song lyrics|Lyrics for)\b[^\]\n]*\]\s*$)", QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption));
            consume(text, false);
        });
    }
}
QVariantList Lyrics::parseLrc(const QString &text)
{
    static const QRegularExpression stamp(R"(\[(\d{1,3}):([0-5]\d)(?:[\.:](\d{1,3}))?\])");
    static const QRegularExpression offsetPattern(R"(\[offset:([+-]?\d+)\])", QRegularExpression::CaseInsensitiveOption);
    const qint64 offset = offsetPattern.match(text).captured(1).toLongLong();
    QVariantList lines;
    for (const auto &raw : text.split('\n')) {
        auto matches = stamp.globalMatch(raw);
        QList<qint64> times; int end = 0;
        while (matches.hasNext()) {
            const auto match = matches.next();
            const auto fraction = match.captured(3);
            const qint64 ms = fraction.isEmpty() ? 0 : fraction.leftJustified(3, '0').toLongLong();
            times.append(qMax(qint64(0), match.captured(1).toLongLong() * 60000 + match.captured(2).toLongLong() * 1000 + ms + offset));
            end = match.capturedEnd();
        }
        for (auto time : times) lines.append(QVariantMap{{"time", time}, {"text", raw.mid(end).trimmed()}});
    }
    std::stable_sort(lines.begin(), lines.end(), [](const QVariant &a, const QVariant &b) { return a.toMap()["time"].toLongLong() < b.toMap()["time"].toLongLong(); });
    return lines;
}
void Lyrics::apply(const QVariantMap &record)
{
    m_state["rawText"] = record.value("syncedLyrics").toString().isEmpty() ? record.value("plainLyrics") : record.value("syncedLyrics");
    auto lines = parseLrc(record.value("syncedLyrics").toString());
    m_state["synced"] = !lines.isEmpty();
    if (lines.isEmpty()) for (const auto &line : record.value("plainLyrics").toString().split('\n'))
        if (!line.trimmed().isEmpty()) lines.append(QVariantMap{{"time", -1}, {"text", line.trimmed()}});
    m_state["lines"] = lines;
    m_state["instrumental"] = record.value("instrumental").toBool();
    m_state["title"] = record.value("trackName"); m_state["artist"] = record.value("artistName");
    m_state["custom"] = false;
    m_state["source"] = record.value("source", "LRCLIB");
    m_state["sourceUrl"] = record.value("sourceUrl", QString("https://lrclib.net/api/get/%1").arg(record.value("id").toLongLong()));
    m_state["message"] = record.value("instrumental").toBool() ? "Инструментальный трек — здесь музыка говорит сама." : lines.isEmpty() ? "У этой записи пока нет текста." : "";
}
bool Lyrics::setCustomText(const QString &text)
{
    if (text.trimmed().isEmpty() || text.toUtf8().size() > 512 * 1024) return false;
    cancel();
    const bool synced = !parseLrc(text).isEmpty();
    apply({{"trackName", m_title}, {"artistName", m_artist},
           {synced ? "syncedLyrics" : "plainLyrics", text}});
    m_state["custom"] = true;
    m_state["sourceUrl"] = QString{};
    if (!m_trackKey.isEmpty()) QSettings().setValue("lyrics/" + m_trackKey, text);
    emit changed();
    return true;
}
void Lyrics::removeCustomText()
{
    if (!m_trackKey.isEmpty()) QSettings().remove("lyrics/" + m_trackKey);
    // lookup hashes the original identity, so retain the existing storage key here.
    const auto key = m_trackKey;
    lookup(m_title, m_artist, m_duration);
    m_trackKey = key;
}
