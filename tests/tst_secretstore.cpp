#include "secretstore.h"
#include <QtTest>
#include <QUuid>

class SecretStoreTest : public QObject {
    Q_OBJECT
private slots:
    void roundTrip() {
        // Отдельная запись с уникальным именем, без пользовательских секретов.
        auto store = systemSecretStore();
        const auto key = "scrip-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QByteArray value;
        QVERIFY(store->read(key, value)); QVERIFY(value.isEmpty());
        const bool written = store->write(key, "fixture-secret");
        if (!written) { store->remove(key); QFAIL("Системное хранилище недоступно для записи"); }
        const bool read = store->read(key, value);
        const bool removed = store->remove(key);
        QVERIFY(read); QCOMPARE(value, QByteArray("fixture-secret")); QVERIFY(removed);
        QVERIFY(store->read(key, value)); QVERIFY(value.isEmpty());
        QVERIFY(store->remove(key));
    }
    void unavailable() {
        auto store = systemSecretStore();
        QByteArray value;
        QVERIFY(!store->read("scrip-test-unavailable", value));
        QVERIFY(value.isEmpty());
        QVERIFY(!store->write("scrip-test-unavailable", "fixture-secret"));
        QVERIFY(!store->remove("scrip-test-unavailable"));
    }
};
QTEST_GUILESS_MAIN(SecretStoreTest)
#include "tst_secretstore.moc"
