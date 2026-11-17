#include "linkrouter.h"
#include "soundcloud.h"
#include <QFileOpenEvent>
#include <QCoreApplication>
#include <QDesktopServices>

LinkRouter::LinkRouter(SoundCloud *soundcloud, QObject *parent) : QObject(parent), m_soundcloud(soundcloud) {
    qApp->installEventFilter(this);
    QDesktopServices::setUrlHandler("scrip", this, "openUrl");
    QDesktopServices::setUrlHandler("soundcloud", this, "openUrl");
}
void LinkRouter::openUrl(const QUrl &url) { m_soundcloud->openLink(url.toString(QUrl::FullyEncoded)); }
bool LinkRouter::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::FileOpen) {
        openUrl(static_cast<QFileOpenEvent *>(event)->url());
        return true;
    }
    return QObject::eventFilter(object, event);
}
