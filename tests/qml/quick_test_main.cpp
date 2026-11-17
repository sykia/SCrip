#include <QtQuickTest/quicktest.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QDir>

class QuickSetup : public QObject {
    Q_OBJECT
    QTemporaryDir settings{QDir::currentPath() + "/qml-settings-XXXXXX"};
public slots:
    void applicationAvailable() {
        QCoreApplication::setOrganizationName("SCripTests");
        QCoreApplication::setApplicationName("QmlFixtures");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    }
};
QUICK_TEST_MAIN_WITH_SETUP(artwork, QuickSetup)
#include "quick_test_main.moc"
