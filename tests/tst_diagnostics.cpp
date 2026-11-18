#include "diagnostics.h"
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QtTest>

class DiagnosticsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("SangeTests");
        QCoreApplication::setApplicationName("Diagnostics");
        QCoreApplication::setApplicationVersion("test");
        QStandardPaths::setTestModeEnabled(true);
        QDir(directory()).removeRecursively();
    }
    void filtersSensitiveData() {
        const auto safe = Diagnostics::sanitize(
            "https://cdn.example/private/token?secret=hidden Bearer abc123 OAuth xyz "
            "access_token=private refresh_token: hidden \"password\":\"two words\" "
            "Cookie=session-secret user@example.com " + QDir::homePath() + "/music\nforged");
        for (const auto &secret : {"cdn.example", "abc123", "xyz", "private", "hidden", "two words", "session-secret", "user@example.com"})
            QVERIFY2(!safe.contains(secret), qPrintable(safe));
        QVERIFY(!safe.contains('\n'));
        QVERIFY(safe.contains("[home]"));
    }
    void persistenceRotationAndClipboard() {
        QDir().mkpath(directory());
        QFile marker(directory() + "/session.active");
        QVERIFY(marker.open(QIODevice::WriteOnly)); marker.write("active"); marker.close();
        {
            Diagnostics diagnostics;
            qWarning("diagnostic warning https://example.com/signed-secret");
            auto report = diagnostics.report();
            QVERIFY(report.contains("Previous session did not shut down cleanly"));
            QVERIFY(report.contains("diagnostic warning [url]"));
            QVERIFY(!report.contains("signed-secret"));
            diagnostics.copyReport();
            QVERIFY(QGuiApplication::clipboard()->text().contains("diagnostic warning [url]"));
            QVERIFY(!QGuiApplication::clipboard()->text().contains("signed-secret"));
            for (int i = 0; i < 400; ++i) Diagnostics::record(QString(4096, 'x'));
            Diagnostics::record("latest-event");
            QVERIFY(diagnostics.report().contains("latest-event"));
            const auto files = QDir(directory()).entryList({"*.log"}, QDir::Files);
            QCOMPARE(files.size(), 3);
            for (const auto &name : files) QVERIFY(QFile(directory() + "/" + name).size() < 520 * 1024);
            QVERIFY(QFile::exists(directory() + "/session.active"));
        }
        QVERIFY(!QFile::exists(directory() + "/session.active"));
        {
            Diagnostics diagnostics;
            QVERIFY(diagnostics.report().contains("Session ended cleanly"));
        }
    }
    void watchdogDetectsBlockedLoop() {
        Diagnostics diagnostics;
        QVERIFY(QMetaObject::invokeMethod(qGuiApp, "applicationStateChanged", Qt::DirectConnection,
                                         Q_ARG(Qt::ApplicationState, Qt::ApplicationActive)));
        std::this_thread::sleep_for(std::chrono::milliseconds(6200));
        QVERIFY(diagnostics.report().contains("Main event loop unresponsive"));
        QTRY_VERIFY_WITH_TIMEOUT(diagnostics.report().contains("Main event loop recovered"), 3500);
    }
    void panelLayoutAndCopy() {
        Diagnostics diagnostics;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("diagnostics", &diagnostics);
        QByteArray source =
            "import QtQuick\n"
            "import QtQuick.Controls.Basic\n"
            "import \"@QML@\" as App\n"
            "Window {\n"
            "    id: window\n"
            "    visible: true; width: 600; height: 380; color: \"#fafafa\"\n"
            "    property color ink: \"#202020\"\n"
            "    property color muted: \"#666666\"\n"
            "    property color raised: \"#eeeeee\"\n"
            "    property color accent: \"#3d8c68\"\n"
            "    property bool dark: false\n"
            "    property int motion: 0\n"
            "    App.DiagnosticsPanel { objectName: \"panel\"; host: window; x: 18; y: 20; width: parent.width - 36; mobile: window.width < 400 }\n"
            "}\n";
        source.replace("@QML@", QUrl::fromLocalFile(QString(SCRIP_SOURCE_DIR) + "/qml").toString().toUtf8());
        engine.loadData(source);
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        for (int width : {600, 320}) {
            window->setWidth(width); QTest::qWait(80);
            auto *copy = window->findChild<QQuickItem *>("copyDiagnosticLog");
            auto *issue = window->findChild<QQuickItem *>("openDiagnosticIssue");
            QVERIFY(copy && issue);
            for (auto *button : {copy, issue}) {
                const auto rect = button->mapRectToScene(button->boundingRect());
                QVERIFY(rect.left() >= 18 && rect.right() <= width - 18);
                if (width == 320) QVERIFY(button->height() >= 44);
            }
            QVERIFY(QMetaObject::invokeMethod(copy, "clicked"));
            QVERIFY(QGuiApplication::clipboard()->text().contains("Sange test"));
            QTest::qWait(50);
            auto *panel = window->findChild<QQuickItem *>("panel"); QVERIFY(panel);
            QVERIFY(panel->y() + panel->height() <= window->height());
            QVERIFY(window->grabWindow().save(QString("diagnostics-panel-%1.png").arg(width)));
        }
    }
    void cleanupTestCase() { QDir(directory()).removeRecursively(); }
private:
    QString directory() const { return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs"; }
};
QTEST_MAIN(DiagnosticsTest)
#include "tst_diagnostics.moc"
