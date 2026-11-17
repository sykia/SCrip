#include "pluginruntime.h"
#include "plugins.h"
#include "playercommand.h"
#include "discordpresence.h"
#include "lyrics.h"
#include "soundcloud.h"
#ifndef Q_OS_ANDROID
#include "desktopmedia.h"
#endif
#include "linkrouter.h"
#include <QLocalServer>
#include <QLockFile>
#include <QDir>
#include <QLocalSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QTimer>
#include "artworknetwork.h"
#include <QQuickWindow>
#include <QQmlContext>
#include <cstdlib>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <QScreen>
#include <QSGRendererInterface>
#include <QProcess>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>

#ifdef Q_OS_ANDROID
#include <QGuiApplication>
#include "androidmedia.h"
#else
#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QEvent>

class TrayWindowFilter : public QObject {
public:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Close && QSystemTrayIcon::isSystemTrayAvailable()) {
            static_cast<QWindow *>(object)->hide();
            event->ignore();
            return true;
        }
        return false;
    }
};
#endif
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
#ifdef Q_OS_ANDROID
    qputenv("QT_BLOCK_EVENT_LOOPS_WHEN_SUSPENDED", "0");
    QGuiApplication app(argc, argv);
#else
    // Preserve transparent pixels outside the rounded window surface.
    QQuickWindow::setDefaultAlphaBuffer(true);
    QApplication app(argc, argv);
#endif
    QCoreApplication::setOrganizationName("Qsae");
    // Keep the storage identity for upgrades from SCrip.
    QCoreApplication::setApplicationName("SCrip");
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Сендж"));
    QCoreApplication::setApplicationVersion(SCRIP_VERSION);
    QString incomingLink;
    for (const auto &argument : app.arguments().mid(1)) {
        const QUrl candidate(argument);
        if (candidate.scheme() == "https" || candidate.scheme() == "http" || candidate.scheme() == "scrip" || candidate.scheme() == "soundcloud") { incomingLink = argument; break; }
    }
#ifndef Q_OS_ANDROID
    const auto socketName = "SCrip-" + QString::fromLatin1(QCryptographicHash::hash(QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toUtf8(), QCryptographicHash::Sha256).toHex().left(20));
    QLocalSocket existing;
    existing.connectToServer(socketName);
    if (existing.waitForConnected(300)) {
        existing.write(QJsonDocument(QJsonArray{incomingLink}).toJson(QJsonDocument::Compact) + '\n');
        existing.waitForBytesWritten(1000);
        return EXIT_SUCCESS;
    }
    QLockFile instanceLock(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(socketName + ".lock"));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock()) {
        existing.abort(); existing.connectToServer(socketName);
        if (existing.waitForConnected(1000)) {
            existing.write(QJsonDocument(QJsonArray{incomingLink}).toJson(QJsonDocument::Compact) + '\n');
            existing.waitForBytesWritten(1000);
            return EXIT_SUCCESS;
        }
        return EXIT_FAILURE;
    }
    QLocalServer linkServer;
    linkServer.setSocketOptions(QLocalServer::UserAccessOption);
    if (!linkServer.listen(socketName)) {
        QLocalServer::removeServer(socketName);
        if (!linkServer.listen(socketName)) return EXIT_FAILURE;
    }
#endif
    SoundCloud soundcloud;
#ifndef Q_OS_ANDROID
    LinkRouter linkRouter(&soundcloud);
