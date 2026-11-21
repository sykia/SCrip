#include "dnsroute.h"
#include <QtTest>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QSettings>

class DnsRouteTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("SangeTests");
        QCoreApplication::setApplicationName("DnsRoute");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    }
    void consentAndScopedRouting() {
        QSettings().clear();
        DnsRoute route;
        QVERIFY(!route.enabled());
        route.setEnabled(true);
        QVERIFY(route.enabled());
        const auto proxy = QNetworkProxyFactory::proxyForQuery(QNetworkProxyQuery(QUrl("https://api.soundcloud.com/me"))).first();
        QCOMPARE(proxy.type(), QNetworkProxy::HttpProxy);
        QCOMPARE(proxy.hostName(), "127.0.0.1");
        QVERIFY(proxy.port() > 0);
        QCOMPARE(QNetworkProxyFactory::proxyForQuery(QNetworkProxyQuery(QUrl("http://127.0.0.1:12345/segment"))).first().type(), QNetworkProxy::NoProxy);
        QVERIFY(!DnsRoute::allowsHost("soundcloud.com.attacker.example"));
        QVERIFY(!DnsRoute::allowsHost("evil-sndcdn.com"));
        QVERIFY(DnsRoute::allowsHost("cf-hls-media.sndcdn.com"));
        QVERIFY(DnsRoute::allowsHost("API.SoundCloud.com"));
        route.setEnabled(false);
        QVERIFY(!QSettings().value("network/secureDns").toBool());
    }
    void rejectsArbitraryTunnels_data() {
        QTest::addColumn<QByteArray>("request");
        QTest::newRow("lan") << QByteArray("CONNECT 192.168.1.1:443 HTTP/1.1\r\n\r\n");
        QTest::newRow("other-host") << QByteArray("CONNECT example.org:443 HTTP/1.1\r\n\r\n");
        QTest::newRow("other-port") << QByteArray("CONNECT api.soundcloud.com:22 HTTP/1.1\r\n\r\n");
        QTest::newRow("credentials") << QByteArray("CONNECT x@api.soundcloud.com:443 HTTP/1.1\r\n\r\n");
        QTest::newRow("oversize") << QByteArray(9000, 'x');
    }
    void rejectsArbitraryTunnels() {
        QFETCH(QByteArray, request);
        DnsRoute route; route.setEnabled(true);
        const auto proxy = QNetworkProxyFactory::proxyForQuery(QNetworkProxyQuery(QUrl("https://api.soundcloud.com/"))).first();
        QTcpSocket client; client.setProxy(QNetworkProxy::NoProxy);
        client.connectToHost(QHostAddress::LocalHost, proxy.port());
        QVERIFY(client.waitForConnected(2000));
        client.write(request);
        QVERIFY(client.waitForReadyRead(2000));
        QVERIFY(client.readAll().startsWith("HTTP/1.1 502"));
        route.setEnabled(false);
    }
    void disablingClosesOpenConnections() {
        DnsRoute route; route.setEnabled(true);
        const auto proxy = QNetworkProxyFactory::proxyForQuery(QNetworkProxyQuery(QUrl("https://api.soundcloud.com/"))).first();
        QTcpSocket client; client.setProxy(QNetworkProxy::NoProxy);
        client.connectToHost(QHostAddress::LocalHost, proxy.port());
        QVERIFY(client.waitForConnected(2000));
        client.write("CONNECT api.soundcloud.com:443 HTTP/1.1\r\n");
        QVERIFY(client.waitForBytesWritten(2000));
        route.setEnabled(false);
        QTRY_COMPARE(client.state(), QAbstractSocket::UnconnectedState);
    }
    void liveTlsTunnel() {
        if (!qEnvironmentVariableIsSet("SANGE_DNS_LIVE")) QSKIP("Explicit network opt-in required");
        DnsRoute route; route.setEnabled(true);
        QNetworkAccessManager network;
        const auto proxy = QNetworkProxyFactory::proxyForQuery(QNetworkProxyQuery(QUrl("https://api.soundcloud.com/"))).first();
        QCOMPARE(proxy.type(), QNetworkProxy::HttpProxy);
        network.setProxy(proxy);
        QNetworkRequest request(QUrl("https://api.soundcloud.com/"));
        request.setTransferTimeout(15000);
        auto *reply = network.get(request);
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(20000));
        // A signed-out API response is enough to prove end-to-end verified TLS.
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QVERIFY2(status > 0, qPrintable(reply->errorString()));
        qInfo() << "Verified TLS through the DNS tunnel; HTTP status:" << status;
        reply->deleteLater();
        route.setEnabled(false);
    }
private:
    QTemporaryDir settings;
};
QTEST_GUILESS_MAIN(DnsRouteTest)
#include "tst_dnsroute.moc"
