#pragma once
#include <QObject>
#include <QThread>
#include <atomic>
#include <memory>

// The tunnel changes DNS resolution only. TLS remains end-to-end with SoundCloud.
class DnsRoute : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit DnsRoute(QObject *parent = nullptr);
    ~DnsRoute() override;
    bool enabled() const { return m_enabled; }
    QString error() const { return m_error; }
    Q_INVOKABLE void setEnabled(bool enabled);
    static bool allowsHost(const QString &host);
signals:
    void changed();
private:
    QThread m_thread;
    QObject *m_worker = nullptr;
    std::shared_ptr<std::atomic<quint16>> m_port;
    quint16 m_listenPort = 0;
    bool m_enabled = false;
    QString m_error;
};
