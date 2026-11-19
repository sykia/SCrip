#pragma once
#include <QObject>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

class Diagnostics final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    explicit Diagnostics(QObject *parent = nullptr);
    ~Diagnostics() override;
    QString status() const { return m_status; }
    Q_INVOKABLE void copyReport();
    Q_INVOKABLE void openIssue();
    QString report() const;
    static QString sanitize(QString text);
    static void record(const QString &message);
signals:
    void statusChanged();
private:
    void setStatus(const QString &status);
    QString m_status;
    QString m_marker;
    bool m_storageReady = false;
    QTimer m_heartbeat;
    std::atomic<qint64> m_lastBeat{0};
    std::atomic<bool> m_active{false};
    std::mutex m_waitMutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    std::thread m_watchdog;
};
