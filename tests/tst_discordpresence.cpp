#include "discordpresence.h"
#include <QtTest>
#include <QLocalServer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include <QtEndian>

class DiscordPresenceTest : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_settings;
    static QVariantMap player() {
        return {{"current", QVariantMap{{"title", "Test track"}, {"user", QVariantMap{{"username", "Artist"}}},
                    {"artwork_url", "https://i1.sndcdn.com/art-large.jpg"}, {"permalink_url", "https://soundcloud.com/artist/track"}}},
            {"position", 42000}, {"duration", 180000}, {"playing", true}};
    }
    static QByteArray frame(quint32 opcode, const QByteArray &payload) {
        QByteArray bytes(8, '\0');
        qToLittleEndian(opcode, bytes.data());
        qToLittleEndian(quint32(payload.size()), bytes.data() + 4);
        return bytes + payload;
    }
private slots:
    void initTestCase() {
        QVERIFY(m_settings.isValid());
        QCoreApplication::setOrganizationName("SCripTests");
        QCoreApplication::setApplicationName("DiscordPresence");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void defaultApplicationAndReset() {
        QSettings().clear();
        DiscordPresence presence;
        QVERIFY(!presence.enabled());
        QCOMPARE(presence.applicationId(), QString("1555593977367887893"));
        presence.setApplicationId("123456789012345678");
        QCOMPARE(presence.applicationId(), QString("123456789012345678"));
        presence.setApplicationId("   ");
        QCOMPARE(presence.applicationId(), QString("1555593977367887893"));
        DiscordPresence restored;
        QCOMPARE(restored.applicationId(), presence.applicationId());
        // Empty settings saved by builds predating the default ID also migrate.
        QSettings().setValue("discord/applicationId", "");
        DiscordPresence migrated;
        QCOMPARE(migrated.applicationId(), presence.applicationId());
    }
    void payload() {
        auto state = player();
        auto activity = DiscordPresence::activity(state, 1000).toObject();
        QCOMPARE(activity["type"].toInt(), 2);
        QCOMPARE(activity["details"].toString(), "Test track");
        QCOMPARE(activity["state"].toString(), "Artist · Сендж");
        QCOMPARE(activity["name"].toString(), "Сендж");
        QCOMPARE(activity["timestamps"].toObject()["start"].toInteger(), 958);
        QCOMPARE(activity["timestamps"].toObject()["end"].toInteger(), 1138);
        QCOMPARE(activity["details_url"].toString(), "https://soundcloud.com/artist/track");
        QCOMPARE(activity["assets"].toObject()["small_url"].toString(), "https://github.com/sykia/SCrip");
        QVERIFY(activity["assets"].toObject()["large_text"].toString().contains("3:00"));
        QCOMPARE(activity["buttons"].toArray().at(1).toObject()["url"].toString(), "https://github.com/sykia/SCrip/releases/latest");
        QCOMPARE(activity["assets"].toObject()["large_image"].toString(), "https://i1.sndcdn.com/art-t500x500.jpg");
        QCOMPARE(activity["buttons"].toArray().size(), 2);
        state["position"] = 90000;
        QCOMPARE(DiscordPresence::activity(state, 1000).toObject()["timestamps"].toObject()["start"].toInteger(), 910);
        state["playing"] = false;
        activity = DiscordPresence::activity(state, 1000).toObject();
        QVERIFY(!activity.contains("timestamps"));
        QVERIFY(activity["state"].toString().startsWith("Пауза · 1:30 / 3:00"));
        state["playing"] = true;
        state["buffering"] = true;
        QVERIFY(!DiscordPresence::activity(state, 1000).toObject().contains("timestamps"));
        QVERIFY(DiscordPresence::activity({}, 1000).isNull());
    }
    void metadataArtistKeepsUploaderLink() {
        auto state = player(); auto track = state["current"].toMap();
        track["metadata_artist"] = "  Credited artist  "; state["current"] = track;
        auto activity = DiscordPresence::activity(state, 1000).toObject();
        QCOMPARE(activity["state"].toString(), "Credited artist · Сендж");
        QCOMPARE(activity["details_url"].toString(), "https://soundcloud.com/artist/track");
        track["metadata_artist"] = " "; state["current"] = track;
        QCOMPARE(DiscordPresence::activity(state, 1000).toObject()["state"].toString(), "Artist · Сендж");
    }
    void durationAndPositionBounds() {
        auto state = player();
        state["position"] = -900;
        auto timestamps = DiscordPresence::activity(state, 1000).toObject()["timestamps"].toObject();
        QCOMPARE(timestamps["start"].toInteger(), 1000);
        QCOMPARE(timestamps["end"].toInteger(), 1180);
        state["position"] = 999000;
        timestamps = DiscordPresence::activity(state, 1000).toObject()["timestamps"].toObject();
        QCOMPARE(timestamps["end"].toInteger(), 1000);
        QCOMPARE(timestamps["end"].toInteger() - timestamps["start"].toInteger(), 180);
        state["duration"] = 0;
        timestamps = DiscordPresence::activity(state, 1000).toObject()["timestamps"].toObject();
        QVERIFY(!timestamps.contains("end"));
        auto track = state["current"].toMap();
        track["duration"] = 150500;
        state["current"] = track;
        state["position"] = 42000;
        timestamps = DiscordPresence::activity(state, 1000).toObject()["timestamps"].toObject();
        QCOMPARE(timestamps["start"].toInteger(), 958);
        QCOMPARE(timestamps["end"].toInteger(), 1109);
        state["buffering"] = true;
        auto activity = DiscordPresence::activity(state, 1000).toObject();
        QVERIFY(!activity.contains("timestamps"));
        QVERIFY(activity["state"].toString().startsWith("Загрузка · 0:42 / 2:30"));
    }
    void unsafeLinksAndUnicode() {
        auto state = player();
        auto track = state["current"].toMap();
        const auto title = QString::fromUtf8("🎵").repeated(100);
        track["title"] = title;
        track["user"] = QVariantMap{{"username", QString::fromUtf8("Исполнитель 🎵 ").repeated(50)}};
        track["artwork_url"] = "file:///home/private/cover.png";
        track["permalink_url"] = "https://example.com/private";
        state["current"] = track;
        const auto activity = DiscordPresence::activity(state, 1000).toObject();
        QVERIFY(!activity.contains("details_url"));
        const auto assets = activity["assets"].toObject();
        QCOMPARE(assets["large_url"].toString(), "https://github.com/sykia/SCrip");
        QVERIFY(assets["large_image"].toString().startsWith("https://"));
        QCOMPARE(activity["buttons"].toArray().size(), 1);
        QVERIFY(activity["details"].toString().toUtf8().size() <= 128);
        QVERIFY(title.startsWith(activity["details"].toString()));
        QVERIFY(activity["state"].toString().endsWith(" · Сендж"));
        QVERIFY(activity["state"].toString().toUtf8().size() <= 128);
    }
    void ipcLifecycle() {
#ifndef Q_OS_WIN
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
        QSettings().clear();
        QLocalServer server;
        QVERIFY(server.listen(runtime.path() + "/discord-ipc-0"));
        DiscordPresence presence;
        presence.setApplicationId("123456789012345678");
        presence.updatePlayer(player());
        presence.setEnabled(true);
        QTRY_VERIFY(server.hasPendingConnections());
        QScopedPointer<QLocalSocket> peer(server.nextPendingConnection());
        QByteArray input;
        auto read = [&] { input += peer->readAll(); return input.size() >= 8 && input.size() >= 8 + qFromLittleEndian<quint32>(input.constData() + 4); };
        QTRY_VERIFY(read());
        QCOMPARE(qFromLittleEndian<quint32>(input.constData()), quint32(0));
        QCOMPARE(QJsonDocument::fromJson(input.mid(8)).object()["client_id"].toString(), presence.applicationId());
        input.clear();
        const auto ready = frame(1, R"({"cmd":"DISPATCH","evt":"READY","data":{}})");
        peer->write(ready.left(5));
        peer->flush();
        QTest::qWait(20);
        QVERIFY(!presence.status().startsWith("Подключено"));
        peer->write(ready.mid(5));
        peer->flush();
        QTRY_VERIFY(read());
        auto command = QJsonDocument::fromJson(input.mid(8)).object();
        QCOMPARE(command["cmd"].toString(), "SET_ACTIVITY");
        QCOMPARE(command["args"].toObject()["activity"].toObject()["details"].toString(), "Test track");
        input.clear();
        peer->write(frame(3, "ping")); peer->flush();
        QTRY_VERIFY(read());
        QCOMPARE(qFromLittleEndian<quint32>(input.constData()), quint32(4));
        QCOMPARE(input.mid(8), QByteArray("ping"));
        input.clear();
        auto state = player();
        state["position"] = 90000;
        presence.updatePlayer(state);
        QTRY_VERIFY_WITH_TIMEOUT(read(), 2000);
        command = QJsonDocument::fromJson(input.mid(8)).object();
        const auto start = command["args"].toObject()["activity"].toObject()["timestamps"].toObject()["start"].toInteger();
        QVERIFY(qAbs(start - (QDateTime::currentSecsSinceEpoch() - 90)) <= 1);
        QCOMPARE(command["args"].toObject()["activity"].toObject()["timestamps"].toObject()["end"].toInteger() - start, 180);
        input.clear();
        // A late duration correction must not be lost to timestamp deduplication.
        state["duration"] = 240000;
        state["position"] = 91000;
        presence.updatePlayer(state);
        QTRY_VERIFY_WITH_TIMEOUT(read(), 2000);
        command = QJsonDocument::fromJson(input.mid(8)).object();
        const auto corrected = command["args"].toObject()["activity"].toObject()["timestamps"].toObject();
        QCOMPARE(corrected["end"].toInteger() - corrected["start"].toInteger(), 240);
        input.clear();
        state["playing"] = false;
        presence.updatePlayer(state);
        QTRY_VERIFY_WITH_TIMEOUT(read(), 2000);
        command = QJsonDocument::fromJson(input.mid(8)).object();
        QVERIFY(!command["args"].toObject()["activity"].toObject().contains("timestamps"));
        input.clear();
        presence.updatePlayer({});
        QTRY_VERIFY_WITH_TIMEOUT(read(), 2000);
        QVERIFY(QJsonDocument::fromJson(input.mid(8)).object()["args"].toObject()["activity"].isNull());
        input.clear();
        presence.setEnabled(false);
        QTRY_VERIFY(read());
        QVERIFY(QJsonDocument::fromJson(input.mid(8)).object()["args"].toObject()["activity"].isNull());
        QCOMPARE(presence.status(), "Выключено");
        DiscordPresence restored;
        QVERIFY(!restored.enabled());
        QCOMPARE(restored.applicationId(), QString("123456789012345678"));
#endif
    }
};
QTEST_GUILESS_MAIN(DiscordPresenceTest)
#include "tst_discordpresence.moc"
