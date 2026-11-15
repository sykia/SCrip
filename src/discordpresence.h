#pragma once

#include <QObject>
#include <QLocalSocket>
#include <QTimer>
#include <QJsonObject>
#include <QVariantMap>

// Discord's local RPC transport. No Discord account token is required or stored.
class DiscordPresence : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString applicationId READ applicationId WRITE setApplicationId NOTIFY settingsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    explicit DiscordPresence(QObject *parent = nullptr);
    ~DiscordPresence() override;
    static bool available();
    bool enabled() const { return m_enabled; }
    QString applicationId() const { return m_applicationId; }
    QString status() const { return m_status; }
    void setEnabled(bool enabled);
    void setApplicationId(const QString &id);
    void updatePlayer(const QVariantMap &state);
    static QJsonValue activity(const QVariantMap &state, qint64 nowSeconds);
signals:
    void settingsChanged();
    void statusChanged();
private:
    void restart();
    void connectNext();
    void readFrames();
    void publish();
    void send(quint32 opcode, const QByteArray &payload);
    void setStatus(const QString &status);
    void close();
    bool configured() const;
    QLocalSocket m_socket;
    QTimer m_retry, m_timeout, m_publish;
    QStringList m_endpoints;
    qsizetype m_endpoint = 0;
    QByteArray m_input;
    QVariantMap m_player;
    QJsonValue m_lastActivity;
    bool m_enabled = false;
    bool m_ready = false;
    bool m_sent = false;
    bool m_rejected = false;
    QString m_applicationId, m_status, m_lastTrackKey;
};
