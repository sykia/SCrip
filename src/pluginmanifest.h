#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace PluginManifest {
struct Result {
    QJsonObject document;
    QString error;
};
Result parse(const QByteArray &bytes);
QString validate(const QJsonObject &document);
bool version(const QString &text, QList<int> *parts = nullptr);
int compareVersions(const QString &left, const QString &right);
bool compatible(const QJsonObject &range, const QString &version);
bool settingValue(const QJsonObject &setting, const QJsonValue &value);
QString commandPermission(const QString &command);
} // namespace PluginManifest
