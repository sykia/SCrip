#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <memory>

// Внутренний интерфейс: не QObject и не часть Plugin API/QML.
class SecretStore {
public:
    virtual ~SecretStore() = default;
    // Успех с пустым значением означает отсутствие записи.
    virtual bool read(const QString &key, QByteArray &value) = 0;
    virtual bool write(const QString &key, const QByteArray &value) = 0;
    virtual bool remove(const QString &key) = 0;
};
std::shared_ptr<SecretStore> systemSecretStore();

class AuthStorage {
public:
    explicit AuthStorage(std::shared_ptr<SecretStore> store) : m_store(std::move(store)) {}
    bool load(const QString &key, const QString &legacyPath, QJsonObject &value);
    bool save(const QString &key, const QJsonObject &value);
    bool remove(const QString &key, const QString &legacyPath);
    bool consistent() const { return m_consistent; }
    static QString errorMessage();
private:
    std::shared_ptr<SecretStore> m_store;
    bool m_consistent = true;
};
