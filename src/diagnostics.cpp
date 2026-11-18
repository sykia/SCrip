#include "diagnostics.h"
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrlQuery>
#include <chrono>
#include <cstdio>

namespace {
QMutex logMutex;
QString logDirectory;
QtMessageHandler previousHandler = nullptr;
constexpr qint64 fileLimit = 512 * 1024;
constexpr int archiveCount = 2;
qint64 monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
void appendLocked(const QString &message) {
    if (logDirectory.isEmpty()) return;
    const auto path = logDirectory + "/current.log";
    if (QFile(path).size() >= fileLimit) {
        QFile::remove(logDirectory + "/previous-2.log");
        QFile::rename(logDirectory + "/previous-1.log", logDirectory + "/previous-2.log");
        QFile::rename(path, logDirectory + "/previous-1.log");
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        file.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + " "
                    + Diagnostics::sanitize(message).left(4096) + "\n").toUtf8());
        file.flush();
    }
}
void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    const char *level = type == QtDebugMsg ? "DEBUG" : type == QtInfoMsg ? "INFO"
        : type == QtWarningMsg ? "WARN" : type == QtCriticalMsg ? "ERROR" : "FATAL";
    QtMessageHandler forward;
    {
        QMutexLocker lock(&logMutex);
        appendLocked(QString::fromLatin1(level) + " " + QString::fromUtf8(context.category ? context.category : "default") + " " + message);
        forward = previousHandler;
    }
    if (forward) forward(type, context, message);
    else {
        const auto safe = Diagnostics::sanitize(message).toUtf8();
        std::fprintf(stderr, "%s: %s\n", level, safe.constData());
    }
}
QString environmentSummary() {
    return QString("Sange %1\nQt %2\nOS: %3\nArchitecture: %4\n")
        .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion()),
             QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
}
}

