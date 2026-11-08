#include "pluginmanifest.h"
#include "plugins.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

// Модель без сети для проверки геометрии настоящего Android Main.qml.
class AndroidLayoutModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY signedInChanged)
    Q_PROPERTY(QVariantMap catalogState READ state CONSTANT)
    Q_PROPERTY(qint64 position READ position CONSTANT)
    Q_PROPERTY(qint64 duration READ position CONSTANT)
public:
    bool signedIn() const { return true; }
    qint64 position() const { return 0; }
    QVariantMap state() const {
        return {{"home", QVariantList{QVariantMap{{"title", "Микс любимых"},
            {"tracks", QVariantList{QVariantMap{{"title", "Проверка отступов"}}}}}}},
            {"profile", QVariantMap{}}, {"current", QVariantMap{}}};
    }
    Q_INVOKABLE void volume(double) {}
    Q_INVOKABLE void loadHome() {}
    Q_INVOKABLE void browse(const QString &, const QString & = {}) {}
signals:
    void signedInChanged();
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
    void v2PermissionsCommandsAndSettings() {
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
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins-v2.json";
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkdir(path));
        QVERIFY(!host.enable("test", false).isEmpty());
        QVERIFY(host.installed()[0].toMap()["enabled"].toBool());
        QDir(path).removeRecursively();
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
        QFile file(dir + "/plugins-v2.json");
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
        QFile sample(QString(SCRIP_SOURCE_DIR) + "/examples/plugins/studio.json");
        QVERIFY(sample.open(QIODevice::ReadOnly));
        const auto p = QJsonDocument::fromJson(sample.readAll()).object();
        QJsonArray flags;
        for (auto g : p["groups"].toArray()) {
            Q_UNUSED(g);
            flags.append(true);
        }
        flags[0] = false;
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
        Plugins concurrent;
        QCOMPARE(migrated.configure("studio", 0, true), QString());
        QVERIFY(!concurrent.enable("studio", false).isEmpty());
        QVERIFY(concurrent.installed()[0].toMap()["enabled"].toBool());
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
        for (auto name : {"PluginAction.qml", "PluginPanels.qml", "PluginManagerView.qml", "PluginToolsPopup.qml"}) {
            QFile::remove(fixture + "/" + name);
            QVERIFY(QFile::copy(QString(SCRIP_SOURCE_DIR) + "/qml/" + name, fixture + "/" + name));
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
        for (const auto size : {QSize(412,860), QSize(320,640), QSize(860,412)}) {
            window->resize(size);
            QTest::qWait(40);
            QVERIFY2(header->height() >= 66, "Шапка потеряла высоту");
            const auto headerBottom = header->mapToScene(QPointF(0, header->height())).y();
            const auto bodyTop = body->mapToScene(QPointF(0, 0)).y();
            QVERIFY2(bodyTop >= headerBottom - 0.5, qPrintable(QString("Содержимое %1 перекрывает шапку %2").arg(bodyTop).arg(headerBottom)));
        }
        QTest::qWait(950);
        window->resize(412,860); QTest::qWait(40);
        QVERIFY(window->grabWindow().save("android-header.png"));
    }
    void lifecycle() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        // Тест регистрируется с отдельным каталогом данных через CTest.
        QCoreApplication::setApplicationName("SCrip-plugin-tests-" + QFileInfo(data.path()).fileName());
        const auto sample = QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/examples/plugins/studio.json");
        Plugins host;
        QCOMPARE(host.install(sample), QString());
        QVERIFY(host.theme().isEmpty());
        QVERIFY(!host.install(sample).isEmpty());
        QCOMPARE(host.enable("studio", true), QString());
        QCOMPARE(host.theme()["accent"].toString(), QString("#614899"));
        QCOMPARE(host.configure("studio", 0, false), QString());
        Plugins restored;
        QVERIFY(!restored.theme().contains("accent"));
        QCOMPARE(restored.theme()["covers"].toString(), QString("artist"));
        QCOMPARE(restored.remove("studio"), QString());
        QVERIFY(restored.panels().isEmpty());
        Plugins empty;
        QVERIFY(empty.installed().isEmpty());
    }
    void rejectsUnexpectedCapabilities() {
        QTemporaryDir dir;
        QFile file(dir.filePath("plugin.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(
            R"({"api":1,"id":"test","name":"Test","groups":[{"label":"Test","page":"home","buttons":[{"label":"Open","action":"openExternal"}]}]})");
        file.close();
        Plugins host;
        QVERIFY(!host.install(QUrl::fromLocalFile(file.fileName())).isEmpty());
        QVERIFY(!host.install(QUrl("https://example.org/plugin.json")).isEmpty());
        QVERIFY(host.installed().isEmpty());
    }
};
QTEST_MAIN(PluginTests)
#include "tst_plugins.moc"
