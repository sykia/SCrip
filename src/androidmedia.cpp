#include "androidmedia.h"
#include "soundcloud.h"
#include "artworknetwork.h"
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QStandardPaths>
#include <QSaveFile>
#include <QImage>
#include <QDir>
#include <QtCore/qcoreapplication_platform.h>

namespace {
QPointer<SoundCloud> controller;
void mediaAction(JNIEnv *, jclass, jint action, jlong position) {
    if (!qApp || QCoreApplication::closingDown()) return;
    QMetaObject::invokeMethod(qApp, [action, position] {
        if (!controller) return;
        switch (action) {
        case 0: controller->togglePlayback(); break;
        case 1: controller->previous(); break;
        case 2: controller->next(); break;
        case 3: controller->seek(position); break;
        case 4: controller->resumePlayback(); break;
        case 5: controller->pausePlayback(); break;
        case 6: controller->stopPlayback(); break;
        case 7: controller->like(controller->state().value("current").toMap()); break;
        case 8: controller->pausePlayback(); break; // Пауза из-за системного фокуса.
        }
    }, Qt::QueuedConnection);
}
}
AndroidMedia::AndroidMedia(SoundCloud *soundcloud, ArtworkNetworkFactory *artwork, QObject *parent)
    : QObject(parent), m_soundcloud(soundcloud), m_network(artwork->create(this)) {
    controller = soundcloud;
    QJniEnvironment env;
    if (!env.registerNativeMethods("org/scrip/app/MediaBridge", {{"nativeAction", "(IJ)V", reinterpret_cast<void *>(mediaAction)}}))
        qWarning("Не удалось подключить системное управление музыкой");
    QJniObject::callStaticMethod<void>("org/scrip/app/MediaBridge", "initialize", "(Landroid/content/Context;)V",
        QNativeInterface::QAndroidApplication::context().object());
    // Позиция обновляется не чаще раза в секунду; кнопки и смена трека — сразу.
    connect(soundcloud, &SoundCloud::catalogChanged, this, [this] {
        const auto s = m_soundcloud->state();
        const bool active = s.value("playbackActive").toBool();
        if (active && !m_playbackActive)
            QJniObject::callStaticMethod<void>("org/scrip/app/MediaBridge", "preparePlayback", "()V");
        m_playbackActive = active;
        // Снимок после всей смены трека: промежуточный stop() не означает паузу.
        if (!m_publishPending) {
            m_publishPending = true;
            QTimer::singleShot(0, this, [this] { m_publishPending = false; publish(); });
        }
    });
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &AndroidMedia::publish);
    m_timer.start(); publish();
}
AndroidMedia::~AndroidMedia() {
    QJniObject::callStaticMethod<void>("org/scrip/app/MediaBridge", "shutdown", "()V");
    controller.clear();
}
bool AndroidMedia::addWidget() {
    return QJniObject::callStaticMethod<jboolean>("org/scrip/app/MediaBridge", "pinWidget", "()Z");
}
int AndroidMedia::widgetStyle() const {
    return QJniObject::callStaticMethod<jint>("org/scrip/app/MediaBridge", "getWidgetStyle", "()I");
}
void AndroidMedia::setWidgetStyle(int style) {
    QJniObject::callStaticMethod<void>("org/scrip/app/MediaBridge", "setWidgetStyle", "(I)V", jint(style));
}
void AndroidMedia::publish() {
    const auto state = m_soundcloud->state();
    const auto track = state.value("current").toMap();
    const auto user = track.value("user").toMap();
    QString artwork = track.value("artwork_url").toString();
    if (artwork.isEmpty()) artwork = user.value("avatar_url").toString();
    artwork.replace("-large.", "-t300x300.");
    if (artwork != m_artworkUrl) {
        m_artworkUrl = artwork; m_artworkPath.clear();
        if (QUrl(artwork).scheme() == "https") {
            auto *reply = m_network->get(QNetworkRequest(QUrl(artwork)));
            connect(reply, &QNetworkReply::finished, this, [this, reply, artwork] {
                reply->deleteLater();
                if (artwork != m_artworkUrl || reply->error() != QNetworkReply::NoError) return;
                const auto image = QImage::fromData(reply->readAll());
                if (image.isNull()) return;
                const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/widget";
                QDir().mkpath(dir);
                // Один файл: изображения предыдущих треков не накапливаются.
                QSaveFile file(dir + "/cover.png");
                if (file.open(QIODevice::WriteOnly) && image.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&file, "PNG") && file.commit()) {
                    m_artworkPath = file.fileName(); publish();
                }
            });
        }
    }
    const QJsonObject data{{"title", track.value("title").toString()}, {"artist", user.value("username").toString()},
        {"artwork", m_artworkPath}, {"playing", state.value("playing").toBool()},
        {"buffering", state.value("buffering").toBool()},
        {"liked", track.value("user_favorite").toBool()},
        {"active", state.value("playbackActive").toBool()}, {"hasTrack", !track.isEmpty()},
        {"position", state.value("position").toLongLong()}, {"duration", state.value("duration").toLongLong()}};
    const auto json = QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
    if (json == m_lastState) return;
    m_lastState = json;
    QJniObject::callStaticMethod<void>("org/scrip/app/MediaBridge", "update", "(Ljava/lang/String;)V", QJniObject::fromString(json).object());
}