QString Diagnostics::sanitize(QString text) {
    // Strip whole URLs, not just query strings: signed paths can contain credentials.
    text.replace(QRegularExpression(R"((?:https?|file|content|scrip|soundcloud)://[^\s<>"']+)", QRegularExpression::CaseInsensitiveOption), "[url]");
    text.replace(QRegularExpression(R"(\b(?:Bearer|OAuth)\s+[^\s,;"']+)", QRegularExpression::CaseInsensitiveOption), "[authorization]");
    text.replace(QRegularExpression(R"re(("?(?:[\w-]*(?:token|secret|password|authorization|cookie)[\w-]*|code|client_id)"?\s*[:=]\s*)(?:"[^"]*"|'[^']*'|[^\s,;]+))re", QRegularExpression::CaseInsensitiveOption), "[credential]");
    const auto home = QDir::homePath();
    if (home.size() > 1) {
        text.replace(home, "[home]", Qt::CaseInsensitive);
        auto nativeHome = QDir::toNativeSeparators(home);
        text.replace(nativeHome, "[home]", Qt::CaseInsensitive);
    }
    text.replace(QRegularExpression(R"(\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b)", QRegularExpression::CaseInsensitiveOption), "[email]");
    text.replace('\n', ' '); text.replace('\r', ' ');
    // Avoid terminal/control injection and one event masquerading as multiple entries.
    text.remove(QRegularExpression("[\\x00-\\x08\\x0b\\x0c\\x0e-\\x1f\\x7f]"));
    return text;
}

void Diagnostics::record(const QString &message) {
    QMutexLocker lock(&logMutex);
    appendLocked("EVENT " + message);
}

Diagnostics::Diagnostics(QObject *parent) : QObject(parent) {
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs";
    m_marker = directory + "/session.active";
    {
        QMutexLocker lock(&logMutex);
        if (QDir().mkpath(directory)) {
            QFile probe(directory + "/current.log");
            m_storageReady = probe.open(QIODevice::WriteOnly | QIODevice::Append);
            if (m_storageReady) logDirectory = directory;
        }
        previousHandler = qInstallMessageHandler(messageHandler);
    }
    const bool interrupted = QFile::exists(m_marker);
    if (m_storageReady) {
        QFile marker(m_marker);
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            marker.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            marker.write("active\n");
        }
    }
    record("Session started; " + environmentSummary());
    if (interrupted) record("Previous session did not shut down cleanly (crash, forced exit or OS termination).");
    m_lastBeat = monotonicMs();
    m_active = QGuiApplication::applicationState() == Qt::ApplicationActive;
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        m_lastBeat = monotonicMs();
        m_active = state == Qt::ApplicationActive;
        record(m_active ? "Application active" : "Application inactive; watchdog paused");
    });
    m_heartbeat.setInterval(1000);
    connect(&m_heartbeat, &QTimer::timeout, this, [this] { m_lastBeat = monotonicMs(); });
    m_heartbeat.start();
    m_watchdog = std::thread([this] {
        bool stalled = false;
        std::unique_lock lock(m_waitMutex);
        while (!m_wake.wait_for(lock, std::chrono::seconds(1), [this] { return m_stop; })) {
            const auto delay = monotonicMs() - m_lastBeat.load();
            if (m_active && delay >= 5000 && !stalled) {
                record("Main event loop unresponsive for at least 5 seconds"); stalled = true;
            } else if (stalled && (delay < 2000 || !m_active)) {
                record("Main event loop recovered or application became inactive"); stalled = false;
            }
        }
    });
}

Diagnostics::~Diagnostics() {
    m_heartbeat.stop();
    { std::lock_guard lock(m_waitMutex); m_stop = true; }
    m_wake.notify_one();
    if (m_watchdog.joinable()) m_watchdog.join();
    record("Session ended cleanly");
    if (m_storageReady) QFile::remove(m_marker);
    QMutexLocker lock(&logMutex);
    qInstallMessageHandler(previousHandler);
    previousHandler = nullptr;
    logDirectory.clear();
}

QString Diagnostics::report() const {
    QMutexLocker lock(&logMutex);
    QString result = environmentSummary() + "Generated (UTC): " + QDateTime::currentDateTimeUtc().toString(Qt::ISODate)
        + "\nURLs, common credentials, email addresses and home paths are filtered. Review before posting publicly.\n";
    // Keep clipboard reports manageable, retaining recent history across restarts.
    for (int i = archiveCount; i >= 0; --i) {
        const auto name = i ? QString("previous-%1.log").arg(i) : QString("current.log");
        QFile file(logDirectory + "/" + name);
        if (logDirectory.isEmpty() || !file.open(QIODevice::ReadOnly)) continue;
        const auto limit = i ? 16 * 1024 : 64 * 1024;
        bool truncated = file.size() > limit;
        if (truncated) { file.seek(file.size() - limit); file.readLine(); }
        result += "\n--- " + name + (truncated ? " (recent tail)" : "") + " ---\n";
        // Reapply filtering in case older versions wrote less strictly filtered logs.
        const auto lines = QString::fromUtf8(file.readAll()).split('\n');
        for (const auto &line : lines) result += sanitize(line) + '\n';
    }
    return result;
}
void Diagnostics::setStatus(const QString &value) { m_status = value; emit statusChanged(); }
void Diagnostics::copyReport() {
    if (!m_storageReady) { setStatus(QStringLiteral("Не удалось открыть хранилище логов. Проверьте свободное место и права доступа.")); return; }
    QGuiApplication::clipboard()->setText(report());
    setStatus(QStringLiteral("Лог скопирован. Вставьте его в отчёт на GitHub и опишите, что произошло."));
}
void Diagnostics::openIssue() {
    QUrl url("https://github.com/sykia/sange/issues/new");
    QUrlQuery query;
    query.addQueryItem("title", "[Bug] ");
    query.addQueryItem("body", "## What happened?\n\nDescribe the problem and the steps to reproduce it.\n\n## Expected behavior\n\n\n## Environment\n\n"
                       + environmentSummary() + "\n## Diagnostic log\n\nCopy the latest log from Settings, review it for personal information, and paste it below.\n\n```text\n\n```\n");
    url.setQuery(query);
    if (!QDesktopServices::openUrl(url)) setStatus(QStringLiteral("Не удалось открыть браузер. Адрес: github.com/sykia/sange/issues"));
}
