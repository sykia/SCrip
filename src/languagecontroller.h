#pragma once

#include <QCoreApplication>
#include <QHash>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

// Russian source strings are shared by the desktop and Android interfaces.
class LanguageController final : public QTranslator {
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
public:
    explicit LanguageController(QObject *parent = nullptr) : QTranslator(parent) {
        const auto saved = QSettings().value("interface/language").toString();
        m_language = saved == "ru" || saved == "en" ? saved
            : QLocale::system().language() == QLocale::Russian ? "ru" : "en";
    }
    QString language() const { return m_language; }
    void setLanguage(const QString &language) {
        if ((language != "ru" && language != "en") || language == m_language) return;
        m_language = language;
        QSettings().setValue("interface/language", language);
        emit languageChanged();
    }
    bool isEmpty() const override { return false; }
    QString translate(const char *, const char *source, const char * = nullptr, int = -1) const override {
        if (m_language != "en") return {};
        return translations().value(QString::fromUtf8(source));
    }
Q_SIGNALS:
    void languageChanged();
private:
    static const QHash<QString, QString> &translations() {
        static const QHash<QString, QString> values = {
#include "translations_en.inc"
        };
        return values;
    }
    QString m_language;
};
