#include "trackwaveform.h"
#include <QBuffer>
#include <QtEndian>
#include <QCache>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <QTimer>
#include <memory>

namespace { constexpr int MaxBytes = 1024 * 1024; QCache<QUrl, QImage> cache(8 * 1024); }
TrackWaveform::TrackWaveform(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &TrackWaveform::detailChanged, this, [this] { tint(); update(); });
    connect(this, &TrackWaveform::progressChanged, this, [this] { update(); });
    connect(this, &TrackWaveform::colorsChanged, this, [this] { tint(); update(); });
}
bool TrackWaveform::allowed(const QUrl &u) {
    const auto host = u.host().toLower();
    return u.isValid() && u.scheme() == "https" && (u.port() == -1 || u.port() == 443)
        && u.userInfo().isEmpty() && u.query().isEmpty() && u.fragment().isEmpty()
        && (host == "wave.sndcdn.com" || host == "w1.sndcdn.com" || host == "w2.sndcdn.com" || host == "w3.sndcdn.com" || host == "w4.sndcdn.com")
        && u.path().endsWith(".png", Qt::CaseInsensitive);
}
QImage TrackWaveform::decode(const QByteArray &bytes) {
    if (bytes.size() > MaxBytes || !bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))) return {};
    // Keep only image chunks: compressed text/ICC metadata must not allocate
    // memory outside the bounded pixel decode. Preserve original chunk CRCs.
    QByteArray pixels = bytes.left(8);
    qsizetype offset = 8; int chunks = 0; bool ended = false;
    while (offset + 12 <= bytes.size() && ++chunks <= 8192) {
        const auto length = qFromBigEndian<quint32>(bytes.constData() + offset);
        if (length > quint32(bytes.size() - offset - 12)) return {};
        const auto type = bytes.mid(offset + 4, 4);
        const bool imageChunk = type == "IHDR" || type == "PLTE" || type == "tRNS" || type == "IDAT" || type == "IEND";
        if (offset == 8 && (type != "IHDR" || length != 13)) return {};
        if (imageChunk) pixels.append(bytes.constData() + offset, length + 12);
        else if (!(type.at(0) & 0x20)) return {}; // Unknown critical format.
        offset += length + 12;
        if (type == "IEND") { ended = length == 0 && offset == bytes.size(); break; }
    }
    if (!ended) return {};
    QBuffer buffer; buffer.setData(pixels); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "png"); reader.setAutoTransform(false);
    const auto size = reader.size();
    if (size.width() < 2 || size.height() < 2 || size.width() > 8192 || size.height() > 1024 || qint64(size.width()) * size.height() > 2 * 1024 * 1024) return {};
    auto image = reader.read();
    if (image.isNull() || !image.hasAlphaChannel()) return {};
    bool transparent = false, content = false;
    for (int y = 0; y < image.height() && !(transparent && content); ++y)
        for (int x = 0; x < image.width() && !(transparent && content); ++x) {
            const auto alpha = image.pixelColor(x,y).alpha();
            transparent |= alpha == 0; content |= alpha > 0;
        }
    if (!transparent || !content) return {};
    return image.scaled(qMin(2048, size.width()), qMin(256, size.height()), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
void TrackWaveform::setSource(const QUrl &url) {
    if (m_source == url) return;
    ++m_generation;
    if (m_reply) { m_reply->abort(); m_reply = nullptr; }
    m_source = url; m_image = {}; tint(); emit sourceChanged(); emit readyChanged(); update();
    if (!allowed(url)) return;
    if (const auto hit = cache.object(url)) { m_image = *hit; tint(); emit readyChanged(); update(); return; }
    request(url, m_generation, 0);
}
void TrackWaveform::request(const QUrl &url, quint64 generation, int redirects) {
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    req.setTransferTimeout(8000);
    req.setRawHeader("Accept", "image/png");
    auto reply = m_transport->get(req); m_reply = reply;
    reply->setReadBufferSize(MaxBytes + 1);
    auto bytes = std::make_shared<QByteArray>();
    auto timer = new QTimer(reply); timer->setSingleShot(true); timer->start(10000);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::metaDataChanged, reply, [reply] {
        if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > MaxBytes) reply->abort();
    });
    connect(reply, &QIODevice::readyRead, reply, [reply, bytes] {
        bytes->append(reply->read(MaxBytes + 1 - bytes->size()));
        if (bytes->size() > MaxBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, generation, redirects, url] {
        reply->deleteLater();
        if (generation != m_generation) return;
        m_reply = nullptr;
        if (reply->error() != QNetworkReply::NoError || bytes->size() > MaxBytes) return;
        const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!redirect.isEmpty()) {
            const auto target = url.resolved(redirect);
            if (redirects < 3 && allowed(target)) request(target, generation, redirects + 1);
            return;
        }
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) return;
        bytes->append(reply->read(MaxBytes + 1 - bytes->size()));
        m_image = decode(*bytes);
        if (!m_image.isNull()) cache.insert(m_source, new QImage(m_image), int(m_image.sizeInBytes() / 1024) + 1);
        tint(); emit readyChanged(); update();
    });
}
void TrackWaveform::tint() {
    QImage mask;
    if (!m_image.isNull()) {
        // SoundCloud PNGs may encode the area outside the waveform.
        qint64 center = 0, edges = 0;
        for (int x = 0; x < m_image.width(); ++x) {
            center += m_image.pixelColor(x, m_image.height() / 2).alpha();
            edges += m_image.pixelColor(x, 0).alpha();
        }
        const bool inverted = edges > center;
        QVector<qreal> levels(m_image.width());
        for (int x = 0; x < m_image.width(); ++x) {
            qreal sum = 0;
            for (int y = 0; y < m_image.height(); ++y) {
                const qreal alpha = m_image.pixelColor(x, y).alphaF();
                sum += inverted ? 1 - alpha : alpha;
            }
            levels[x] = sum / m_image.height();
        }
        mask = QImage(m_image.size(), QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        const int radius = m_detailed ? 0 : qMax(2, m_image.width() / 32);
        QVector<qreal> smoothed(levels.size());
        qreal low = 1, high = 0;
        for (int x = 0; x < levels.size(); ++x) {
            qreal sum = 0, weight = 0;
            for (int d = -radius; d <= radius; ++d) {
                const qreal w = radius ? std::exp(-4.5 * d * d / (radius * radius)) : 1;
                sum += levels[qBound(0, x + d, int(levels.size()) - 1)] * w;
                weight += w;
            }
            smoothed[x] = sum / weight;
            low = qMin(low, smoothed[x]);
            high = qMax(high, smoothed[x]);
        }
        // A mastered track can stay near full scale for its entire duration.
        // Keep the resting shape compact and reveal its dynamics instead of a slab.
        const auto amplitude = [&](int x) {
            const qreal level = smoothed[x];
            const qreal normalized = high - low > 0.02
                ? qBound(0.0, (level - low) / (high - low), 1.0) : level;
            const qreal envelope = m_detailed ? level : 0.025 + 0.28 * std::pow(normalized, 1.6);
            return qMax(0.015, envelope) * (mask.height() - 2) / 2;
        };
        QPainterPath path;
        const qreal mid = mask.height() / 2.0;
        path.moveTo(0, mid);
        for (int x = 0; x < mask.width(); ++x) path.lineTo(x, mid - amplitude(x));
        for (int x = mask.width() - 1; x >= 0; --x) path.lineTo(x, mid + amplitude(x));
        path.closeSubpath();
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillPath(path, Qt::white);
    }
    auto colored = [&mask](QColor color) {
        if (mask.isNull()) return QImage();
        QImage image = mask.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter p(&image); p.setCompositionMode(QPainter::CompositionMode_SourceIn); p.fillRect(image.rect(), color); return image;
    };
    m_remaining = colored(m_ink); m_played = colored(m_accent);
}
void TrackWaveform::paint(QPainter *p) {
    if (!ready()) return;
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->drawImage(boundingRect(), m_remaining);
    p->setClipRect(QRectF(0, 0, width() * qBound(0.0, m_progress, 1.0), height()));
    p->drawImage(boundingRect(), m_played);
}

#include <QCoreApplication>
#include <qqml.h>
static void registerTrackWaveform() { qmlRegisterType<TrackWaveform>("SCrip.Native", 1, 0, "TrackWaveform"); }
Q_COREAPP_STARTUP_FUNCTION(registerTrackWaveform)
