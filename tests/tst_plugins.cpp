#include "pluginmanifest.h"
#include "pluginruntime.h"
#include "plugins.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <limits>
#include "trackmetadata.h"
#include "trackinfo.h"

// Модель без сети для проверки геометрии настоящего Android Main.qml.
class AndroidLayoutModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY signedInChanged)
    Q_PROPERTY(QVariantMap catalogState READ state NOTIFY catalogChanged)
    Q_PROPERTY(QVariantMap uiState READ state NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList homeShelves READ home NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList playlistRows READ playlistTracks NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList queueTracks READ queue NOTIFY catalogChanged)
    Q_PROPERTY(QVariantMap commentsState READ commentsState CONSTANT)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ position CONSTANT)
public:
    Q_INVOKABLE QVariantList trackInfo(const QVariantMap &track) const { return TrackInfo::rows(track); }
    Q_INVOKABLE QString trackDescription(const QVariantMap &track) const { return TrackInfo::text(track.value("description")); }
    Q_INVOKABLE QString displayArtist(const QVariantMap &track) const { return TrackMetadata::displayArtist(track); }
    Q_INVOKABLE bool hasSeparateArtist(const QVariantMap &track) const { return TrackMetadata::hasSeparateArtist(track); }
    QVariantList home() const { return state().value("home").toList(); }
    QVariantList playlistTracks() const { return state().value("playlistTracks").toList(); }
    QVariantList queue() const { return {}; }
    QVariantMap currentSnapshot{{"urn", "soundcloud:tracks:1"}, {"title", "Тест"}};
    int repeatMode = 0;
    Q_INVOKABLE void setRepeatMode(int mode) { repeatMode = mode; emit catalogChanged(); }
    QVariantMap commentsState() const { return {{"track", "soundcloud:tracks:1"}, {"items", QVariantList{QVariantMap{{"urn", "soundcloud:comments:1"}, {"body", "Этот момент хочется переслушивать"}, {"timestamp", 0}, {"user", QVariantMap{{"username", "anastasia"}}}}, QVariantMap{{"urn", "soundcloud:comments:2"}, {"body", "А здесь вступают ударные"}, {"timestamp", 10000}, {"user", QVariantMap{{"username", "slow motion"}}}}}}}; }
    Q_INVOKABLE void cancelCollectionSearch() {}
    Q_INVOKABLE void searchWholeCollection(bool = false) {}
    Q_INVOKABLE void setCommentTrack(const QVariantMap &) {}
    Q_INVOKABLE void loadComments(const QString &, bool = false, bool = false) {}
    bool signedIn() const { return true; }
    qint64 playbackPosition = 0;
    qint64 position() const { return playbackPosition; }
    QString publishedBody;
    qint64 publishedTimestamp = -2;
    Q_INVOKABLE void postComment(const QString &, const QString &body, qint64 timestamp) { publishedBody = body; publishedTimestamp = timestamp; emit commentPosted("soundcloud:tracks:1"); }
    Q_INVOKABLE void seek(qint64 timestamp) { playbackPosition = timestamp; emit positionChanged(); }
    Q_INVOKABLE void resumePlayback() {}
    Q_INVOKABLE void copyText(const QString &) {}
    QVariantMap profileSnapshot;
    QVariantMap state() const {
        return {{"preloadEnabled", true}, {"wifiPolicyAvailable", true}, {"repeatMode", repeatMode}, {"home", QVariantList{QVariantMap{{"title", "Микс любимых"}, {"subtitle", "Похоже на «Длинное название исходного трека» · SoundCloud"},
            {"tracks", QVariantList{QVariantMap{{"title", "Проверка отступов"}, {"urn", "soundcloud:tracks:seed"}, {"user", QVariantMap{{"urn", "soundcloud:users:artist"}, {"username", "Исполнитель"}}}}}}}}},
            {"playlistTracks", QVariantList{QVariantMap{{"urn", "soundcloud:tracks:1"}, {"title", "Первый трек"}, {"user", QVariantMap{{"username", "Исполнитель"}}}}, QVariantMap{{"urn", "soundcloud:tracks:2"}, {"title", "Второй трек"}}}},
            {"profile", profileSnapshot}, {"current", currentSnapshot}};
    }
    Q_INVOKABLE bool queueHasDuplicates() const { return false; }
    Q_INVOKABLE void setWaveVisualActive(bool) {}
    Q_INVOKABLE void volume(double) {}
    QVariantMap lastPreview;
    Q_INVOKABLE void loadPlaylist(const QVariantMap &item, bool = false) { lastPreview = item; }
    int homeRequests = 0;
    bool forcedHome = false;
    Q_INVOKABLE void loadHome(bool force = false) { ++homeRequests; forcedHome = force; }
    QString lastSection, lastUser;
    Q_INVOKABLE void browse(const QString &section, const QString &user = {}, bool = false) { lastSection = section; lastUser = user; }
signals:
    void recommendationExclusionAdded();
    void queuePlaylistSaved();
    void positionChanged();
    void commentPosted(const QString &track);
    void signedInChanged();
    void catalogChanged();
};

