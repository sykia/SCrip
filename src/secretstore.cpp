#include "secretstore.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <wincrypt.h>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#elif defined(Q_OS_ANDROID)
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#else
// GLib использует идентификатор signals.
#undef signals
#include <libsecret/secret.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#endif

namespace {
#if !defined(Q_OS_WIN) && !defined(Q_OS_ANDROID)
// Отсутствующий/зависший Secret Service не должен блокировать запуск навсегда.
class SecretOperation {
    GCancellable *m_cancel = g_cancellable_new();
    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::jthread m_timer{[this](std::stop_token stop) {
        std::unique_lock lock(m_mutex);
        m_wake.wait_for(lock, stop, std::chrono::seconds(10), [] { return false; });
        if (!stop.stop_requested()) g_cancellable_cancel(m_cancel);
    }};
public:
    ~SecretOperation() { m_timer.request_stop(); m_timer.join(); g_object_unref(m_cancel); }
    GCancellable *get() const { return m_cancel; }
};
#endif
class PlatformStore final : public SecretStore {
public:
#if defined(Q_OS_WIN)
    QString path(const QString &key) const {
        return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/auth/" + key + ".dpapi";
    }
    bool read(const QString &key, QByteArray &value) override {
        value.clear(); QFile file(path(key));
        if (!file.exists()) return true;
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return false;
        auto bytes = file.readAll();
        DATA_BLOB input{DWORD(bytes.size()), reinterpret_cast<BYTE *>(bytes.data())}, output{};
        if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return false;
        value = QByteArray(reinterpret_cast<char *>(output.pbData), output.cbData);
        SecureZeroMemory(output.pbData, output.cbData); LocalFree(output.pbData); return true;
    }
    bool write(const QString &key, const QByteArray &value) override {
        DATA_BLOB input{DWORD(value.size()), reinterpret_cast<BYTE *>(const_cast<char *>(value.constData()))}, output{};
        if (!CryptProtectData(&input, L"SCrip OAuth", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return false;
        const auto name = path(key);
        bool ok = QDir().mkpath(QFileInfo(name).absolutePath());
        QSaveFile file(name);
        ok = ok && file.open(QIODevice::WriteOnly);
        if (ok) ok = file.write(reinterpret_cast<char *>(output.pbData), output.cbData) == output.cbData && file.commit();
        LocalFree(output.pbData); return ok;
    }
    bool remove(const QString &key) override { const auto name = path(key); return !QFileInfo::exists(name) || QFile::remove(name); }
#elif defined(Q_OS_ANDROID)
    QJniObject context() const { return QNativeInterface::QAndroidApplication::context(); }
    bool read(const QString &key, QByteArray &value) override {
        auto result = QJniObject::callStaticObjectMethod("org/scrip/app/SecretVault", "read",
            "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;", context().object(), QJniObject::fromString(key).object());
        if (!result.isValid()) return false;
        value = result.toString().toUtf8(); return true;
    }
    bool write(const QString &key, const QByteArray &value) override {
        return QJniObject::callStaticMethod<jboolean>("org/scrip/app/SecretVault", "write",
            "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z", context().object(),
            QJniObject::fromString(key).object(), QJniObject::fromString(QString::fromUtf8(value)).object());
    }
    bool remove(const QString &key) override {
        return QJniObject::callStaticMethod<jboolean>("org/scrip/app/SecretVault", "remove",
            "(Landroid/content/Context;Ljava/lang/String;)Z", context().object(), QJniObject::fromString(key).object());
    }
#else
    const SecretSchema *schema() const {
        static const SecretSchema s = {"org.scrip.OAuth", SECRET_SCHEMA_NONE, {{"record", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}};
        return &s;
    }
    bool read(const QString &key, QByteArray &value) override {
        value.clear(); GError *error = nullptr; SecretOperation operation;
        auto *password = secret_password_lookup_sync(schema(), operation.get(), &error, "record", key.toUtf8().constData(), nullptr);
        const bool ok = !error;
        if (password) { value = QByteArray(password); secret_password_free(password); }
        if (error) g_error_free(error); return ok;
    }
    bool write(const QString &key, const QByteArray &value) override {
        GError *error = nullptr; SecretOperation operation;
        const bool ok = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, "SCrip OAuth", value.constData(), operation.get(), &error,
            "record", key.toUtf8().constData(), nullptr);
        if (error) g_error_free(error); return ok;
    }
    bool remove(const QString &key) override {
        GError *error = nullptr; SecretOperation operation;
        secret_password_clear_sync(schema(), operation.get(), &error, "record", key.toUtf8().constData(), nullptr);
        const bool ok = !error; if (error) g_error_free(error); return ok;
    }
#endif
};
}
std::shared_ptr<SecretStore> systemSecretStore() { return std::make_shared<PlatformStore>(); }
QString AuthStorage::errorMessage() {
    QString help;
#if defined(Q_OS_WIN)
    help = QStringLiteral("Проверьте доступ к профилю Windows.");
#elif defined(Q_OS_ANDROID)
    help = QStringLiteral("Проверьте доступность Android Keystore и свободное место.");
#else
    help = QStringLiteral("Запустите и разблокируйте Secret Service (например, GNOME Keyring или совместимый KWallet).");
#endif
    return QStringLiteral("Защищённое хранилище авторизации недоступно или операция не завершена. ") + help
        + QStringLiteral(" Затем перезапустите SCrip. Токены не сохраняются открытым текстом; старые файлы сохраняются до подтверждения переноса.");
}
bool AuthStorage::save(const QString &key, const QJsonObject &value) {
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    QByteArray previous;
    if (!m_store->read(key, previous)) return false;
    const bool hadPrevious = !previous.isEmpty();
    QByteArray verified;
    if (m_store->write(key, bytes) && m_store->read(key, verified) && bytes == verified) {
        m_consistent = true;
        return true;
    }

    // A SecretStore has no atomic replace operation. Detect when best-effort
    // restoration also fails so callers can refuse to use this session.
    const bool restored = hadPrevious ? m_store->write(key, previous) : m_store->remove(key);
    QByteArray afterRestore;
    m_consistent = restored && m_store->read(key, afterRestore)
        && (hadPrevious ? afterRestore == previous : afterRestore.isEmpty());
    if (!m_consistent && key == QStringLiteral("session")) {
        // Best-effort persistent quarantine: if rollback is unavailable but a
        // later write succeeds, leave a token-free record for the next launch.
        const auto quarantine = QJsonDocument(QJsonObject{{"access_token", ""}, {"refresh_token", ""},
            {"expires_at", 0}, {"user_session", false}}).toJson(QJsonDocument::Compact);
        QByteArray quarantined;
        if (m_store->write(key, quarantine) && m_store->read(key, quarantined) && quarantine == quarantined)
            m_consistent = false;
    }
    return false;
}
bool AuthStorage::load(const QString &key, const QString &legacyPath, QJsonObject &value) {
    if (!m_consistent) return false;
    QByteArray bytes;
    if (!m_store->read(key, bytes)) return false;
    QFile legacy(legacyPath);
    const bool exists = legacy.exists();
    if (bytes.isEmpty() && exists) {
        // Не следуем ссылкам на чужие файлы; исходник остаётся при любой ошибке.
        if (!QFileInfo(legacy).isFile() || QFileInfo(legacy).isSymLink() || !legacy.open(QIODevice::ReadOnly) || legacy.size() > 1024 * 1024) return false;
        bytes = legacy.readAll(); if (legacy.error() != QFileDevice::NoError) return false;
        legacy.close();
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject() || !save(key, doc.object())) return false;
    }
    QJsonObject result;
    if (!bytes.isEmpty()) {
        QJsonParseError error; auto doc = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
        result = doc.object();
    }
    // При прерывании после записи системная запись имеет приоритет над старой.
    if (exists && !legacy.remove()) return false;
    value = result; return true;
}
bool AuthStorage::remove(const QString &key, const QString &legacyPath) {
    if (QFileInfo::exists(legacyPath) && !QFile::remove(legacyPath)) { m_consistent = false; return false; }
    QByteArray check;
    m_consistent = m_store->remove(key) && m_store->read(key, check) && check.isEmpty();
    return m_consistent;
}
