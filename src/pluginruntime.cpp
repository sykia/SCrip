#include "pluginruntime.h"
#include "plugins.h"
#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <utility>

PluginSdk::PluginSdk(QString id, QQuickWindow *window, QObject *player, Plugins *plugins, QObject *parent)
    : QObject(parent), m_id(std::move(id)), m_window(window), m_player(player), m_plugins(plugins) {
    connect(plugins, &Plugins::changed, this, &PluginSdk::settingsChanged);
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}
QObject *PluginSdk::window() const { return m_window; }
QObject *PluginSdk::plugins() const { return m_plugins; }
QVariantMap PluginSdk::settings() const {
    for (const auto &entry : m_plugins->installed()) {
        const auto row = entry.toMap();
        if (row["plugin"].toMap()["id"].toString() == m_id)
            return row["settings"].toMap();
    }
    return {};
}
QObject *PluginSdk::find(const QString &name) const {
    if (!m_window || name.isEmpty()) return nullptr;
    if (m_window->objectName() == name) return m_window;
    // Include visual children which may have a different QObject owner.
    std::function<QObject *(QQuickItem *)> visit = [&](QQuickItem *item) -> QObject * {
        if (item->objectName() == name) return item;
        for (auto *child : item->childItems())
            if (auto *found = visit(child)) return found;
        return nullptr;
    };
    if (auto *found = visit(m_window->contentItem())) return found;
    return m_window->findChild<QObject *>(name);
}
QString PluginSdk::setSetting(const QString &key, const QVariant &value) {
    return m_plugins->setSetting(m_id, key, value);
}
PluginRuntime::PluginRuntime(QQmlEngine *engine, QQuickWindow *window, QObject *player, Plugins *plugins)
    : m_engine(engine), m_window(window), m_player(player), m_plugins(plugins) {
    connect(plugins, &Plugins::changed, this, &PluginRuntime::sync, Qt::QueuedConnection);
    connect(plugins, &Plugins::runtimeReloadRequested, this, [this](const QString &id) {
        for (const auto &key : m_instances.keys())
            if (m_instances.value(key).pluginId == id) unload(key);
        sync();
    }, Qt::QueuedConnection);
    QTimer::singleShot(0, this, &PluginRuntime::sync);
}
PluginRuntime::~PluginRuntime() {
    for (const auto &key : m_instances.keys()) unload(key);
}
void PluginRuntime::unload(const QString &key) {
    const auto instance = m_instances.take(key);
    // Destroy QML first: Component.onDestruction can still use its SDK.
    if (instance.sdk) instance.sdk->deactivate();
    delete instance.item.data();
    delete instance.sdk;
}
void PluginRuntime::sync() {
    if (!m_window || QCoreApplication::arguments().contains("--no-plugin-code") || qEnvironmentVariableIsSet("SCRIP_NO_PLUGIN_CODE")) return;
    QHash<QString, QVariantMap> desired;
    for (const auto &value : m_plugins->runtimes()) {
        const auto spec = value.toMap();
        desired.insert(spec["pluginId"].toString() + "/" + spec["id"].toString(), spec);
    }
    for (const auto &key : m_instances.keys())
        if (!desired.contains(key) || desired[key]["source"].toString() != m_instances[key].source)
            unload(key);
    for (auto it = desired.cbegin(); it != desired.cend(); ++it) {
        if (m_instances.contains(it.key())) continue;
        const auto id = it.value()["pluginId"].toString();
        auto *sdk = new PluginSdk(id, m_window, m_player, m_plugins, this);
        auto *component = new QQmlComponent(m_engine, sdk);
        m_instances.insert(it.key(), {id, it.value()["source"].toString(), sdk, nullptr});
        const auto key = it.key();
        auto finish = [this, component, sdk, key, id] {
            if (component->isLoading()) return;
            if (component->isError()) {
                m_plugins->runtimeError(id, component->errorString());
                return;
            }
            auto *object = component->beginCreate(m_engine->rootContext());
            auto *item = qobject_cast<QQuickItem *>(object);
            if (item) {
                component->setInitialProperties(item, {{"sdk", QVariant::fromValue(sdk)}});
                item->setParent(sdk);
                item->setParentItem(m_window->contentItem());
            }
            component->completeCreate();
            if (!item || component->isError()) {
                sdk->deactivate();
                delete object;
                m_plugins->runtimeError(id, component->isError() ? component->errorString() : "Корень QML-плагина должен быть Item.");
                return;
            }
            m_instances[key].item = item;
        };
        component->setData(it.value()["source"].toString().toUtf8(), QUrl("scrip-plugin:/" + key + ".qml"));
        if (component->isLoading())
            connect(component, &QQmlComponent::statusChanged, sdk, finish, Qt::SingleShotConnection);
        else finish();
    }
}