class PluginTests : public QObject {
    Q_OBJECT
    QTemporaryDir imports;
    int serial = 0;
    QJsonObject basic(QString id = "test") {
        return {{"api", 2},
                {"kind", "declarative"},
                {"id", id},
                {"name", "Тест"},
                {"version", "1.0.0"},
                {"requiresHost", QJsonObject{{"min", "1.4.1"}, {"maxExclusive", "2.0.0"}}},
                {"permissions", QJsonArray{}},
                {"settings", QJsonArray{}},
                {"contributions", QJsonArray{}},
                {"rules", QJsonArray{}}};
    }
    QVariantMap preview(Plugins &host, const QJsonObject &plugin) {
        const auto path = imports.filePath(QString::number(++serial) + ".json");
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {{"error", "write"}};
        f.write(QJsonDocument(plugin).toJson());
        f.close();
        QSignalSpy spy(&host, &Plugins::installPrepared);
        auto started = host.prepareInstall(QUrl::fromLocalFile(path));
        if (!started.value("error").toString().isEmpty())
            return started;
        if (!spy.wait(3000))
            return {{"error", "timeout"}};
        return spy.first()[0].toMap();
    }
    QString add(Plugins &host, const QJsonObject &p) {
        const auto v = preview(host, p);
        if (!v.value("error").toString().isEmpty())
            return v["error"].toString();
        QStringList grants;
        for (auto value : p["permissions"].toArray())
            grants.append(value.toString());
        return host.commitInstall(v["operationId"].toString(), grants);
    }
    QJsonObject panel(const QString &command, QJsonObject args = {}) {
        args["command"] = command;
        return {{"id", "panel"},
                {"type", "panel"},
                {"slot", "home.panels"},
                {"title", "Тестовая панель"},
                {"buttons", QJsonArray{QJsonObject{{"label", "Выполнить"}, {"action", args}}}}};
    }
  private slots:
    void init() {
        QCoreApplication::setApplicationVersion("1.4.1");
        QCoreApplication::setApplicationName("SCrip-v2-tests-" +
                                             QUuid::createUuid().toString(QUuid::WithoutBraces));
    }
    void trustedRuntimeLifecycle() {
        Plugins host;
        auto p = basic("code-test");
        p["kind"] = "qml";
        p["permissions"] = QJsonArray{"code.execute"};
        p["settings"] = QJsonArray{QJsonObject{{"id", "dim"}, {"type", "boolean"}, {"label", "Dim"}, {"default", true}}};
        const QString source = R"(import QtQuick
Item {
    required property var sdk
    objectName: "runtimeProbe"
    property string owner: sdk.id
    property bool dim: sdk.settings.dim
    Binding { target: sdk.find("testDock"); property: "opacity"; value: 0.3; when: sdk.active; restoreMode: Binding.RestoreBindingOrValue }
})";
        p["contributions"] = QJsonArray{QJsonObject{{"id", "main"}, {"type", "runtime"}, {"source", source}}};
        QVERIFY2(PluginManifest::validate(p).isEmpty(), qPrintable(PluginManifest::validate(p)));
        auto denied = p;
        denied["permissions"] = QJsonArray{};
        QVERIFY(!PluginManifest::validate(denied).isEmpty());
        denied = p;
        denied["kind"] = "declarative";
        QVERIFY(!PluginManifest::validate(denied).isEmpty());
        QCOMPARE(add(host, p), QString());
        QVERIFY(host.runtimes().isEmpty());
        QQuickWindow window;
        QQuickItem dock(window.contentItem());
        dock.setObjectName("testDock");
        dock.setOpacity(0.8);
        QQmlEngine engine;
        PluginRuntime runtime(&engine, &window, nullptr, &host);
        QCOMPARE(host.enable("code-test", true), QString());
        auto findProbe = [&]() -> QQuickItem * {
            for (auto *child : window.contentItem()->childItems())
                if (child->objectName() == "runtimeProbe") return child;
            return nullptr;
        };
        QTRY_VERIFY(findProbe());
        QPointer<QQuickItem> original = findProbe();
        QCOMPARE(original->property("owner").toString(), QString("code-test"));
        QCOMPARE(original->property("dim").toBool(), true);
        QCOMPARE(dock.opacity(), 0.3);
        QCOMPARE(host.setSetting("code-test", "dim", false), QString());
        QTRY_COMPARE(original->property("dim").toBool(), false);
        host.reloadRuntime("code-test");
        QTRY_VERIFY(original.isNull());
        QTRY_VERIFY(findProbe());
        QCOMPARE(host.setPermission("code-test", "code.execute", false), QString());
        QTRY_VERIFY(!findProbe());
        QCOMPARE(dock.opacity(), 0.8);
        QVERIFY(host.runtimes().isEmpty());
        QCOMPARE(host.setPermission("code-test", "code.execute", true), QString());
        QTRY_VERIFY(findProbe());
        QCOMPARE(host.remove("code-test"), QString());
        QTRY_VERIFY(!findProbe());
    }
    void trustedRuntimeRecoveryMode() {
        Plugins host;
        auto p = basic("safe-mode-test");
        p["kind"] = "qml";
        p["permissions"] = QJsonArray{"code.execute"};
        p["contributions"] = QJsonArray{QJsonObject{{"id", "main"}, {"type", "runtime"},
            {"source", "import QtQuick\nItem { required property var sdk; objectName: \"shouldNotLoad\" }"}}};
        QCOMPARE(add(host, p), QString());
        QCOMPARE(host.enable("safe-mode-test", true), QString());
        const bool wasSet = qEnvironmentVariableIsSet("SCRIP_NO_PLUGIN_CODE");
        const auto previous = qgetenv("SCRIP_NO_PLUGIN_CODE");
        qputenv("SCRIP_NO_PLUGIN_CODE", "1");
        QQuickWindow window;
        QQmlEngine engine;
        {
            PluginRuntime runtime(&engine, &window, nullptr, &host);
            QTest::qWait(30);
            QVERIFY(window.contentItem()->childItems().isEmpty());
        }
        if (wasSet) qputenv("SCRIP_NO_PLUGIN_CODE", previous);
        else qunsetenv("SCRIP_NO_PLUGIN_CODE");
        QCOMPARE(host.installed().first().toMap()["enabled"].toBool(), true);
    }
    void trustedRuntimeErrorAndExample() {
        Plugins host;
        QFile manifestFile(QString(SCRIP_SOURCE_DIR) + "/examples/plugins/glass-lab.manifest.json");
        QFile sourceFile(QString(SCRIP_SOURCE_DIR) + "/examples/plugins/glass-lab.qml");
        QVERIFY(manifestFile.open(QIODevice::ReadOnly));
        QVERIFY(sourceFile.open(QIODevice::ReadOnly));
        auto p = QJsonDocument::fromJson(manifestFile.readAll()).object();
        p["requiresHost"] = basic()["requiresHost"];
        auto c = p["contributions"].toArray()[0].toObject();
        c["source"] = QString::fromUtf8(sourceFile.readAll());
        p["contributions"] = QJsonArray{c};
        QCOMPARE(add(host, p), QString());
        QQuickWindow window;
        window.resize(412, 860);
        QQmlEngine engine;
        PluginRuntime runtime(&engine, &window, nullptr, &host);
        QCOMPARE(host.enable("glass-lab", true), QString());
        auto findMenu = [&]() -> QObject * {
            for (auto *child : window.contentItem()->childItems())
                if (child->objectName() == "glassLabExtension") return child->findChild<QObject *>("glassLabMenu");
            return nullptr;
        };
        QTRY_VERIFY2(findMenu(), qPrintable(host.installed().first().toMap()["lastError"].toString()));
        QCOMPARE(host.panels().size(), 0);
        QCOMPARE(host.enable("glass-lab", false), QString());
        QTRY_VERIFY(!findMenu());
        p["version"] = "1.0.1";
        c["source"] = "import QtQuick\nItem { required property var sdk; broken syntax }";
        p["contributions"] = QJsonArray{c};
        QCOMPARE(add(host, p), QString());
        QCOMPARE(host.enable("glass-lab", true), QString());
        QTRY_COMPARE(host.installed().first().toMap()["status"].toString(), QString("error"));
        QVERIFY(host.runtimes().isEmpty());
        p["version"] = "1.0.2";
        c["source"] = "import QtQuick\nItem { required property var sdk; objectName: \"fixedPlugin\" }";
        p["contributions"] = QJsonArray{c};
        QCOMPARE(add(host, p), QString());
        host.reloadRuntime("glass-lab");
        QTRY_COMPARE(host.installed().first().toMap()["status"].toString(), QString("active"));
    }
    void parserRejectsHostileInput() {
        auto p = basic();
        QVERIFY(PluginManifest::validate(p).isEmpty());
        for (const auto &bytes :
             QList<QByteArray>{R"({"api":2,"api":1})", R"({"api":1,"\u0061pi":2})", QByteArray(131073, ' '),
                               QByteArray("{\"x\":") + QByteArray(18, '[') + "0" + QByteArray(18, ']') + "}"})
            QVERIFY(!PluginManifest::parse(bytes).error.isEmpty());
        p["kind"] = "executable";
        QVERIFY(PluginManifest::validate(p).contains("unsupported-runtime"));
        p = basic();
        p["permissions"] = QJsonArray{"network"};
        QVERIFY(!PluginManifest::validate(p).isEmpty());
        p = basic();
        p["permissions"] = QJsonArray{"ui.panels"};
        p["contributions"] = QJsonArray{panel("player.pause")};
        QVERIFY(!PluginManifest::validate(p).isEmpty());
        p = basic();
        p["version"] = "1.2.65536";
        QVERIFY(!PluginManifest::validate(p).isEmpty());
        p = basic();
        p["extra"] = true;
        QVERIFY(!PluginManifest::validate(p).isEmpty());
        QVERIFY(PluginManifest::compareVersions("1.9.0", "1.10.0") < 0);
        QVERIFY(!PluginManifest::version("01.0.0"));
    }
    void apiTwoPermissionsCommandsAndSettings() {
        Plugins host;
        auto p = basic();
        p["permissions"] = QJsonArray{"ui.panels", "player.control"};
        p["settings"] = QJsonArray{
            QJsonObject{{"id", "switch"}, {"type", "boolean"}, {"label", "Показывать"}, {"default", true}}};
        auto c = panel("player.pause");
        c["enabledSetting"] = "switch";
        p["contributions"] = QJsonArray{c};
        QCOMPARE(add(host, p), QString());
        QVERIFY(host.panels().isEmpty());
        QCOMPARE(host.enable("test", true), QString());
        QCOMPARE(host.panels().size(), 1);
        int calls = 0;
        host.setPlayerCommand([&](const QString &command, const QJsonObject &) {
            ++calls;
            return command == "player.pause" ? QString() : QString("bad");
        });
        QCOMPARE(host.invokeButton("test", "panel", 0), QString());
        QCOMPARE(calls, 1);
        QCOMPARE(host.setPermission("test", "player.control", false), QString());
        QVERIFY(!host.invokeButton("test", "panel", 0).isEmpty());
        QCOMPARE(calls, 1);
        QCOMPARE(host.installed()[0].toMap()["status"].toString(), QString("awaiting-permissions"));
        QCOMPARE(host.setPermission("test", "player.control", true), QString());
        QVERIFY(!host.setSetting("test", "switch", 42).isEmpty());
        QCOMPARE(host.setSetting("test", "switch", false), QString());
        QVERIFY(host.panels().isEmpty());
        QVERIFY(!host.invokeButton("test", "panel", 0).isEmpty());
        Plugins restored;
        QCOMPARE(restored.installed()[0].toMap()["settings"].toMap()["switch"].toBool(), false);
        auto enumPlugin = basic("enum-example");
        enumPlugin["settings"] = QJsonArray{QJsonObject{{"id", "choice"},
                                                        {"type", "enum"},
                                                        {"label", "Вариант"},
                                                        {"options", QJsonArray{"a", "b"}},
                                                        {"default", "a"}}};
        QCOMPARE(add(host, enumPlugin), QString());
        QCOMPARE(host.setSetting("enum-example", "choice", "b"), QString());
        QVERIFY(!host.setSetting("enum-example", "choice", "c").isEmpty());
    }
    void updatesAreTransactionalAndPreserveData() {
        Plugins host;
        auto p = basic();
        p["settings"] = QJsonArray{QJsonObject{{"id", "minutes"},
                                               {"type", "integer"},
                                               {"label", "Минуты"},
                                               {"default", 30},
                                               {"min", 1},
                                               {"max", 180}}};
        QCOMPARE(add(host, p), QString());
        QCOMPARE(host.setSetting("test", "minutes", 45), QString());
        auto update = p;
        update["version"] = "1.1.0";
        QCOMPARE(add(host, update), QString());
        QCOMPARE(host.installed()[0].toMap()["settings"].toMap()["minutes"].toInt(), 45);
        QVERIFY(!add(host, p).isEmpty());
        auto withPermission = update;
        withPermission["version"] = "1.1.1";
        withPermission["permissions"] = QJsonArray{"storage"};
        auto permissionPreview = preview(host, withPermission);
        QVERIFY(permissionPreview["changes"].toStringList().join(' ').contains("Новое разрешение: storage"));
        QVERIFY(!host.commitInstall(permissionPreview["operationId"].toString(), {}).isEmpty());
        QCOMPARE(host.installed()[0].toMap()["version"].toString(), QString("1.1.0"));
        update["version"] = "1.2.0";
        update["settings"] = QJsonArray{};
        QCOMPARE(add(host, update), QString());
        update = p;
        update["version"] = "1.3.0";
        QCOMPARE(add(host, update), QString());
        QCOMPARE(host.installed()[0].toMap()["settings"].toMap()["minutes"].toInt(), 45);
        update["version"] = "1.4.0";
        auto view = preview(host, update);
        QVERIFY(view["error"].toString().isEmpty());
        QCOMPARE(host.enable("test", true), QString());
        QVERIFY(!host.commitInstall(view["operationId"].toString(), {}).isEmpty());
        QCOMPARE(host.remove("test", true), QString());
        QCOMPARE(host.installed()[0].toMap()["status"].toString(), QString("removed"));
        QVERIFY(!host.enable("test", true).isEmpty());
        QCOMPARE(add(host, update), QString());
        QCOMPARE(host.installed()[0].toMap()["settings"].toMap()["minutes"].toInt(), 45);
        QCOMPARE(host.remove("test"), QString());
        QVERIFY(host.installed().isEmpty());
    }
    void dependenciesAndCycles() {
        Plugins host;
        auto dep = basic("dependency");
        auto p = basic();
        p["dependencies"] =
            QJsonArray{QJsonObject{{"id", "dependency"}, {"min", "1.0.0"}, {"maxExclusive", "2.0.0"}}};
        QCOMPARE(add(host, p), QString());
        QCOMPARE(host.enable("test", true), QString());
        QCOMPARE(host.installed()[0].toMap()["status"].toString(), QString("blocked"));
        QCOMPARE(add(host, dep), QString());
        QCOMPARE(host.enable("dependency", true), QString());
        QCOMPARE(host.installed()[0].toMap()["status"].toString(), QString("active"));
        dep["version"] = "2.0.0";
        QVERIFY(!add(host, dep).isEmpty());
        dep["version"] = "1.1.0";
        dep["dependencies"] =
            QJsonArray{QJsonObject{{"id", "test"}, {"min", "1.0.0"}, {"maxExclusive", "2.0.0"}}};
        QVERIFY(!add(host, dep).isEmpty());
        QCOMPARE(host.enable("dependency", false), QString());
        QCOMPARE(host.installed()[0].toMap()["status"].toString(), QString("blocked"));
        QCOMPARE(host.remove("dependency"), QString());
        QVERIFY(!host.installed()[0].toMap()["enabled"].toBool());
    }
    void eventsStopOnDisableAndRateLimit() {
        Plugins host;
        auto p = basic();
        p["permissions"] = QJsonArray{"player.observe", "player.control"};
        p["rules"] = QJsonArray{
            QJsonObject{{"id", "pause"},
                        {"event", "player.playbackChanged"},
                        {"conditions", QJsonArray{QJsonObject{{"field", "playing"}, {"equals", true}}}},
                        {"actions", QJsonArray{QJsonObject{{"command", "player.pause"}}}}}};
        QCOMPARE(add(host, p), QString());
        QCOMPARE(host.enable("test", true), QString());
        QVariantMap state{{"playing", false}, {"signedIn", true}};
        host.updatePlayer(state);
        int calls = 0;
        host.setPlayerCommand([&](const QString &, const QJsonObject &) {
            ++calls;
            state["playing"] = false;
            host.updatePlayer(state);
            return QString();
        });
        state["playing"] = true;
        host.updatePlayer(state);
        QTRY_COMPARE(calls, 1);
        state["playing"] = true;
        host.updatePlayer(state);
        QCOMPARE(host.enable("test", false), QString());
        QTest::qWait(20);
        QCOMPARE(calls, 1);
        QCOMPARE(host.enable("test", true), QString());
        const int beforeBurst = calls;
        for (int i = 0; i < 10; ++i) {
            host.enqueueEvent("player.playbackChanged", {{"playing", true}, {"buffering", false}});
        }
        QTRY_COMPARE(host.installed()[0].toMap()["status"].toString(), QString("error"));
        QVERIFY(calls - beforeBurst <= 8);
    }
    void asynchronousEventCyclesAreBounded() {
        Plugins host;
        auto p = basic();
        p["permissions"] = QJsonArray{"player.observe", "player.control"};
        p["rules"] =
            QJsonArray{QJsonObject{{"id", "cycle"},
                                   {"event", "player.playbackChanged"},
                                   {"conditions", QJsonArray{}},
                                   {"actions", QJsonArray{QJsonObject{{"command", "player.pause"}}}}}};
        QCOMPARE(add(host, p), QString());
        QCOMPARE(host.enable("test", true), QString());
        QVariantMap state{{"playing", false}};
        host.updatePlayer(state);
        int calls = 0;
        host.setPlayerCommand([&](const QString &, const QJsonObject &) {
            ++calls;
            QTimer::singleShot(0, &host, [&] {
                state["playing"] = !state["playing"].toBool();
                host.updatePlayer(state);
            });
            return QString();
        });
        state["playing"] = true;
        host.updatePlayer(state);
        QTRY_COMPARE(host.installed()[0].toMap()["status"].toString(), QString("error"));
        QVERIFY(calls <= 5);
    }
    void timerAndLogout() {
        Plugins host;
        auto p = basic();
        p["permissions"] = QJsonArray{"ui.panels", "timers", "player.control"};
        p["settings"] = QJsonArray{QJsonObject{{"id", "minutes"},
                                               {"type", "integer"},
                                               {"label", "Минуты"},
                                               {"default", 30},
                                               {"min", 1},
                                               {"max", 180}}};
        p["contributions"] =
            QJsonArray{panel("timer.start", {{"timerId", "sleep"}, {"minutesSetting", "minutes"}})};
        p["rules"] = QJsonArray{
            QJsonObject{{"id", "pause"},
                        {"event", "timer.expired"},
                        {"conditions", QJsonArray{QJsonObject{{"field", "timerId"}, {"equals", "sleep"}}}},
                        {"actions", QJsonArray{QJsonObject{{"command", "player.pause"}}}}}};
        QCOMPARE(add(host, p), QString());
        QCOMPARE(host.enable("test", true), QString());
        int calls = 0;
        host.setPlayerCommand([&](const QString &, const QJsonObject &) {
            ++calls;
            return QString();
        });
        QCOMPARE(host.invokeButton("test", "panel", 0), QString());
        QCOMPARE(host.m_timers["test"].size(), 1);
        auto timer = host.m_timers["test"]["sleep"].object;
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        QTRY_COMPARE(calls, 1);
        QCOMPARE(host.invokeButton("test", "panel", 0), QString());
        host.updatePlayer({{"signedIn", true}});
        host.updatePlayer({{"signedIn", false}});
        QVERIFY(host.m_timers["test"].isEmpty());
    }
    void storageAndFailedSave() {
        Plugins host;
        auto p = basic();
        p["permissions"] = QJsonArray{"ui.panels", "storage"};
        p["contributions"] = QJsonArray{panel("storage.set", {{"key", "bookmark"}, {"value", "saved"}})};
        QCOMPARE(add(host, p), QString());
        QCOMPARE(host.enable("test", true), QString());
        QCOMPARE(host.invokeButton("test", "panel", 0), QString());
        Plugins restored;
        QCOMPARE(restored.installed()[0].toMap()["data"].toMap()["bookmark"].toString(), QString("saved"));
        const auto path =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins.json";
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkdir(path));
        QVERIFY(!host.enable("test", false).isEmpty());
        QVERIFY(host.installed()[0].toMap()["enabled"].toBool());
        QDir(path).removeRecursively();
    }
    void playerSeekRelativeAndVolumeValidation() {
        qint64 target = -1;
        QVERIFY(Plugins::relativeSeekTarget(1000, 10000, 2500, true, &target));
        QCOMPARE(target, qint64(3500));
        QVERIFY(Plugins::relativeSeekTarget(1000, 10000, -2500, true, &target));
        QCOMPARE(target, qint64(0));
        QVERIFY(Plugins::relativeSeekTarget(9000, 10000, 5000, true, &target));
        QCOMPARE(target, qint64(10000));
        QVERIFY(Plugins::relativeSeekTarget(1000, 10000, -5000, true, &target));
        QCOMPARE(target, qint64(0));
        QVERIFY(!Plugins::relativeSeekTarget(1000, 10000, 1, false, &target));
        QVERIFY(!Plugins::relativeSeekTarget(10001, 10000, 1, true, &target));
        QVERIFY(Plugins::relativeSeekTarget(1, std::numeric_limits<qint64>::max(), std::numeric_limits<qint64>::max(), true, &target));
        QCOMPARE(target, std::numeric_limits<qint64>::max());
        QVERIFY(Plugins::relativeSeekTarget(std::numeric_limits<qint64>::max() - 1,
                                            std::numeric_limits<qint64>::max(),
                                            std::numeric_limits<qint64>::min(), true, &target));
        QCOMPARE(target, qint64(0));
        auto p = basic();
        p["permissions"] = QJsonArray{"ui.panels", "player.control"};
        p["contributions"] = QJsonArray{panel("player.seekRelative", {{"offsetMs", 2500}}),
                                        panel("player.volume", {{"value", 0.5}})};
        auto commandPanels = p["contributions"].toArray();
        auto seekPanel = commandPanels[0].toObject(); seekPanel["id"] = "seek";
        auto volumePanel = commandPanels[1].toObject(); volumePanel["id"] = "volume";
        commandPanels[0] = seekPanel; commandPanels[1] = volumePanel; p["contributions"] = commandPanels;
        QVERIFY(PluginManifest::validate(p).isEmpty());
        auto bad = p;
        auto contributions = bad["contributions"].toArray();
        auto action = contributions[0].toObject()["buttons"].toArray()[0].toObject();
        auto args = action["action"].toObject(); args["offsetMs"] = 1.5;
        action["action"] = args;
        contributions[0] = action;
        bad["contributions"] = contributions;
        QVERIFY(!PluginManifest::validate(bad).isEmpty());
        bad = p; contributions = bad["contributions"].toArray();
        action = contributions[1].toObject()["buttons"].toArray()[0].toObject();
        args = action["action"].toObject(); args["value"] = 1.01;
        action["action"] = args; contributions[1] = action; bad["contributions"] = contributions;
        QVERIFY(!PluginManifest::validate(bad).isEmpty());
        bad = p; contributions = bad["contributions"].toArray();
        action = contributions[1].toObject()["buttons"].toArray()[0].toObject();
        args = action["action"].toObject(); args["value"] = QJsonValue(std::numeric_limits<double>::quiet_NaN());
        action["action"] = args; contributions[1] = action; bad["contributions"] = contributions;
        QVERIFY(!PluginManifest::validate(bad).isEmpty());
        auto missingGrant = p; missingGrant["permissions"] = QJsonArray{"ui.panels"};
        QVERIFY(!PluginManifest::validate(missingGrant).isEmpty());
        Plugins host;
        QCOMPARE(add(host, p), QString());
        QCOMPARE(host.enable("test", true), QString());
        QStringList calls;
        host.setPlayerCommand([&](const QString &command, const QJsonObject &arguments) {
            calls.append(command + QJsonDocument(arguments).toJson(QJsonDocument::Compact));
            return QString();
        });
        QCOMPARE(host.invokeButton("test", "seek", 0), QString());
        QCOMPARE(host.invokeButton("test", "volume", 0), QString());
        QCOMPARE(calls.size(), 2);
        QVERIFY(calls[0].startsWith("player.seekRelative"));
        QVERIFY(calls[1].startsWith("player.volume"));
        QCOMPARE(host.setPermission("test", "player.control", false), QString());
        QVERIFY(!host.invokeButton("test", "seek", 0).isEmpty());
        QCOMPARE(host.enable("test", false), QString());
        QVERIFY(!host.invokeButton("test", "seek", 0).isEmpty());
    }
    void declarativeOwnStateIsTypedAndIsolated() {
        Plugins host;
        auto p = basic("first");
        p["permissions"] = QJsonArray{"ui.panels", "storage"};
        p["settings"] = QJsonArray{QJsonObject{{"id", "enabled"}, {"type", "boolean"},
                                               {"label", "Включено"}, {"default", true}}};
        auto statePanel = QJsonObject{{"id", "state"}, {"type", "panel"}, {"slot", "home.panels"},
                                      {"title", "Состояние"},
                                      {"items", QJsonArray{QJsonObject{{"type", "toggle"}, {"source", "setting"},
                                          {"key", "enabled"}, {"valueType", "boolean"}, {"label", "Флаг"}},
                                          QJsonObject{{"type", "text"}, {"source", "storage"}, {"key", "mark"},
                                              {"valueType", "text"}, {"label", "Метка"}},
                                          QJsonObject{{"type", "text"}, {"source", "storage"}, {"key", "mark"},
                                              {"valueType", "integer"}, {"label", "Неверный тип"}}}}};
        auto storePanel = panel("storage.set", {{"key", "mark"}, {"value", QString(250, 'x')}});
        storePanel["id"] = "write";
        auto removeMissing = panel("storage.remove", {{"key", "missing"}});
        removeMissing["id"] = "remove-missing";
        p["contributions"] = QJsonArray{statePanel, storePanel, removeMissing};
        QVERIFY(PluginManifest::validate(p).isEmpty());
        auto noStorage = p; noStorage["permissions"] = QJsonArray{"ui.panels"};
        QVERIFY(!PluginManifest::validate(noStorage).isEmpty());
        QCOMPARE(add(host, p), QString());
        auto second = p; second["id"] = "second";
        auto secondPanel = second["contributions"].toArray();
        auto write = secondPanel[1].toObject();
        auto button = write["buttons"].toArray()[0].toObject();
        auto action = button["action"].toObject(); action["value"] = "two"; button["action"] = action;
        write["buttons"] = QJsonArray{button}; secondPanel[1] = write; second["contributions"] = secondPanel;
        QCOMPARE(add(host, second), QString());
        QCOMPARE(host.enable("first", true), QString());
        QCOMPARE(host.enable("second", true), QString());
        QCOMPARE(host.invokeButton("first", "write", 0), QString());
        QCOMPARE(host.invokeButton("first", "remove-missing", 0), QString());
        QCOMPARE(host.invokeButton("second", "write", 0), QString());
        const auto panels = host.panels();
        auto valueFor = [&](const QString &id) {
            for (const auto &entry : panels) {
                const auto row = entry.toMap();
                if (row["pluginId"] == id && row["contributionId"] == "state")
                    return row["displayItems"].toList()[1].toMap()["value"].toString();
            }
            return QString();
        };
        QCOMPARE(valueFor("first"), QString(160, 'x'));
        QCOMPARE(valueFor("second"), QString("two"));
        QCOMPARE(panels[0].toMap()["displayItems"].toList()[2].toMap()["available"].toBool(), false);
        QVERIFY(!host.setSetting("first", "enabled", false).size());
        QCOMPARE(host.panels()[0].toMap()["displayItems"].toList()[0].toMap()["value"].toBool(), false);
        const auto registryPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins.json";
        QFile beforeFile(registryPath); QVERIFY(beforeFile.open(QIODevice::ReadOnly)); const auto before = beforeFile.readAll(); beforeFile.close();
        QCOMPARE(host.invokeButton("first", "write", 0), QString());
        QFile afterFile(registryPath); QVERIFY(afterFile.open(QIODevice::ReadOnly)); QCOMPARE(afterFile.readAll(), before); afterFile.close();
        const auto firstData = host.installed()[0].toMap()["data"].toMap();
        QCOMPARE(host.setPermission("first", "storage", false), QString());
        QVERIFY(!host.invokeButton("first", "write", 0).isEmpty());
        QCOMPARE(host.installed()[0].toMap()["data"].toMap()["mark"].toString(), QString(250, 'x'));
        QCOMPARE(host.installed()[1].toMap()["data"].toMap()["mark"].toString(), QString("two"));
        QVERIFY(!firstData.isEmpty());
    }
    void preparedImportsAreBoundedAndCancellable() {
        Plugins host;
        auto p = basic();
        auto prepared = preview(host, p);
        QVERIFY(prepared["error"].toString().isEmpty());
        const auto id = prepared["operationId"].toString();
        host.cancelInstall(id);
        QVERIFY(!host.commitInstall(id, {}).isEmpty());
        QVERIFY(host.installed().isEmpty());
        const auto bad = host.prepareInstall(QUrl("https://example.org/plugin.json"));
        QVERIFY(bad["error"].toString().isEmpty());
        QSignalSpy spy(&host, &Plugins::installPrepared);
        QVERIFY(spy.wait(3000));
        QVERIFY(!spy.first()[0].toMap()["error"].toString().isEmpty());
    }
    void progressPayloadAndPermissionFiltering() {
        Plugins host;
        auto p = basic();
        p["permissions"] = QJsonArray{"player.observe", "storage"};
        p["rules"] = QJsonArray{QJsonObject{
            {"id", "progress"},
            {"event", "player.progress"},
            {"conditions", QJsonArray{}},
            {"actions",
             QJsonArray{QJsonObject{{"command", "storage.set"}, {"key", "seen"}, {"value", true}}}}}};
        QCOMPARE(add(host, p), QString());
        QCOMPARE(host.enable("test", true), QString());
        host.updatePlayer({{"position", 100}, {"duration", 1000}, {"access_token", "secret"}});
        host.enqueueEvent("player.progress", {{"positionMs", 100}, {"durationMs", 1000}});
        host.enqueueEvent("player.progress", {{"positionMs", 200}, {"durationMs", 1000}});
        QCOMPARE(host.m_events.size(), 1);
        QCOMPARE(host.m_events.head().payload.size(), 2);
        QCOMPARE(host.m_events.head().payload["positionMs"].toInt(), 200);
        QCOMPARE(host.setPermission("test", "player.observe", false), QString());
        QTest::qWait(20);
        QVERIFY(host.installed()[0].toMap()["data"].toMap().isEmpty());
        QCOMPARE(host.setPermission("test", "player.observe", true), QString());
        QSignalSpy snapshot(&host, &Plugins::progressSnapshotRequested);
        QVERIFY(QMetaObject::invokeMethod(&host.m_progressTimer, "timeout", Qt::DirectConnection));
        QCOMPARE(snapshot.count(), 1);
        QTRY_VERIFY(host.installed()[0].toMap()["data"].toMap()["seen"].toBool());
    }
    void storageQuotaAndTimerOwnership() {
        Plugins host;
        auto p = basic();
        p["permissions"] = QJsonArray{"ui.panels", "storage"};
        p["contributions"] =
            QJsonArray{panel("storage.set", {{"key", "new"}, {"value", QString(90000, 'x')}})};
        QCOMPARE(add(host, p), QString());
        QCOMPARE(host.enable("test", true), QString());
        auto next = host.entries;
        auto row = next[0].toMap();
        row["data"] = QVariantMap{{"old", QString(180000, 'y')}};
        next[0] = row;
        QCOMPARE(host.save(next), QString());
        QVERIFY(!host.invokeButton("test", "panel", 0).isEmpty());
        QVERIFY(!host.installed()[0].toMap()["data"].toMap().contains("new"));
        auto bad = basic("timer");
        bad["permissions"] = QJsonArray{"ui.panels", "timers"};
        bad["contributions"] = QJsonArray{panel("timer.cancel", {{"timerId", "foreign"}})};
        QVERIFY(!PluginManifest::validate(bad).isEmpty());
    }
    void corruptedRegistryDoesNotFallback() {
        const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QVERIFY(QDir().mkpath(dir));
        QFile file(dir + "/plugins.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("broken");
        file.close();
        Plugins host;
        QVERIFY(!host.error().isEmpty());
        QVERIFY(host.installed().isEmpty());
        QVERIFY(!host.enable("test", true).isEmpty());
    }
    void examplesAndQmlRender() {
        Plugins host;
        for (auto name : {"sleep-timer", "queue-tools", "calm-studio"}) {
            QFile file(QString(SCRIP_SOURCE_DIR) + "/examples/plugins/" + name + ".json");
            QVERIFY(file.open(QIODevice::ReadOnly));
            const auto parsed = PluginManifest::parse(file.readAll());
            QVERIFY2(parsed.error.isEmpty(), qPrintable(parsed.error));
            QCOMPARE(add(host, parsed.document), QString());
            QCOMPARE(host.enable(name, true), QString());
        }
        QCOMPARE(host.panels().size(), 4);
        QCOMPARE(host.theme()["accent"].toString(), QString("#238579"));
        QQuickWindow window;
        window.resize(360, 720);
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (auto error : errors)
                warnings.append(error.toString());
        });
        QQmlComponent view(&engine,
                           QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/PluginManagerView.qml"));
        QVERIFY2(view.isReady(), qPrintable(view.errorString()));
        QScopedPointer<QObject> object(
            view.createWithInitialProperties({{"manager", QVariant::fromValue(&host)},
                                              {"parent", QVariant::fromValue(window.contentItem())},
                                              {"width", 320}}));
        QVERIFY2(object, qPrintable(view.errorString()));
        QTest::qWait(30);
        std::function<bool(QQuickItem *)> hasCard = [&](QQuickItem *item) {
            if (item->objectName() == "plugin-card-sleep-timer")
                return true;
            for (auto *child : item->childItems())
                if (hasCard(child))
                    return true;
            return false;
        };
        QVERIFY(hasCard(qobject_cast<QQuickItem *>(object.data())));
        window.show();
        for (int width : {320, 960}) {
            window.resize(width, 720);
            object->setProperty("width", width);
            QTest::qWait(60);
            QVERIFY(window.grabWindow().save(QString("plugin-manager-%1.png").arg(width)));
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        auto update = basic("review-example");
        update["permissions"] = QJsonArray{"player.control"};
        auto prepared = preview(host, update);
        QVERIFY(prepared["error"].toString().isEmpty());
        object->setProperty("operationId", prepared["operationId"]);
        object->setProperty("preview", prepared);
        auto *review = object->findChild<QObject *>("plugin-install-review");
        QVERIFY(review);
        QVERIFY(QMetaObject::invokeMethod(review, "open"));
        QTest::qWait(60);
        QVERIFY(window.grabWindow().save("plugin-review.png"));
        window.resize(320, 720);
        object->setProperty("width", 320);
        QTest::qWait(60);
        QVERIFY(window.grabWindow().save("plugin-review-narrow.png"));
        QVERIFY(QMetaObject::invokeMethod(review, "close"));
        QTest::qWait(20);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        int uiCommands = 0;
        host.setPlayerCommand([&](const QString &command, const QJsonObject &) {
            if (command == "player.next")
                ++uiCommands;
            return QString();
        });
        for (auto slot : {"home", "profile", "player"}) {
            QQmlComponent panels(&engine,
                                 QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml/PluginPanels.qml"));
            QVERIFY2(panels.isReady(), qPrintable(panels.errorString()));
            QScopedPointer<QObject> panelObject(
                panels.createWithInitialProperties({{"manager", QVariant::fromValue(&host)},
                                                    {"parent", QVariant::fromValue(window.contentItem())},
                                                    {"slotName", slot},
                                                    {"width", 320}}));
            QVERIFY(panelObject);
            QTest::qWait(10);
            if (QString(slot) == "player") {
                std::function<QQuickItem *(QQuickItem *)> findNext = [&](QQuickItem *item) -> QQuickItem * {
                    if (item->property("text").toString() == "Следующий" &&
                        item->inherits("QQuickAbstractButton"))
                        return item;
                    for (auto *child : item->childItems())
                        if (auto *found = findNext(child))
                            return found;
                    return nullptr;
                };
                auto *next = findNext(qobject_cast<QQuickItem *>(panelObject.data()));
                QVERIFY(next);
                QVERIFY(QMetaObject::invokeMethod(next, "clicked", Qt::DirectConnection));
                QCOMPARE(uiCommands, 1);
            }
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    void migrationPreservesV1AndRejectsConcurrentWrites() {
        const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QVERIFY(QDir().mkpath(dir));
        const QJsonObject p{{"api", 1}, {"id", "studio"}, {"name", "Студия"},
            {"groups", QJsonArray{QJsonObject{{"label", "Акцент"}, {"accent", "#614899"}},
                                  QJsonObject{{"label", "Портреты"}, {"covers", "artist"}}}}};
        QJsonArray flags{false, true};
        QFile file(dir + "/plugins-v1.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonArray{QJsonObject{{"plugin", p}, {"enabled", true}, {"flags", flags}}})
                       .toJson());
        file.close();
        Plugins migrated;
        QVERIFY(migrated.error().isEmpty());
        QCOMPARE(migrated.installed().size(), 1);
        QVERIFY(!migrated.theme().contains("accent"));
        QVERIFY(QFile::exists(dir + "/plugins-v1.json"));
        QVERIFY(QFile::exists(dir + "/plugins-v1.json.bak"));
        Plugins concurrent;
        QCOMPARE(migrated.setSetting("studio", "legacy-group-0", true), QString());
        QVERIFY(!concurrent.enable("studio", false).isEmpty());
        QVERIFY(concurrent.installed()[0].toMap()["enabled"].toBool());
    }
    void unifiedMigrationPreservesBothRegistries() {
        const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QVERIFY(QDir().mkpath(dir));
        const QJsonObject legacy{{"api", 1}, {"id", "studio"}, {"name", "Студия"},
            {"groups", QJsonArray{QJsonObject{{"label", "Черничный акцент"}, {"accent", "#614899"},
                                              {"coverRadius", 16}},
                                  QJsonObject{{"label", "Портреты исполнителей"}, {"covers", "artist"}},
                                  QJsonObject{{"label", "Моя музыка"}, {"page", "home"},
                                    {"buttons", QJsonArray{QJsonObject{{"label", "Любимые треки"}, {"action", "likes"}}}}}}}};
        QJsonArray flags{false, true, true};
        QFile oldFile(dir + "/plugins-v1.json");
        QVERIFY(oldFile.open(QIODevice::WriteOnly));
        const auto oldBytes = QJsonDocument(QJsonArray{QJsonObject{{"plugin", legacy}, {"enabled", true}, {"flags", flags}}}).toJson();
        QCOMPARE(oldFile.write(oldBytes), oldBytes.size());
        oldFile.close();

        auto modern = basic("preserved-data");
        modern["permissions"] = QJsonArray{"storage", "ui.theme"};
        modern["settings"] = QJsonArray{QJsonObject{{"id", "enabled-look"}, {"type", "boolean"},
            {"label", "Оформление"}, {"default", false}}};
        modern["contributions"] = QJsonArray{QJsonObject{{"id", "look"}, {"type", "theme"},
            {"enabledSetting", "enabled-look"}, {"values", QJsonObject{{"accent", "#123456"}}}}};
        QJsonObject modernRow{{"plugin", modern}, {"enabled", true}, {"retained", false},
            {"grants", QJsonArray{"storage"}}, {"settings", QJsonObject{{"enabled-look", true}}},
            {"orphanSettings", QJsonObject{{"old-setting", 7}}}, {"data", QJsonObject{{"bookmark", "saved"}}}};
        const auto modernBytes = QJsonDocument(QJsonObject{{"format", 2}, {"registryRevision", 9},
            {"entries", QJsonArray{modernRow}}}).toJson();
        QFile currentFile(dir + "/plugins-v2.json");
        QVERIFY(currentFile.open(QIODevice::WriteOnly));
        QCOMPARE(currentFile.write(modernBytes), modernBytes.size());
        currentFile.close();

        Plugins migrated;
        QVERIFY2(migrated.error().isEmpty(), qPrintable(migrated.error()));
        QCOMPARE(migrated.installed().size(), 2);
        QCOMPARE(migrated.installed()[0].toMap()["plugin"].toMap()["id"].toString(), QString("preserved-data"));
        QCOMPARE(migrated.installed()[1].toMap()["plugin"].toMap()["id"].toString(), QString("studio"));
        auto stored = migrated.installed()[0].toMap();
        QCOMPARE(stored["settings"].toMap()["enabled-look"].toBool(), true);
        QCOMPARE(stored["grants"].toList(), QVariantList{QString("storage")});
        QCOMPARE(stored["orphanSettings"].toMap()["old-setting"].toInt(), 7);
        QCOMPARE(stored["data"].toMap()["bookmark"].toString(), QString("saved"));
        QVERIFY(!migrated.theme().contains("accent"));
        QCOMPARE(migrated.theme()["covers"].toString(), QString("artist"));
        QCOMPARE(migrated.setPermission("preserved-data", "ui.theme", true), QString());
        QCOMPARE(migrated.theme()["accent"].toString(), QString("#123456"));
        QCOMPARE(migrated.setSetting("studio", "legacy-group-0", true), QString());
        QCOMPARE(migrated.theme()["accent"].toString(), QString("#614899"));
        QFile legacyBackup(dir + "/plugins-v1.json.bak");
        QVERIFY(legacyBackup.open(QIODevice::ReadOnly));
        QCOMPARE(legacyBackup.readAll(), oldBytes);
        QFile currentBackup(dir + "/plugins-v2.json.bak");
        QVERIFY(currentBackup.open(QIODevice::ReadOnly));
        QCOMPARE(currentBackup.readAll(), modernBytes);
        QFile unified(dir + "/plugins.json");
        QVERIFY(unified.open(QIODevice::ReadOnly));
        const auto root = QJsonDocument::fromJson(unified.readAll()).object();
        QCOMPARE(root["format"].toInt(), 2);
        QCOMPARE(root["registryRevision"].toInteger(), 12);
    }
    void apiTwoSuiteManifestRemainsCompatible() {
        auto suite = basic("scrip-probe-suite");
        suite["name"] = "SCrip Probe Suite";
        suite["permissions"] = QJsonArray{"ui.theme"};
        QJsonArray themes;
        for (int i = 0; i < 6; ++i)
            themes.append(QJsonObject{{"id", QString("probe-theme-%1").arg(i)}, {"type", "theme"},
                {"values", QJsonObject{{"coverRadius", i * 4}}}});
        suite["contributions"] = themes;
        QVERIFY2(PluginManifest::validate(suite).isEmpty(), qPrintable(PluginManifest::validate(suite)));
        Plugins host;
        QCOMPARE(add(host, suite), QString());
        QCOMPARE(host.installed()[0].toMap()["plugin"].toMap(), suite.toVariantMap());
    }
    void androidHeaderReservesSpace() {
        QCoreApplication::setOrganizationName("SCripLayoutTests");
        AndroidLayoutModel sc;
        Plugins host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("sc", &sc);
        engine.rootContext()->setContextProperty("pluginManager", &host);
        QFile source(QString(SCRIP_SOURCE_DIR) + "/qml/android/Main.qml");
        QVERIFY(source.open(QIODevice::ReadOnly));
        QQmlComponent component(&engine);
        // Qt 6.8 повторно открывает URL inline-компонентов: используем настоящие файлы
        // в build/, с той же плоской структурой, что и QRC приложения.
        const auto fixture = QDir::currentPath() + "/android-layout-fixture";
        QVERIFY(QDir().mkpath(fixture));
        QFile mainFile(fixture + "/Main.qml");
        QVERIFY(mainFile.open(QIODevice::WriteOnly));
        mainFile.write(source.readAll()); mainFile.close();
        for (auto name : {"NetworkAccess.qml", "DiagnosticsPanel.qml", "RecommendationPreferences.qml", "RecommendationNotice.qml", "TrackInformation.qml", "TrackProgress.qml", "PlaybackQuality.qml", "SelectionInput.qml", "SelectionBar.qml", "TrackSelection.qml", "QueueActions.qml", "CollectionTools.qml", "TrackSearchFilters.qml", "PeoplePopup.qml", "SmoothWheel.qml", "SheetMotion.qml", "SwipeDismiss.qml", "MetadataLabel.qml", "SurfaceButton.qml", "ProfileExtras.qml", "ProfileStat.qml", "ProfileNavigation.qml", "EngagementAction.qml", "PlaylistEditor.qml", "CommentsPane.qml", "PlaylistPopup.qml", "Artwork.qml", "InteractionFeedback.qml", "SurfaceTransition.qml", "WavePanel.qml", "PluginAction.qml", "PluginPanels.qml", "PluginManagerView.qml", "PluginToolsPopup.qml", "RoundedImage.qml", "AndroidGlyph.qml"}) {
            const auto sourcePath = QString(SCRIP_SOURCE_DIR) + (QString(name) == "RoundedImage.qml" || QString(name) == "AndroidGlyph.qml" ? "/qml/android/" : "/qml/") + name;
            QFile::remove(fixture + "/" + name);
            QVERIFY(QFile::copy(sourcePath, fixture + "/" + name));
        }
        component.loadUrl(QUrl::fromLocalFile(fixture + "/Main.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(object.data());
        QVERIFY(window);
        auto *header = object->property("header").value<QQuickItem *>();
        QVERIFY(header);
        QQmlExpression expression(QQmlEngine::contextForObject(object.data()), object.data(), "body");
        auto *body = qobject_cast<QQuickItem *>(expression.evaluate().value<QObject *>());
        QVERIFY2(body, qPrintable(expression.error().toString()));
        QQmlExpression closeStartup(QQmlEngine::contextForObject(object.data()), object.data(), "startup.close()");
        closeStartup.evaluate();
        QVERIFY2(!closeStartup.hasError(), qPrintable(closeStartup.error().toString()));
        const int initialHomeRequests = sc.homeRequests;
        sc.profileSnapshot = {{"urn", "soundcloud:users:profile-ready"}, {"username", "Profile fixture"}};
        emit sc.catalogChanged();
        QTRY_VERIFY(sc.homeRequests > initialHomeRequests);
        QVERIFY(sc.forcedHome);
        QCOMPARE(object->property("page").toInt(), 0);
        for (const auto size : {QSize(412,860), QSize(320,640), QSize(860,412)}) {
            window->resize(size);
            QTest::qWait(40);
            QVERIFY2(header->height() >= 66, "Шапка потеряла высоту");
            const auto headerBottom = header->mapToScene(QPointF(0, header->height())).y();
            const auto bodyTop = body->mapToScene(QPointF(0, 0)).y();
            QVERIFY2(bodyTop >= headerBottom - 0.5, qPrintable(QString("Содержимое %1 перекрывает шапку %2").arg(bodyTop).arg(headerBottom)));
            QVERIFY(window->grabWindow().save(QString("recommendations-home-android-%1.png").arg(size.width())));
            std::function<QQuickItem *(QQuickItem *)> findCaption = [&](QQuickItem *item) -> QQuickItem * {
                if (item->property("text").toString().startsWith("Похоже на «")) return item;
                for (auto *child : item->childItems()) if (auto *found = findCaption(child)) return found;
                return nullptr;
            };
            auto *caption = findCaption(window->contentItem()); QVERIFY(caption);
            QVERIFY(caption->width() <= size.width() - 36);
            auto *flick = caption->parentItem();
            while (flick && flick->metaObject()->indexOfProperty("contentY") < 0) flick = flick->parentItem();
            QVERIFY(flick);
            const auto maximum = qMax(0.0, flick->property("contentHeight").toReal() - flick->height());
            flick->setProperty("contentY", qBound(0.0, caption->mapToScene(QPointF()).y() - bodyTop - 40, maximum));
            QTest::qWait(80);
            QVERIFY(window->grabWindow().save(QString("recommendations-caption-android-%1.png").arg(size.width())));
            flick->setProperty("contentY", 0);
        }
        object->setProperty("page", 4);
        QTest::qWait(80);
        auto *collectionTools = object->findChild<QQuickItem *>("collectionTools");
        auto *selectionBar = object->findChild<QQuickItem *>("collectionSelectionBar");
        QVERIFY(collectionTools && selectionBar);
        QVERIFY(!collectionTools->isVisible()); QVERIFY(!selectionBar->isVisible());
        QVERIFY(window->grabWindow().save("android-profile-no-tools.png"));
        object->setProperty("page", 1);
        QTest::qWait(40);
        QVERIFY(collectionTools->isVisible()); QVERIFY(selectionBar->isVisible());
        object->setProperty("page", 5);
        QTest::qWait(80);
        for (const auto size : {QSize(320,640), QSize(412,860)}) {
            window->resize(size); QTest::qWait(50);
            for (const auto name : {"playbackQualityControl", "preloadControl", "preloadWifiControl"}) {
                auto *control = object->findChild<QQuickItem *>(QString::fromLatin1(name)); QVERIFY(control);
                QVERIFY(control->isVisible());
                const auto rect = control->mapRectToScene(control->boundingRect());
                QVERIFY2(rect.left() >= 0 && rect.right() <= size.width(), qPrintable(QString("%1: left=%2 right=%3 window=%4").arg(name).arg(rect.left()).arg(rect.right()).arg(size.width())));
                QVERIFY(!QQmlProperty(control, "Accessible.name", QQmlEngine::contextForObject(control)).read().toString().isEmpty());
            }
            auto *quality = object->findChild<QQuickItem *>("playbackQualityControl");
            auto *scroll = quality->parentItem();
            while (scroll && scroll->metaObject()->indexOfProperty("contentY") < 0) scroll = scroll->parentItem();
            QVERIFY(scroll);
            scroll->setProperty("contentY", qMax(0.0, quality->mapToItem(scroll, QPointF()).y() + scroll->property("contentY").toReal() - 90));
            QTest::qWait(80);
            QVERIFY(window->grabWindow().save(QString("playback-settings-android-%1.png").arg(size.width())));
            auto *copyLog = object->findChild<QQuickItem *>("copyDiagnosticLog");
            auto *openIssue = object->findChild<QQuickItem *>("openDiagnosticIssue");
            QVERIFY(copyLog && openIssue);
            for (auto *button : {copyLog, openIssue}) {
                const auto rect = button->mapRectToScene(button->boundingRect());
                QVERIFY(rect.left() >= 0 && rect.right() <= size.width());
                QVERIFY(button->height() >= 44);
            }
            scroll->setProperty("contentY", qMax(0.0, scroll->property("contentHeight").toReal() - scroll->height()));
            QTest::qWait(80);
            QVERIFY(window->grabWindow().save(QString("diagnostics-settings-android-%1.png").arg(size.width())));

        }
        object->setProperty("page", 0);
        for (const auto size : {QSize(320,640), QSize(412,860)}) {
            window->resize(size);
            QQmlExpression menu(QQmlEngine::contextForObject(object.data()), object.data(), "queueOptions.open()");
            menu.evaluate(); QVERIFY2(!menu.hasError(), qPrintable(menu.error().toString()));
            QTest::qWait(450);
            QQmlExpression geometry(QQmlEngine::contextForObject(object.data()), object.data(), "queueOptions.height");
            QVERIFY(geometry.evaluate().toReal() >= 180);
            QVERIFY(window->grabWindow().save(QString("queue-menu-android-%1.png").arg(size.width())));
            QQmlExpression closeMenu(QQmlEngine::contextForObject(object.data()), object.data(), "queueOptions.close()");
            closeMenu.evaluate(); QVERIFY(!closeMenu.hasError());
        }
        QTest::qWait(400);
        // Exercise the actual Android menu, including touches starting on a button.
        window->resize(412, 860);
        auto *touchDevice = QTest::createTouchDevice();
        // A mini-player swipe must reveal the surface while the finger is held,
        // including reversal, rather than triggering an opening animation.
        auto *dock = object->findChild<QQuickItem *>("playbackDock"); QVERIFY(dock);
        auto *player = object->findChild<QObject *>("playerSheet"); QVERIFY(player);
        const QPoint dockStart = dock->mapToScene(QPointF(40, 30)).toPoint();
        QTest::touchEvent(window, touchDevice).press(0, dockStart, window);
        for (int dy : {25, 60, 120, 180}) {
            QTest::touchEvent(window, touchDevice).move(0, dockStart - QPoint(0, dy), window);
            QTest::qWait(20);
        }
        QVERIFY(player->property("visible").toBool());
        const qreal dragged = player->property("dragOffset").toReal();
        QVERIFY(dragged > 0 && dragged < player->property("height").toReal());
        auto *playerBackground = player->property("background").value<QQuickItem *>();
        auto *playerContent = player->property("contentItem").value<QQuickItem *>();
        QVERIFY(playerBackground); QVERIFY(playerContent);
        // A partially raised player must leave the original page visible above
        // the surface, not a stationary full-screen background or dimmer.
        QVERIFY(playerBackground->mapToScene(QPointF()).y() > 100);
        QVERIFY2(qAbs(playerBackground->mapToScene(QPointF()).y() - dragged) < 2,
            qPrintable(QString("background=%1 offset=%2 popupY=%3 reveal=%4")
                .arg(playerBackground->mapToScene(QPointF()).y()).arg(dragged)
                .arg(player->property("y").toReal()).arg(player->property("reveal").toReal())));
        QVERIFY(qAbs(playerContent->mapToScene(QPointF()).y()
            - playerBackground->mapToScene(QPointF()).y() - player->property("topPadding").toReal()) < 1);
        QVERIFY(!player->property("dim").toBool());
        QTest::qWait(100);
        QCOMPARE(player->property("dragOffset").toReal(), dragged);
        QTest::touchEvent(window, touchDevice).move(0, dockStart - QPoint(0, 150), window);
        QTest::qWait(20);
        QVERIFY(qAbs(player->property("dragOffset").toReal() - dragged - 30) < 2);
        QTest::touchEvent(window, touchDevice).release(0, dockStart - QPoint(0, 150), window);
        // Cleanup also exercises interruption of a settling surface by a button.
        QVERIFY(QMetaObject::invokeMethod(player, "close"));
        QTRY_VERIFY(!player->property("visible").toBool());
        const auto openTrackMenu = [&] {
            QQmlExpression menu(QQmlEngine::contextForObject(object.data()), object.data(),
                "app.selectedTrack = {urn: 'soundcloud:tracks:swipe', title: 'бамжыха иуу (prod kurane🥩❤️🙏🏻🤙🏻, kayoshi)', user_favorite: true, favoritings_count: 336, reposts_count: 11, station_urn: 'soundcloud:system-playlists:fixture', permalink_url: 'https://soundcloud.com/artist/track', user: {urn: 'soundcloud:users:fixture', username: 'hinizaza (og shiba🐶)#mæjilgang'}}; trackMenu.open(); trackMenu");
            auto *popup = menu.evaluate().value<QObject *>();
            if (menu.hasError()) return static_cast<QObject *>(nullptr);
            return popup;
        };
        for (const auto size : {QSize(320, 640), QSize(412, 860), QSize(860, 412)}) {
            window->resize(size);
            auto *menu = openTrackMenu(); QVERIFY(menu);
            QTRY_VERIFY(menu->property("opened").toBool());
            QCOMPARE(menu->property("leftPadding").toReal(), 16.0);
            QCOMPARE(menu->property("rightPadding").toReal(), 16.0);
            auto *cover = object->findChild<QQuickItem *>("trackActionsCover"); QVERIFY(cover);
            auto *scroll = object->findChild<QQuickItem *>("trackActionsScroll"); QVERIFY(scroll);
            const QPointF coverPosition = cover->mapToScene(QPointF());
            scroll->setProperty("contentY", qMin(100.0, qMax(0.0, scroll->property("contentHeight").toReal() - scroll->height())));
            QTest::qWait(30);
            QCOMPARE(cover->mapToScene(QPointF()), coverPosition);
            scroll->setProperty("contentY", 0);
            QVERIFY(window->grabWindow().save(QString("android-track-menu-dark-%1.png").arg(size.width())));
            QQmlExpression light(QQmlEngine::contextForObject(object.data()), object.data(), "prefs.darkMode = false");
            light.evaluate(); QVERIFY(!light.hasError()); QTest::qWait(250);
            QVERIFY(window->grabWindow().save(QString("android-track-menu-light-%1.png").arg(size.width())));
            QQmlExpression dark(QQmlEngine::contextForObject(object.data()), object.data(), "prefs.darkMode = true");
            dark.evaluate(); QTest::qWait(250);
            QVERIFY(QMetaObject::invokeMethod(menu, "close"));
            QTRY_VERIFY(!menu->property("visible").toBool());
        }
        window->resize(412, 860);
        for (const int startOffset : {28, 70}) {
            auto *menu = openTrackMenu(); QVERIFY(menu);
            QTRY_VERIFY(menu->property("opened").toBool());
            auto *cover = object->findChild<QQuickItem *>("trackActionsCover"); QVERIFY(cover);
            auto *menuTitle = object->findChild<QQuickItem *>("trackActionsTitle"); QVERIFY(menuTitle);
            auto *menuArtist = object->findChild<QQuickItem *>("trackActionsArtist"); QVERIFY(menuArtist);
            QVERIFY(menuTitle->height() < 40);
            QVERIFY(menuArtist->height() < 35);
            const auto coverRect = cover->mapRectToScene(cover->boundingRect());
            QVERIFY(coverRect.left() >= 20);
            QVERIFY(coverRect.right() < window->width() - 20);
            QVERIFY(coverRect.top() >= menu->property("y").toReal() + 28);
            QVERIFY(window->grabWindow().save("android-track-menu-polished.png"));
            const int top = qRound(menu->property("y").toReal());
            const QPoint start(200, top + startOffset);
            QTest::touchEvent(window, touchDevice).press(0, start, window);
            for (int dy = 20; dy <= 300; dy += 20) {
                QTest::touchEvent(window, touchDevice).move(0, start + QPoint(0, dy), window);
                QTest::qWait(16);
            }
            QVERIFY(menu->property("position").toReal() < 0.9);
            QTest::touchEvent(window, touchDevice).release(0, start + QPoint(0, 300), window);
            QTRY_VERIFY(!menu->property("visible").toBool());
        }
        auto *shortSwipeMenu = openTrackMenu(); QVERIFY(shortSwipeMenu);
        QTRY_VERIFY(shortSwipeMenu->property("opened").toBool());
        const QPoint shortStart(200, qRound(shortSwipeMenu->property("y").toReal()) + 30);
        QTest::touchEvent(window, touchDevice).press(0, shortStart, window);
        QTest::touchEvent(window, touchDevice).move(0, shortStart + QPoint(0, 25), window);
        QTest::qWait(30);
        QTest::touchEvent(window, touchDevice).move(0, shortStart + QPoint(0, 35), window);
        QTest::qWait(150);
        QTest::touchEvent(window, touchDevice).release(0, shortStart + QPoint(0, 35), window);
        QTest::qWait(16);
        const QPoint catchHeader(200, qRound(shortSwipeMenu->property("y").toReal()) + 20);
        QTest::touchEvent(window, touchDevice).press(0, catchHeader, window);
        const qreal caughtPosition = shortSwipeMenu->property("position").toReal();
        QTest::qWait(100);
        QVERIFY(qAbs(shortSwipeMenu->property("position").toReal() - caughtPosition) < 0.001);
        QTest::touchEvent(window, touchDevice).release(0, catchHeader, window);
        QTRY_COMPARE(shortSwipeMenu->property("position").toReal(), 1.0);
        QTRY_VERIFY(shortSwipeMenu->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(shortSwipeMenu, "close"));
        QTRY_VERIFY(!shortSwipeMenu->property("visible").toBool());
        window->resize(320, 500);
        auto *menu = openTrackMenu(); QVERIFY(menu);
        QTRY_VERIFY(menu->property("opened").toBool());
        auto *menuScroll = object->findChild<QQuickItem *>("trackActionsScroll"); QVERIFY(menuScroll);
        QVERIFY(menuScroll->property("contentHeight").toReal() > menuScroll->height());
        const QPoint scrollStart = menuScroll->mapToScene(QPointF(menuScroll->width() / 2, menuScroll->height() - 30)).toPoint();
        QVERIFY(menuScroll->isVisible());
        QVERIFY(menuScroll->property("interactive").toBool());
        QTest::touchEvent(window, touchDevice).press(0, scrollStart, window);
        for (int dy = 20; dy <= 220; dy += 20) {
            QTest::touchEvent(window, touchDevice).move(0, scrollStart - QPoint(0, dy), window);
            QTest::qWait(16);
        }
        QTest::touchEvent(window, touchDevice).release(0, scrollStart - QPoint(0, 220), window);
        QTRY_VERIFY2(menuScroll->property("contentY").toReal() > 0,
            qPrintable(QString("viewport %1x%2, content %3, start %4,%5, visible %6")
                .arg(menuScroll->width()).arg(menuScroll->height()).arg(menuScroll->property("contentHeight").toReal())
                .arg(scrollStart.x()).arg(scrollStart.y()).arg(menuScroll->isVisible())));
        QVERIFY(menu->property("opened").toBool());
        // Exercise the actual menu viewport, including its nested gesture ownership.
        const auto startFlick = [&](qreal velocity) {
            QMetaObject::invokeMethod(menuScroll, "cancelFlick");
            menuScroll->setProperty("contentY", 0.0);
            return QMetaObject::invokeMethod(menuScroll, "flick", Q_ARG(qreal, 0.0), Q_ARG(qreal, velocity));
        };
        QVERIFY(startFlick(-200));
        QTRY_VERIFY(!menuScroll->property("flicking").toBool());
        const qreal slowDistance = menuScroll->property("contentY").toReal();
        QVERIFY(slowDistance > 0);
        QVERIFY(startFlick(-800));
        QTRY_VERIFY(!menuScroll->property("flicking").toBool());
        QVERIFY(menuScroll->property("contentY").toReal() > slowDistance * 2);
        QVERIFY(startFlick(-800));
        QTest::qWait(40);
        QVERIFY(menuScroll->property("flicking").toBool());
        const QPoint hold = menuScroll->mapToScene(QPointF(menuScroll->width() / 2, menuScroll->height() / 2)).toPoint();
        QTest::touchEvent(window, touchDevice).press(0, hold, window);
        QTRY_VERIFY(!menuScroll->property("flicking").toBool());
        const qreal heldPosition = menuScroll->property("contentY").toReal();
        QTest::qWait(100);
        QVERIFY(qAbs(menuScroll->property("contentY").toReal() - heldPosition) < 1);
        for (int dy = 10; dy <= 60; dy += 10) {
            QTest::touchEvent(window, touchDevice).move(0, hold + QPoint(0, dy), window);
            QTest::qWait(16);
        }
        QVERIFY(menuScroll->property("contentY").toReal() < heldPosition);
        QTest::touchEvent(window, touchDevice).release(0, hold + QPoint(0, 60), window);
        QVERIFY(menu->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(!menu->property("visible").toBool());
        QQmlExpression publicProfile(QQmlEngine::contextForObject(object.data()), object.data(),
            "app.openProfile({urn: 'soundcloud:users:public', username: 'North Records'}); app.profileTab = 2; app.refresh()");
        publicProfile.evaluate(); QVERIFY2(!publicProfile.hasError(), qPrintable(publicProfile.error().toString()));
        for (const auto size : {QSize(320,640), QSize(412,860)}) {
            window->resize(size); QTest::qWait(100);
            auto *navigation = window->findChild<QQuickItem *>("profileNavigation"); QVERIFY(navigation);
            QVERIFY(navigation->isVisible()); QVERIFY(navigation->width() <= size.width() - 36);
            std::function<QQuickItem *(QQuickItem *, const QString &)> findTab = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
                if (item->objectName() == name) return item;
                for (auto *child : item->childItems()) if (auto *found = findTab(child, name)) return found;
                return nullptr;
            };
            qreal previousRight = 0;
            for (int i = 0; i < 4; ++i) {
                auto *tab = findTab(navigation, QString("profileSection%1").arg(i)); QVERIFY(tab);
                QVERIFY(tab->x() >= previousRight - 1); previousRight = tab->x() + tab->width();
                QCOMPARE(tab->height(), 44.0);
                QVERIFY(tab->width() >= 35);
            }
            QTest::qWait(400);
            QQmlExpression listExpression(QQmlEngine::contextForObject(object.data()), object.data(), "tracks");
            auto *profileList = qobject_cast<QQuickItem *>(listExpression.evaluate().value<QObject *>()); QVERIFY(profileList);
            const auto offset = navigation->mapToScene(QPointF()).y() - body->mapToScene(QPointF()).y() - 28;
            profileList->setProperty("contentY", profileList->property("contentY").toReal() + offset);
            QTest::qWait(100);
            QCOMPARE(navigation->property("section").toInt(), 2);
            auto *indicator = navigation->findChild<QQuickItem *>("profileSectionIndicator"); QVERIFY(indicator);
            QVERIFY(indicator->width() > 0); QVERIFY(indicator->x() > 0);
            auto *tabViewport = navigation->findChild<QQuickItem *>("profileTabsViewport"); QVERIFY(tabViewport);
            QVERIFY(tabViewport->property("contentX").toReal() >= 0);
            QVERIFY(tabViewport->property("contentX").toReal() <= qMax(0.0, tabViewport->property("contentWidth").toReal() - tabViewport->width()) + 1);
            QCOMPARE(sc.lastSection, "userLikes"); QCOMPARE(sc.lastUser, "soundcloud:users:public");
            QVERIFY(window->grabWindow().save(QString("public-profile-android-%1.png").arg(size.width())));
            auto *likes = findTab(navigation, "profileSection2"); QVERIFY(likes);
            QVERIFY(QMetaObject::invokeMethod(likes, "clicked")); QTest::qWait(100);
            QVERIFY(window->grabWindow().save(QString("public-profile-android-menu-%1.png").arg(size.width())));
            auto *menu = navigation->findChild<QObject *>("profileLikesMenu"); QVERIFY(menu);
            QVERIFY(QMetaObject::invokeMethod(menu, "close")); QTest::qWait(150);
        }
        for (const auto &pair : QList<QPair<QString, QString>>{{"likes", "userLikes"}, {"likedPlaylists", "userLikedPlaylists"}, {"likedStations", "userLikedStations"}}) {
            object->setProperty("profileLikes", pair.first);
            QQmlExpression refresh(QQmlEngine::contextForObject(object.data()), object.data(), "app.refresh()"); refresh.evaluate();
            QCOMPARE(sc.lastSection, pair.second);
        }
        QQmlExpression publicReposts(QQmlEngine::contextForObject(object.data()), object.data(), "app.profileTab = 3; app.repostLibrary = 'repostedPlaylists'; app.refresh()");
        publicReposts.evaluate(); QCOMPARE(sc.lastSection, "userRepostedPlaylists");
        QQmlExpression showFilters(QQmlEngine::contextForObject(object.data()), object.data(), "app.page = 2; trackFilters.expanded = true; trackFilters");
        auto *filters = qobject_cast<QQuickItem *>(showFilters.evaluate().value<QObject *>());
        QVERIFY2(filters, qPrintable(showFilters.error().toString()));
        for (const auto size : {QSize(320,640), QSize(412,860)}) {
            window->resize(size); QTest::qWait(80);
            QVERIFY(filters->isVisible());
            QVERIFY(filters->width() <= size.width() - 39);
            auto *viewport = filters->parentItem();
            QVERIFY(viewport);
            // The Flickable content item carries the full panel; its parent is the viewport.
            auto *flick = viewport->parentItem();
            QVERIFY(flick);
            QVERIFY(flick->height() <= window->height() * 0.42 + 1);
            QVERIFY(window->grabWindow().save(QString("android-search-%1.png").arg(size.width())));
        }
        QQmlExpression openRepeatPlayer(QQmlEngine::contextForObject(object.data()), object.data(), "player.open()");
        openRepeatPlayer.evaluate(); QVERIFY(!openRepeatPlayer.hasError());
        std::function<QQuickItem *(QQuickItem *)> findRepeat = [&](QQuickItem *item) -> QQuickItem * {
            if (item->objectName() == "repeatModeButton") return item;
            for (auto *child : item->childItems()) if (auto *found = findRepeat(child)) return found;
            return nullptr;
        };
        for (int width : {320, 412}) {
            window->resize(width, 860);
            for (int mode = 0; mode < 3; ++mode) {
                sc.setRepeatMode(mode); QTest::qWait(400);
                auto *button = findRepeat(window->contentItem()); QVERIFY(button); QVERIFY(button->isVisible());
                QCOMPARE(button->property("symbol").toString(), QStringList({"repeatOff", "repeatOne", "repeat"})[mode]);
                QCOMPARE(QQmlProperty(button, "Accessible.name", QQmlEngine::contextForObject(button)).read().toString(), QStringList({"Повтор выключен", "Повтор трека", "Повтор очереди"})[mode]);
                const auto rect = button->mapRectToScene(button->boundingRect());
                QVERIFY(rect.left() >= 0); QVERIFY(rect.right() <= width);
                QVERIFY(rect.top() >= 0); QVERIFY(rect.bottom() <= window->height());
                QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
                QCOMPARE(sc.repeatMode, (mode + 1) % 3);
                sc.setRepeatMode(mode); QTest::qWait(20);
                QVERIFY(window->grabWindow().save(QString("repeat-android-%1-%2.png").arg(width).arg(mode)));
            }
        }
        QQmlExpression closePlayer(QQmlEngine::contextForObject(object.data()), object.data(), "player.close()");
        closePlayer.evaluate(); QVERIFY(!closePlayer.hasError());
        QQmlExpression restorePage(QQmlEngine::contextForObject(object.data()), object.data(), "app.page = 0");
        restorePage.evaluate();
        QTest::qWait(950);
        window->resize(412,860); QTest::qWait(40);
        QVERIFY(window->grabWindow().save("android-header.png"));
        const auto findStationCard = [&]() -> QQuickItem * {
            std::function<QQuickItem *(QQuickItem *)> visit = [&](QQuickItem *item) -> QQuickItem * {
                if (item->objectName() == "artistStationCard0") return item;
                for (auto *child : item->childItems()) if (auto *found = visit(child)) return found;
                return nullptr;
            };
            return visit(window->contentItem());
        };
        QTRY_VERIFY(findStationCard());
        QVERIFY(QMetaObject::invokeMethod(findStationCard(), "clicked"));
        QCOMPARE(sc.lastPreview["preview_type"].toString(), QString("station"));
        QCOMPARE(sc.lastPreview["urn"].toString(), QString("soundcloud:tracks:seed"));
        QTest::qWait(300);
        QVERIFY(window->grabWindow().save("android-artist-station.png"));
        QQmlExpression preview(QQmlEngine::contextForObject(object.data()), object.data(),
            "playlistView.playlist = {title: 'Вечерняя коллекция', track_count: 2, user: {username: 'SCrip'}}; playlistView.open()");
        preview.evaluate();
        QVERIFY2(!preview.hasError(), qPrintable(preview.error().toString()));
        QQmlExpression enteringPreviewExpression(QQmlEngine::contextForObject(object.data()), object.data(), "playlistView");
        auto *enteringPreview = enteringPreviewExpression.evaluate().value<QObject *>(); QVERIFY(enteringPreview);
        QTRY_VERIFY(enteringPreview->property("opacity").toReal() > 0
            && enteringPreview->property("opacity").toReal() < 1);
        auto *enteringBackground = enteringPreview->property("background").value<QQuickItem *>(); QVERIFY(enteringBackground);
        auto *enteringContent = enteringPreview->property("contentItem").value<QQuickItem *>(); QVERIFY(enteringContent);
        QVERIFY(enteringBackground->mapToScene(QPointF()).y() > enteringPreview->property("y").toReal());
        QVERIFY(qAbs(enteringContent->mapToScene(QPointF()).y()
            - enteringBackground->mapToScene(QPointF()).y() - enteringPreview->property("topPadding").toReal()) < 1);
        QTRY_VERIFY(enteringPreview->property("opened").toBool());
        QVERIFY(window->grabWindow().save("android-playlist.png"));
        std::function<QQuickItem *(QQuickItem *)> findTrackActions = [&](QQuickItem *item) -> QQuickItem * {
            if (item->objectName() == "previewTrackActions0") return item;
            for (auto *child : item->childItems()) if (auto *found = findTrackActions(child)) return found;
            return nullptr;
        };
        QTRY_VERIFY(findTrackActions(window->contentItem()));
        QQmlExpression previewObject(QQmlEngine::contextForObject(object.data()), object.data(), "playlistView");
        auto *previewPopup = previewObject.evaluate().value<QObject *>(); QVERIFY(previewPopup);
        auto *previewBackground = previewPopup->property("background").value<QQuickItem *>(); QVERIFY(previewBackground);
        const QPoint previewStart = previewBackground->mapToScene(QPointF(140, 35)).toPoint();
        const qreal previewTop = previewBackground->mapToScene(QPointF()).y();
        QTest::touchEvent(window, touchDevice).press(0, previewStart, window);
        for (int dy : {25, 60, 120, 200}) {
            QTest::touchEvent(window, touchDevice).move(0, previewStart + QPoint(0, dy), window);
            QTest::qWait(20);
        }
        QVERIFY(previewBackground->mapToScene(QPointF()).y() > previewTop + 100);
        const qreal heldPreviewTop = previewBackground->mapToScene(QPointF()).y();
        QTest::qWait(100);
        QVERIFY(qAbs(previewBackground->mapToScene(QPointF()).y() - heldPreviewTop) < 1);
        QTest::touchEvent(window, touchDevice).move(0, previewStart + QPoint(0, 170), window);
        QTest::qWait(20);
        QVERIFY(qAbs(previewBackground->mapToScene(QPointF()).y() - heldPreviewTop + 30) < 2);
        QTest::qWait(150);
        QTest::touchEvent(window, touchDevice).release(0, previewStart + QPoint(0, 170), window);
        QTRY_VERIFY(!previewPopup->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(previewPopup, "open"));
        QTRY_VERIFY(previewPopup->property("opened").toBool());
        auto *actionsButton = findTrackActions(window->contentItem());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            actionsButton->mapToScene(QPointF(actionsButton->width() / 2, actionsButton->height() / 2)).toPoint());
        QQmlExpression actionMenuObject(QQmlEngine::contextForObject(object.data()), object.data(), "trackMenu");
        auto *actionMenu = actionMenuObject.evaluate().value<QObject *>();
        QVERIFY(actionMenu);
        QTest::qWait(80);
        auto *menuBackground = actionMenu->property("background").value<QQuickItem *>();
        auto *menuBody = actionMenu->property("contentItem").value<QQuickItem *>();
        QVERIFY(menuBackground); QVERIFY(menuBody);
        const auto contentInset = actionMenu->property("topPadding").toReal();
        const auto checkSurface = [&] {
            return qAbs(menuBody->mapToScene(QPointF()).y() - menuBackground->mapToScene(QPointF()).y() - contentInset) < 1;
        };
        QVERIFY2(checkSurface(), "Menu background and content separated during entrance");
        QVERIFY(window->grabWindow().save("android-track-actions-opening.png"));
        QTest::qWait(60);
        QVERIFY2(checkSurface(), "Menu background and content moved at different speeds");
        QTRY_VERIFY(actionMenu->property("opened").toBool());
        QVERIFY(window->grabWindow().save("android-track-actions.png"));
        QVERIFY(QMetaObject::invokeMethod(actionMenu, "close"));
        QTRY_VERIFY(!actionMenu->property("visible").toBool());
        QQmlExpression infoFixture(QQmlEngine::contextForObject(object.data()), object.data(),
            "trackInfoSheet.track = {urn: 'soundcloud:tracks:selected', title: 'Selected track with a very long title that must wrap on a small screen', metadata_artist: 'Credited artist', user: {urn: 'soundcloud:users:uploader', username: 'Uploader'}, duration: 123000, description: ('Long description with multiple paragraphs.\\n').repeat(60), bpm: 125, release_year: 2024, release_month: 2, tag_list: 'rock \"hip hop\"', license: 'cc-by-nc-sa'}; trackInfoSheet.open(); trackInfoSheet");
        auto *info = infoFixture.evaluate().value<QObject *>();
        QVERIFY2(!infoFixture.hasError(), qPrintable(infoFixture.error().toString())); QVERIFY(info);
        QTRY_VERIFY(info->property("opened").toBool());
        for (auto size : {QSize(320,640), QSize(412,860), QSize(860,412)}) {
            window->resize(size); QTest::qWait(50);
            QVERIFY(info->property("height").toReal() <= window->height());
            QCOMPARE(info->property("track").toMap().value("urn").toString(), "soundcloud:tracks:selected");
            QVERIFY(window->grabWindow().save(QString("track-info-android-%1.png").arg(size.width())));
        }
        QQmlExpression infoBack(QQmlEngine::contextForObject(object.data()), object.data(), "goBack()");
        QVERIFY(infoBack.evaluate().toBool()); QTRY_VERIFY(!info->property("visible").toBool());
        window->resize(320, 640);
        QTest::qWait(350);
        auto *previewList = window->findChild<QQuickItem *>("previewTrackList"); QVERIFY(previewList);
        QVERIFY(previewList->height() >= 72);
        QVERIFY(window->grabWindow().save("android-playlist-compact.png"));
        window->resize(860, 412); QTest::qWait(350);
        QVERIFY(previewList->height() >= 72);
        QVERIFY(window->grabWindow().save("android-playlist-landscape.png"));
        QQmlExpression dismissPreview(QQmlEngine::contextForObject(object.data()), object.data(), "playlistView.close()");
        dismissPreview.evaluate();
        window->resize(412, 860);
        QTest::qWait(300);

        QQmlExpression miniExpression(QQmlEngine::contextForObject(object.data()), object.data(), "miniPlayer");
        auto *mini = qobject_cast<QQuickItem *>(miniExpression.evaluate().value<QObject *>());
        QVERIFY(mini);
        QQmlExpression playerVisible(QQmlEngine::contextForObject(object.data()), object.data(), "player.visible");
        const QPoint start = mini->mapToScene(QPointF(100, mini->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        for (int y = 10; y <= 300; y += 10) QTest::mouseMove(window, start - QPoint(0, y), 10);
        QTest::qWait(150);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start - QPoint(0, 300));
        QTRY_VERIFY_WITH_TIMEOUT(playerVisible.evaluate().toBool(), 1500);
        QTRY_COMPARE(player->property("dragOffset").toReal(), 0.0);
        QTRY_VERIFY(!player->property("manualMotion").toBool());
        QTest::qWait(500);
        auto *commentPreview = window->findChild<QQuickItem *>("playingCommentPreview"); QVERIFY(commentPreview);
        QVERIFY(commentPreview->isVisible());
        QTRY_COMPARE(commentPreview->property("displayedComment").toMap()["timestamp"].toInt(), 0);
        QVERIFY(window->grabWindow().save("android-playing-comment.png"));
        sc.seek(11000);
        QTRY_COMPARE(commentPreview->property("displayedComment").toMap()["timestamp"].toInt(), 10000);
        QTest::qWait(400);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, commentPreview->mapToScene(QPointF(commentPreview->width() / 2, commentPreview->height() / 2)).toPoint());
        auto *commentsSheet = window->findChild<QObject *>("commentsSheet"); QVERIFY(commentsSheet);
        QTRY_VERIFY(commentsSheet->property("opened").toBool());
        auto *commentPane = window->findChild<QQuickItem *>("mobileCommentsPane"); QVERIFY(commentPane);
        QVERIFY(commentPane->isVisible());
        auto *input = window->findChild<QObject *>("trackCommentInput"); QVERIFY(input);
        auto *timestamp = window->findChild<QObject *>("commentTimestampInput"); QVERIFY(timestamp);
        input->setProperty("text", "Очень красивый переход"); timestamp->setProperty("text", "0:12");
        auto *sendComment = window->findChild<QObject *>("postCommentButton"); QVERIFY(sendComment);
        QVERIFY(sendComment->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(sendComment, "clicked"));
        QCOMPARE(sc.publishedBody, QString("Очень красивый переход")); QCOMPARE(sc.publishedTimestamp, 12000);
        QVERIFY(input->property("text").toString().isEmpty());
        QTest::qWait(400);
        QVERIFY(window->grabWindow().save("android-comments-sheet.png"));
        window->resize(320,640); QTest::qWait(300);
        QVERIFY(window->grabWindow().save("android-comments-compact.png"));
        QVERIFY(commentPane->width() <= window->width());
        QVERIFY(QMetaObject::invokeMethod(commentsSheet, "close"));
        QTRY_VERIFY(!commentsSheet->property("visible").toBool());
        window->resize(412,860); QTest::qWait(300);
        sc.currentSnapshot["title"] = "бамжыха иуу (prod kurane🥩❤️🙏🏻🤙🏻, kayoshi) — Длинное название композиции, которое занимает две строки на экране телефона";
        sc.currentSnapshot["user"] = QVariantMap{{"username", QStringLiteral("hinizaza (og shiba🐶)#mæjilgang")}};
        emit sc.catalogChanged();
        auto *details = object->findChild<QQuickItem *>("playerDetails"); QVERIFY(details);
        auto *trackTitle = object->findChild<QQuickItem *>("playerTrackTitle"); QVERIFY(trackTitle);
        auto *playerCover = object->findChild<QQuickItem *>("playerCover"); QVERIFY(playerCover);
        for (const auto size : {QSize(320, 640), QSize(412, 860)}) {
            window->resize(size);
            QTest::qWait(100);
            QVERIFY(trackTitle->height() <= 58);
            QVERIFY(trackTitle->height() > 0);
            QVERIFY(trackTitle->property("baselineOffset").toReal() > 0);
            QVERIFY(trackTitle->property("baselineOffset").toReal() < 35);
            const auto detailsRect = details->mapRectToScene(details->boundingRect());
            QVERIFY2(detailsRect.bottom() <= window->height() - 15, "Player controls extend below the safe bottom padding");
            const auto coverRect = playerCover->mapRectToScene(playerCover->boundingRect());
            QVERIFY(coverRect.bottom() <= detailsRect.top());
        }
        window->resize(412, 860); QTest::qWait(100);
        const QPoint playerStart(200, 100);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, playerStart);
        for (int y = 10; y <= 50; y += 10) QTest::mouseMove(window, playerStart + QPoint(0, y), 5);
        QTest::qWait(150); // A held short pull must return, without stale inertia.
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, playerStart + QPoint(0, 50));
        QQmlExpression dragOffset(QQmlEngine::contextForObject(object.data()), object.data(), "player.dragOffset");
        QTRY_COMPARE(dragOffset.evaluate().toReal(), 0.0);
        QVERIFY(playerVisible.evaluate().toBool());
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, playerStart);
        for (int y = 10; y <= 130; y += 10) QTest::mouseMove(window, playerStart + QPoint(0, y), 5);
        QTest::qWait(150); // Deliberately release slowly: distance alone closes the sheet.
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, playerStart + QPoint(0, 130));
        QTRY_VERIFY_WITH_TIMEOUT(!playerVisible.evaluate().toBool(), 1500);
        QQmlExpression navigate(QQmlEngine::contextForObject(object.data()), object.data(), "page = 1; page = 2; goBack(); page");
        QCOMPARE(navigate.evaluate().toInt(), 1);
        QVERIFY2(!navigate.hasError(), qPrintable(navigate.error().toString()));
        QQmlExpression openPlayer(QQmlEngine::contextForObject(object.data()), object.data(), "player.open()");
        openPlayer.evaluate();
        QTRY_VERIFY(playerVisible.evaluate().toBool());
        QTest::qWait(500);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!playerVisible.evaluate().toBool(), 1500);
    }
    void lifecycle() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        // Тест регистрируется с отдельным каталогом данных через CTest.
        QCoreApplication::setApplicationName("SCrip-plugin-tests-" + QFileInfo(data.path()).fileName());
        const auto sample = QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/examples/plugins/studio.json");
        Plugins host;
        QFile pluginFile(sample.toLocalFile());
        QVERIFY(pluginFile.open(QIODevice::ReadOnly));
        const auto plugin = QJsonDocument::fromJson(pluginFile.readAll()).object();
        QCOMPARE(add(host, plugin), QString());
        QVERIFY(host.theme().isEmpty());
        QVERIFY(!add(host, plugin).isEmpty());
        QCOMPARE(host.enable("studio", true), QString());
        QCOMPARE(host.theme()["accent"].toString(), QString("#614899"));
        QCOMPARE(host.setSetting("studio", "legacy-group-0", false), QString());
        Plugins restored;
        QVERIFY(!restored.theme().contains("accent"));
        QCOMPARE(restored.theme()["covers"].toString(), QString("artist"));
        QCOMPARE(restored.remove("studio"), QString());
        QVERIFY(restored.panels().isEmpty());
        Plugins empty;
        QVERIFY(empty.installed().isEmpty());
    }
    void playerSnapshotExcludesPrivateData() {
        Plugins host;
        host.updatePlayer({{"signedIn", true}, {"access_token", "private-token"},
            {"profile", QVariantMap{{"email", "private-email"}}},
            {"current", QVariantMap{{"urn", "soundcloud:tracks:1"}, {"title", "Трек"},
                {"stream_url", "private-url"}, {"waveform_url", "https://wave.sndcdn.com/a.png?private-token=secret"}, {"user", QVariantMap{{"username", "Автор"}, {"email", "private-email"}}}}}});
        const auto bytes = QJsonDocument::fromVariant(host.m_player).toJson();
        QVERIFY(!bytes.contains("private-"));
        QCOMPARE(host.m_player["current"].toMap()["title"].toString(), QString("Трек"));
        for (const auto &permission : {"oauth", "secrets", "filesystem", "network"}) {
            auto plugin = basic(); plugin["permissions"] = QJsonArray{permission};
            QVERIFY(!PluginManifest::validate(plugin).isEmpty());
        }
    }
    void rejectsUnexpectedCapabilities() {
        QTemporaryDir dir;
        QFile file(dir.filePath("plugin.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(
            R"({"api":1,"id":"test","name":"Test","groups":[{"label":"Test","page":"home","buttons":[{"label":"Open","action":"openExternal"}]}]})");
        file.close();
        Plugins host;
        QSignalSpy spy(&host, &Plugins::installPrepared);
        QVERIFY(host.prepareInstall(QUrl::fromLocalFile(file.fileName()))["error"].toString().isEmpty());
        QVERIFY(spy.wait(3000));
        QVERIFY(!spy.first()[0].toMap()["error"].toString().isEmpty());
        QVERIFY(host.prepareInstall(QUrl("https://example.org/plugin.json"))["error"].toString().isEmpty());
        QVERIFY(host.installed().isEmpty());
    }
};
QTEST_MAIN(PluginTests)
#include "tst_plugins.moc"