#endif
    QObject::connect(&soundcloud, &SoundCloud::authorizationRequested, &app, [&soundcloud](const QUrl &url) {
        bool opened = false;
#ifndef Q_OS_ANDROID
        // Отдельное окно настоящего браузера поддерживает вход Google и системную сессию.
        const auto firefox = QStandardPaths::findExecutable("firefox");
        if (!firefox.isEmpty())
            opened = QProcess::startDetached(firefox, {"--new-window", url.toString(QUrl::FullyEncoded)});
        else {
            for (const auto &name : {"google-chrome", "chromium", "msedge", "chrome"}) {
                const auto executable = QStandardPaths::findExecutable(QString::fromLatin1(name));
                if (!executable.isEmpty()) {
                    opened = QProcess::startDetached(executable, {"--new-window", url.toString(QUrl::FullyEncoded)});
                    if (opened) break;
                }
            }
        }
#endif
        if (!opened) opened = QDesktopServices::openUrl(url);
        if (!opened) soundcloud.browserFailed();
    });
    DiscordPresence discordPresence;
    auto updateDiscord = [&] { discordPresence.updatePlayer(soundcloud.state()); };
    QObject::connect(&soundcloud, &SoundCloud::catalogChanged, &discordPresence, updateDiscord);
    QObject::connect(&soundcloud, &SoundCloud::progressChanged, &discordPresence, updateDiscord);
    updateDiscord();
    Lyrics lyrics;
    Plugins plugins;
    plugins.setPlayerCommand(playerCommandHandler(soundcloud));
    plugins.updatePlayer(soundcloud.state());
    QObject::connect(&soundcloud, &SoundCloud::catalogChanged, &plugins, [&] { plugins.updatePlayer(soundcloud.state()); });
    // Получаем свежий прогресс только при наличии подписок, не на каждом тике backend.
    QObject::connect(&plugins, &Plugins::progressSnapshotRequested, &plugins, [&] { plugins.updatePlayer(soundcloud.state()); });
    ArtworkNetworkFactory artwork;
#ifdef Q_OS_ANDROID
    AndroidMedia androidMedia(&soundcloud, &artwork);
#endif
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&artwork);
    artwork.prepare(&app, [] {});
    engine.rootContext()->setContextProperty("sc", &soundcloud);
    engine.rootContext()->setContextProperty("discordPresence", &discordPresence);
    engine.rootContext()->setContextProperty("lyricsBackend", &lyrics);
#ifdef Q_OS_ANDROID
    engine.rootContext()->setContextProperty("androidMedia", &androidMedia);
#else
    engine.rootContext()->setContextProperty("androidMedia", QVariant());
#endif
    engine.rootContext()->setContextProperty("pluginManager", &plugins);
    engine.loadFromModule("SCrip", "Main");
    if (engine.rootObjects().isEmpty())
        return EXIT_FAILURE;
    if (auto *quickWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
        // Opt-in aggregate timing only: no account, track, URL, or input data.
        if (qEnvironmentVariableIntValue("SCRIP_PERF") == 1) {
            struct Timing {
                std::atomic<qint64> lastFrame{0}, frames{0}, frameMax{0}, slowFrames{0};
                qint64 lastTick = 0, tickMax = 0, slowTicks = 0;
            };
            auto timing = std::make_shared<Timing>();
            auto now = [] { return std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count(); };
            QObject::connect(quickWindow, &QQuickWindow::frameSwapped, quickWindow, [timing, now] {
                const qint64 stamp = now(), previous = timing->lastFrame.exchange(stamp);
                ++timing->frames;
                // Ignore gaps between independent updates of an idle window.
                const qint64 gap = stamp - previous;
                if (previous && gap < 250000) {
                    if (gap > 25000) ++timing->slowFrames;
                    auto maximum = timing->frameMax.load();
                    while (gap > maximum && !timing->frameMax.compare_exchange_weak(maximum, gap)) {}
                }
            }, Qt::DirectConnection);
            auto *tick = new QTimer(quickWindow);
            tick->setTimerType(Qt::PreciseTimer); tick->setInterval(16);
            timing->lastTick = now();
            QObject::connect(tick, &QTimer::timeout, quickWindow, [timing, now] {
                const qint64 stamp = now(), gap = stamp - timing->lastTick;
                timing->lastTick = stamp; timing->tickMax = qMax(timing->tickMax, qint64(gap));
                if (gap > 32000) ++timing->slowTicks;
            });
            tick->start();
            auto *report = new QTimer(quickWindow); report->setInterval(5000);
            QObject::connect(report, &QTimer::timeout, quickWindow, [timing, quickWindow] {
                const auto *screen = quickWindow->screen();
                std::fprintf(stderr, "SCripPerf ui_max_ms=%.1f ui_slow=%lld frames=%lld frame_max_ms=%.1f frame_gaps=%lld refresh_hz=%.1f graphics_api=%d\n",
                    timing->tickMax / 1000.0, static_cast<long long>(timing->slowTicks),
                    static_cast<long long>(timing->frames.exchange(0)), timing->frameMax.exchange(0) / 1000.0,
                    static_cast<long long>(timing->slowFrames.exchange(0)), screen ? screen->refreshRate() : 0,
                    int(quickWindow->rendererInterface()->graphicsApi()));
                std::fflush(stderr); timing->tickMax = timing->slowTicks = 0;
            });
            report->start();
        }
        // Recreate graphical resources on restore instead of retaining a hidden scene.
        quickWindow->setPersistentSceneGraph(false);
        quickWindow->setPersistentGraphics(false);
    }
    if (!incomingLink.isEmpty()) QTimer::singleShot(0, &soundcloud, [&soundcloud, incomingLink] { soundcloud.openLink(incomingLink); });
