#include "trackinfo.h"
#include "trackwaveform.h"
#include <QtTest>
#include <QBuffer>
#include <QTimer>
#include <QPainter>
#include <cstring>

struct Fixture { QByteArray bytes; int status = 200; QUrl redirect; int delay = 0; qint64 length = -1; };
class FixtureReply : public QNetworkReply {
public:
    FixtureReply(QNetworkRequest req, Fixture f, QObject *parent) : QNetworkReply(parent), fixture(f) {
        setRequest(req); setUrl(req.url()); open(ReadOnly);
        QTimer::singleShot(f.delay, this, [this] {
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, fixture.status);
            if (!fixture.redirect.isEmpty()) setAttribute(QNetworkRequest::RedirectionTargetAttribute, fixture.redirect);
            setHeader(QNetworkRequest::ContentLengthHeader, fixture.length < 0 ? fixture.bytes.size() : fixture.length);
            emit metaDataChanged();
            emit readyRead(); setFinished(true); emit finished();
        });
    }
    void abort() override { setError(OperationCanceledError, "Cancelled"); setFinished(true); emit finished(); }
    qint64 bytesAvailable() const override { return fixture.bytes.size() - offset + QNetworkReply::bytesAvailable(); }
    qint64 readData(char *out, qint64 max) override {
        const auto n = qMin(max, fixture.bytes.size() - offset);
        if (n <= 0) return -1;
        memcpy(out, fixture.bytes.constData() + offset, n); offset += n; return n;
    }
    Fixture fixture; qint64 offset = 0;
};
class FixtureNetwork : public QNetworkAccessManager {
public:
    QList<Fixture> fixtures;
    QList<QNetworkRequest> requests;
    QNetworkReply *createRequest(Operation, const QNetworkRequest &req, QIODevice *) override {
        requests << req;
        return new FixtureReply(req, fixtures.isEmpty() ? Fixture{} : fixtures.takeFirst(), this);
    }
};
class TrackDetailsTests : public QObject {
    Q_OBJECT
    static QByteArray png(int w = 160, int h = 40) {
        QImage image(w, h, QImage::Format_ARGB32); image.fill(Qt::transparent);
        for (int x = 0; x < w; ++x) for (int y = h/3; y < 2*h/3; ++y) image.setPixelColor(x,y,Qt::black);
        QByteArray data; QBuffer b(&data); b.open(QIODevice::WriteOnly); image.save(&b,"PNG"); return data;
    }
private slots:
    void metadata() {
        QCOMPARE(TrackInfo::text(42), QString()); QCOMPARE(TrackInfo::text(" null "), QString());
        QVERIFY(TrackInfo::rows({{"bpm", true}, {"genre", 7}, {"key_signature", "null"}, {"license", QVariant()}}).isEmpty());
        const auto rows = TrackInfo::rows({{"bpm", 123.5}, {"metadata_artist", "Artist"}, {"user", QVariantMap{{"username", "Uploader"}}}, {"label_name", "Label"}, {"key_signature", "F#m"}, {"license", "cc-by"}});
        QCOMPARE(rows.size(), 6);
        QCOMPARE(rows.first().toMap().value("value").toString(), "Artist");
        QCOMPARE(rows.at(1).toMap().value("value").toString(), "Uploader");
        QVERIFY(TrackInfo::license("something-new").startsWith("Неизвестная лицензия:"));
        QVERIFY(TrackInfo::rows({{"bpm", -1}, {"duration", INFINITY}}).isEmpty());
    }
    void availability() {
        QCOMPARE(TrackInfo::availability({{"access", "playable"}}), "Полная версия");
        QCOMPARE(TrackInfo::availability({{"access", "preview"}}), "Только фрагмент");
        QCOMPARE(TrackInfo::availability({{"access", "blocked"}}), "Недоступен через API");
        QCOMPARE(TrackInfo::availability({{"access", "playable"}, {"streamable", false}}), "Недоступен через API");
        QVERIFY(TrackInfo::availability({{"streamable", "false"}}).isEmpty());
        QVERIFY(TrackInfo::availability({{"sharing", "private"}}).isEmpty());
        QVERIFY(TrackInfo::availability({}).isEmpty());
        QVERIFY(TrackInfo::availabilityNote({{"access", "blocked"}}).contains("Точная причина"));
        const auto rows = TrackInfo::rows({{"sharing", "private"}, {"access", "playable"}});
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.last().toMap().value("value").toString(), "Приватный трек");
    }
    void dates() {
        QCOMPARE(TrackInfo::releaseDate({{"created_at", "2024-05-03"}}), QString());
        QCOMPARE(TrackInfo::releaseDate({{"release_year", 2024}}), "2024");
        QCOMPARE(TrackInfo::releaseDate({{"release_year", 2024}, {"release_day", 3}}), "2024");
        QCOMPARE(TrackInfo::releaseDate({{"release_year", 2024}, {"release_month", 2}}), "2024-02");
        QCOMPARE(TrackInfo::releaseDate({{"release_year", 2024}, {"release_month", 2}, {"release_day", 29}}), "2024-02-29");
        QCOMPARE(TrackInfo::releaseDate({{"release_year", 2023}, {"release_month", 2}, {"release_day", 29}}), "2023-02");
        QCOMPARE(TrackInfo::releaseDate({{"release_year", "2024"}}), QString());
    }
    void tags() {
        QCOMPARE(TrackInfo::tags("rock \"hip hop\" geo:lat=43.555 \"machine:tag=with space\" rock"), QStringList({"rock", "hip hop", "geo:lat=43.555", "machine:tag=with space"}));
        QCOMPARE(TrackInfo::tags("rock \"unfinished"), QStringList{"rock"});
        QVERIFY(TrackInfo::tags(QVariantList{"tag"}).isEmpty());
        QCOMPARE(TrackInfo::tags("\"with, comma\""), QStringList{"with, comma"});
    }
    void pngFormat() {
        QVERIFY(!TrackWaveform::decode(png()).isNull());
        auto metadata = png();
        // Deliberately invalid compressed text is removed before libpng sees it.
        const QByteArray ancillary = QByteArray::fromHex("000000047a545874") + QByteArray("bomb") + QByteArray(4, '\0');
        metadata.insert(33, ancillary);
        QVERIFY(!TrackWaveform::decode(metadata).isNull());
        auto invalidChunk = png(); invalidChunk[8] = char(0x7f);
        QVERIFY(TrackWaveform::decode(invalidChunk).isNull());
        QImage opaque(10,10,QImage::Format_RGB32); opaque.fill(Qt::white);
        QByteArray opaqueBytes; QBuffer opaqueBuffer(&opaqueBytes); opaqueBuffer.open(QIODevice::WriteOnly); opaque.save(&opaqueBuffer, "PNG");
        QVERIFY(TrackWaveform::decode(opaqueBytes).isNull());
        QVERIFY(TrackWaveform::decode("{\"samples\":[1,2,3]}").isNull());
        QVERIFY(TrackWaveform::decode(png().left(35)).isNull());
        QVERIFY(TrackWaveform::decode(QByteArray(1024*1024+1, 'x')).isNull());
        QVERIFY(TrackWaveform::decode(png(8193,2)).isNull());
        QVERIFY(TrackWaveform::decode(png(2,1025)).isNull());
        QCOMPARE(TrackWaveform::decode(png(4096,128)).width(), 2048);
    }
    void addresses() {
        QVERIFY(TrackWaveform::allowed(QUrl("https://wave.sndcdn.com/a.png")));
        for (auto url : {"http://wave.sndcdn.com/a.png", "https://wave.sndcdn.com.evil.test/a.png", "https://127.0.0.1/a.png", "https://wave.sndcdn.com:444/a.png", "https://user:password@wave.sndcdn.com/a.png", "https://wave.sndcdn.com/a.png?oauth_token=secret", "https://wave.sndcdn.com/a.json", "file:///tmp/wave.png"}) QVERIFY(!TrackWaveform::allowed(QUrl(url)));
    }
    void transport() {
        FixtureNetwork net; TrackWaveform wave; wave.m_transport = &net;
        net.fixtures = {{png()}};
        wave.setSource(QUrl("https://wave.sndcdn.com/test-transport.png"));
        QTRY_VERIFY(wave.ready()); QCOMPARE(net.requests.size(), 1);
        wave.setSize(QSizeF(160,40)); wave.m_ink = Qt::gray; wave.m_accent = Qt::red; wave.tint();
        const auto geometryKey = wave.m_played.cacheKey();
        wave.m_progress = 0.5;
        QImage rendered(160,40,QImage::Format_ARGB32_Premultiplied); rendered.fill(Qt::transparent);
        { QPainter painter(&rendered); wave.paint(&painter); }
        QCOMPARE(rendered.pixelColor(20,20), QColor(Qt::red));
        QCOMPARE(rendered.pixelColor(130,20), QColor(Qt::gray));
        QCOMPARE(wave.m_played.cacheKey(), geometryKey);
        const auto req = net.requests.first();
        QVERIFY(!req.hasRawHeader("Authorization")); QVERIFY(!req.hasRawHeader("Cookie"));
        QCOMPARE(req.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(), int(QNetworkRequest::ManualRedirectPolicy));
        QCOMPARE(req.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(), int(QNetworkRequest::Manual));
        QVERIFY(req.transferTimeout() > 0);
        wave.setSource({}); QVERIFY(!wave.ready());
        wave.setSource(QUrl("https://wave.sndcdn.com/test-transport.png")); QVERIFY(wave.ready()); QCOMPARE(net.requests.size(), 1);
    }
    void redirectsAndFallback() {
        FixtureNetwork net; TrackWaveform wave; wave.m_transport = &net;
        net.fixtures = {{ {},302,QUrl("https://localhost/a.png") }};
        wave.setSource(QUrl("https://wave.sndcdn.com/test-redirect.png"));
        QTRY_VERIFY(!wave.m_reply); QCOMPARE(net.requests.size(),1); QVERIFY(!wave.ready());
        net.fixtures = {{ {},302,QUrl("https://w1.sndcdn.com/test-redirect-target.png") }, {png()}};
        wave.setSource(QUrl("https://wave.sndcdn.com/test-redirect-safe.png")); QTRY_VERIFY(wave.ready());
        net.fixtures = {{png(), 500}};
        wave.setSource(QUrl("https://wave.sndcdn.com/test-error.png")); QTRY_VERIFY(!wave.m_reply); QVERIFY(!wave.ready());
        net.fixtures = {{png(),200,{},0,1024*1024+1}};
        wave.setSource(QUrl("https://wave.sndcdn.com/test-large-header.png")); QTRY_VERIFY(!wave.m_reply); QVERIFY(!wave.ready());
        net.fixtures = {{QByteArray(1024*1024+1,'a'),200,{},0,0}};
        wave.setSource(QUrl("https://wave.sndcdn.com/test-large-body.png")); QTRY_VERIFY(!wave.m_reply); QVERIFY(!wave.ready());
        net.fixtures = {{"not PNG"}};
        wave.setSource(QUrl("https://wave.sndcdn.com/test-invalid.png")); QTRY_VERIFY(!wave.m_reply); QVERIFY(!wave.ready());
    }
    void stale() {
        FixtureNetwork net; TrackWaveform wave; wave.m_transport = &net;
        net.fixtures = {{png(),200,{},80}, {"bad",200,{},10}};
        wave.setSource(QUrl("https://wave.sndcdn.com/test-old.png"));
        const QPointer<QNetworkReply> old = wave.m_reply;
        wave.setSource(QUrl("https://wave.sndcdn.com/test-new.png"));
        QVERIFY(!old || old->error() == QNetworkReply::OperationCanceledError);
        QTest::qWait(120); QVERIFY(!wave.ready());
        QCOMPARE(wave.source(), QUrl("https://wave.sndcdn.com/test-new.png"));
    }
};
QTEST_MAIN(TrackDetailsTests)
#include "tst_trackdetails.moc"
