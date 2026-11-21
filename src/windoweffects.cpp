#include "windoweffects.h"
#include <QWindow>
#include <QEvent>
#include <QGuiApplication>

WindowEffects::WindowEffects(QObject *parent) : QObject(parent) {
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(50);
    connect(&m_refresh, &QTimer::timeout, this, &WindowEffects::apply);
}
void WindowEffects::showWindow(QWindow *window) {
    if (!window) return;
#ifdef Q_OS_LINUX
    const auto desktopId = QGuiApplication::desktopFileName();
    QGuiApplication::setDesktopFileName(QStringLiteral("SangeMiniPlayer"));
    prepareNative(window);
    window->show();
    QGuiApplication::setDesktopFileName(desktopId);
#else
    window->show();
#endif
}
void WindowEffects::configure(QWindow *window, bool onTop, int backdrop) {
    if (m_window != window) {
        if (m_window) m_window->removeEventFilter(this);
        m_window = window;
        if (window) window->installEventFilter(this);
    }
    m_onTop = onTop;
    m_backdrop = qBound(0, backdrop, 2);
    schedule();
}
void WindowEffects::schedule() { m_refresh.start(); }
bool WindowEffects::eventFilter(QObject *, QEvent *event) {
    switch (event->type()) {
    case QEvent::Show: case QEvent::Expose: case QEvent::PlatformSurface:
    case QEvent::WinIdChange: case QEvent::WindowDeactivate: case QEvent::Resize:
        schedule(); break;
    default: break;
    }
    return false;
}
void WindowEffects::apply() {
    if (m_window && m_window->isVisible() && m_window->handle()) {
        if (m_window->size() != QSize(272, 340)) m_window->resize(272, 340);
        applyNative();
    }
}
void WindowEffects::capabilities(bool blur, bool top, const QString &status) {
    if (m_blurAvailable == blur && m_topAvailable == top && m_status == status) return;
    m_blurAvailable = blur; m_topAvailable = top; m_status = status;
    emit capabilitiesChanged();
}