#ifndef Q_OS_ANDROID
    QObject::connect(&linkServer, &QLocalServer::newConnection, &app, [&] {
        while (auto *socket = linkServer.nextPendingConnection()) {
            socket->setParent(&linkServer);
            auto read = [socket, &soundcloud, &engine] {
                if (socket->bytesAvailable() > 16384) { socket->disconnectFromServer(); return; }
                if (!socket->canReadLine()) return;
                const auto array = QJsonDocument::fromJson(socket->readLine(16385)).array();
                if (array.size() != 1 || !array.first().isString()) { socket->disconnectFromServer(); return; }
                if (auto *window = qobject_cast<QWindow *>(engine.rootObjects().first())) {
                    window->showNormal(); window->raise(); window->requestActivate();
                }
                const auto link = array.first().toString();
                if (!link.isEmpty()) soundcloud.openLink(link);
                socket->disconnectFromServer();
            };
            QObject::connect(socket, &QLocalSocket::readyRead, &app, read);
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(5000, socket, [socket] { socket->disconnectFromServer(); });
            read();
        }
    });
#endif
    PluginRuntime pluginRuntime(&engine, qobject_cast<QQuickWindow *>(engine.rootObjects().first()), &soundcloud, &plugins);
#ifndef Q_OS_ANDROID
    auto *window = qobject_cast<QWindow *>(engine.rootObjects().first());
    DesktopMedia desktopMedia(&soundcloud, window);
    QIcon icon(":/qt/qml/SCrip/assets/blob.png");
    app.setWindowIcon(icon);
    QMenu menu;
    QSystemTrayIcon tray(icon);
    TrayWindowFilter filter;
    auto restore = [window] {
        if (window->visibility() == QWindow::Minimized) window->showNormal();
        else window->show();
        window->raise();
        window->requestActivate();
    };
    menu.addAction("Открыть Сендж", restore);
    menu.addSeparator();
    auto *play = menu.addAction("Воспроизвести", &soundcloud, &SoundCloud::togglePlayback);
    auto *previous = menu.addAction("Предыдущий трек", &soundcloud, &SoundCloud::previous);
    auto *next = menu.addAction("Следующий трек", &soundcloud, &SoundCloud::next);
    auto *like = menu.addAction("Поставить лайк", [&] {
        soundcloud.like(soundcloud.state().value("current").toMap());
    });
    menu.addSeparator();
    menu.addAction("Выход", &app, &QCoreApplication::quit);
    QObject::connect(&menu, &QMenu::aboutToShow, &app, [&] {
        const auto state = soundcloud.state();
        const auto current = state.value("current").toMap();
        play->setText(state.value("playing").toBool() ? "Пауза" : "Воспроизвести");
        play->setEnabled(!current.isEmpty());
        previous->setEnabled(!current.isEmpty());
        next->setEnabled(!state.value("queue").toList().isEmpty());
        like->setEnabled(soundcloud.signedIn() && !current.isEmpty());
        like->setText(current.value("user_favorite").toBool() ? "Снять лайк" : "Поставить лайк");
    });
    QObject::connect(&tray, &QSystemTrayIcon::activated, &app, [restore](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) restore();
    });
    tray.setContextMenu(&menu);
    tray.setToolTip("Сендж");
    tray.show();
    window->installEventFilter(&filter);
#endif
    return app.exec();
}
