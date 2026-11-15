#pragma once
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariant>

class Plugins;
class QQmlEngine;
class QQuickWindow;
class QQuickItem;

// Trusted in-process SDK. This is deliberately not a security boundary.
class PluginSdk : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QObject *window READ window CONSTANT)
    Q_PROPERTY(QObject *player READ player CONSTANT)
    Q_PROPERTY(QObject *plugins READ plugins CONSTANT)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
  public:
    PluginSdk(QString id, QQuickWindow *window, QObject *player, Plugins *plugins, QObject *parent);
    QString id() const { return m_id; }
    bool active() const { return m_active; }
    void deactivate() { m_active = false; emit activeChanged(); }
    QObject *window() const;
    QObject *player() const { return m_player; }
    QObject *plugins() const;
    QVariantMap settings() const;
    Q_INVOKABLE QObject *find(const QString &objectName) const;
    Q_INVOKABLE QString setSetting(const QString &key, const QVariant &value);
  signals:
    void settingsChanged();
    void activeChanged();
  private:
    bool m_active = true;
    QString m_id;
    QPointer<QQuickWindow> m_window;
    QPointer<QObject> m_player;
    Plugins *m_plugins;
};

class PluginRuntime : public QObject {
    Q_OBJECT
  public:
    PluginRuntime(QQmlEngine *engine, QQuickWindow *window, QObject *player, Plugins *plugins);
    ~PluginRuntime() override;
  private:
    struct Instance {
        QString pluginId, source;
        PluginSdk *sdk = nullptr;
        QPointer<QQuickItem> item;
    };
    QQmlEngine *m_engine;
    QPointer<QQuickWindow> m_window;
    QObject *m_player;
    Plugins *m_plugins;
    QHash<QString, Instance> m_instances;
    void sync();
    void unload(const QString &key);
};
