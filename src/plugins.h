#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <functional>

class Plugins : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList installed READ installed NOTIFY changed)
    Q_PROPERTY(QVariantMap theme READ theme NOTIFY changed)
    Q_PROPERTY(QVariantList panels READ panels NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
  public:
    explicit Plugins(QObject *parent = nullptr);
    QVariantList installed() const;
    QVariantMap theme() const;
    QVariantList panels() const;
    QString error() const { return m_error; }
    // Синхронная совместимость v1. UI использует prepareInstall/commitInstall.
    Q_INVOKABLE QString install(const QUrl &file);
    Q_INVOKABLE QVariantMap prepareInstall(const QUrl &file);
    Q_INVOKABLE QString commitInstall(const QString &operationId, const QStringList &acceptedPermissions);
    Q_INVOKABLE void cancelInstall(const QString &operationId);
    Q_INVOKABLE QString enable(const QString &id, bool value);
    Q_INVOKABLE QString configure(const QString &id, int group, bool value);
    Q_INVOKABLE QString setPermission(const QString &id, const QString &permission, bool value);
    Q_INVOKABLE QString setSetting(const QString &id, const QString &key, const QVariant &value);
    Q_INVOKABLE QString remove(const QString &id, bool keepData = false);
    Q_INVOKABLE QStringList dependents(const QString &id) const;
    Q_INVOKABLE QString clearData(const QString &id);
    Q_INVOKABLE QString invokeButton(const QString &id, const QString &contributionId, int button);
    Q_INVOKABLE void clearError(const QString &id);
    // Только C++-адаптер. Плагины не получают SoundCloud или произвольный QObject.
    using PlayerCommand = std::function<QString(const QString &, const QJsonObject &)>;
    void setPlayerCommand(PlayerCommand handler) { m_playerCommand = std::move(handler); }
    void updatePlayer(const QVariantMap &state);
  signals:
    void changed();
    void progressSnapshotRequested();
    void installPrepared(const QVariantMap &preview);
    void navigationRequested(const QString &destination);
    void commandFinished(const QVariantMap &result);

  private:
    friend class PluginTests;
    struct Prepared {
        QJsonObject document;
        quint64 revision;
        qint64 expires;
        QString sha256;
    };
    struct Event {
        QString id, name;
        QJsonObject payload;
        quint64 generation;
        int depth;
        int offset = 0;
    };
    struct Timer {
        QTimer *object;
        quint64 generation;
        qint64 deadline;
    };
    struct Budget {
        qint64 start = 0;
        int count = 0;
    };
    QVariantList entries;
    QHash<QString, Prepared> m_prepared;
    QSet<QString> m_pending;
    QHash<QString, quint64> m_generation;
    QHash<QString, QString> m_errors;
    QHash<QString, QStringList> m_logs;
    QHash<QString, QHash<QString, Timer>> m_timers;
    QHash<QString, Budget> m_budgets;
    QHash<QString, QPair<int, qint64>> m_causes;
    Budget m_globalBudget;
    QQueue<Event> m_events;
    QVariantMap m_player;
    PlayerCommand m_playerCommand;
    QElapsedTimer m_clock;
    QTimer m_progressTimer;
    quint64 m_revision = 0;
    int m_depth = 0;
    bool m_drainPending = false;
    QString m_error;
    QByteArray m_diskHash;
    int indexOf(const QString &id) const;
    QString save(const QVariantList &next);
    QString reason(const QString &id, QSet<QString> &visiting) const;
    QString reason(const QString &id) const;
    void stop(const QString &id);
    void reconcile();
    void recordError(const QString &id, const QString &message, bool fatal = false);
    bool permitted(const QString &id, const QString &permission) const;
    void enqueueEvent(const QString &name, const QJsonObject &payload);
    void drain();
    QString execute(const QString &id, const QJsonObject &action, bool button, int depth);
    QString candidate(const QJsonObject &manifest, QVariantList *next, QStringList *changes) const;
    bool dataValid(const QVariantMap &row) const;
};
