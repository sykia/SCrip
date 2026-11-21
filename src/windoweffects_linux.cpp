#include "windoweffects.h"
#include <QWindow>
#include <QGuiApplication>
#include <QLocalSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>
#include <QPainterPath>
#include <memory>
#include <utility>
#ifdef SANGE_XCB
#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>
#endif
#ifdef SANGE_KWINDOWSYSTEM
#include <KWindowEffects>
#include <KX11Extras>
#include <KWindowSystem>
#include <NETWM>
#endif

void WindowEffects::prepareNative(QWindow *window) {
    window->setProperty("_q_xcb_wm_window_role", "sange-mini-player");
    window->create();
#ifdef SANGE_XCB
#if QT_CONFIG(xcb)
    if (auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
        // Separate identity keeps per-application geometry rules away from this widget.
        const char wmClass[] = "sange-mini-player\0SangeMiniPlayer\0";
        xcb_change_property(native->connection(), XCB_PROP_MODE_REPLACE,
            window->winId(), XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof(wmClass) - 1, wmClass);
        xcb_flush(native->connection());
    }
#endif
#endif
}

void WindowEffects::request(const QByteArray &command, std::function<void(QByteArray)> done) {
    auto *socket = new QLocalSocket(this);
    auto data = std::make_shared<QByteArray>();
    auto completed = std::make_shared<bool>(false);
    auto finish = [socket, data, completed, done] {
        if (std::exchange(*completed, true)) return;
        *data += socket->readAll();
        if (done) done(*data);
        socket->deleteLater();
    };
    connect(socket, &QLocalSocket::connected, socket, [socket, command] { socket->write(command); });
    connect(socket, &QLocalSocket::readyRead, socket, [socket, data] { *data += socket->readAll(); });
    connect(socket, &QLocalSocket::disconnected, socket, finish);
    connect(socket, &QLocalSocket::errorOccurred, socket, [finish](auto) { finish(); });
    QTimer::singleShot(1200, socket, finish);
    socket->connectToServer(m_hyprPath + "/.socket.sock");
}
void WindowEffects::restack() {
    if (!m_window || !m_window->isVisible() || !m_onTop || m_address.isEmpty()) return;
    // Changes stacking without stealing focus from the user's active application.
    const auto selector = "address:" + m_address.toUtf8();
    request(m_hyprLua
        ? "dispatch hl.dsp.window.alter_zorder({mode=\"top\",window=\"" + selector + "\"})"
        : "dispatch alterzorder top," + selector);
}
void WindowEffects::discoverHyprland() {
    if (m_discovering) return;
    m_discovering = true;
    request("j/clients", [this](const QByteArray &data) {
        m_discovering = false;
        if (!m_window || !m_window->isVisible()) return;
        QJsonObject found;
        for (const auto &entry : QJsonDocument::fromJson(data).array()) {
            const auto client = entry.toObject();
            if (client["pid"].toInteger() == QCoreApplication::applicationPid()
                && client["title"].toString() == m_window->title()) { found = client; break; }
        }
        const auto address = found["address"].toString();
        if (!QRegularExpression("^0x[0-9a-fA-F]+$").match(address).hasMatch()) {
            if (++m_discoveryAttempts < 10) QTimer::singleShot(150, this, &WindowEffects::apply);
            return;
        }
        m_discoveryAttempts = 0;
        m_address = address;
        const auto selector = "address:" + address.toUtf8();
        if (!found["floating"].toBool())
            request(m_hyprLua ? "dispatch hl.dsp.window.float({action=\"set\",window=\"" + selector + "\"})"
                              : "dispatch setfloating " + selector);
        if (found["pinned"].toBool() != m_onTop)
            request(m_hyprLua ? "dispatch hl.dsp.window.pin({action=\"" + QByteArray(m_onTop ? "set" : "unset") + "\",window=\"" + selector + "\"})"
                              : "dispatch pin " + selector);
        const auto size = found["size"].toArray();
        if (size.size() == 2 && (size[0].toInt() != 272 || size[1].toInt() != 340))
            request(m_hyprLua ? "dispatch hl.dsp.window.resize({x=272,y=340,relative=false,window=\"" + selector + "\"})"
                              : "dispatch resizewindowpixel exact 272 340," + selector);
        request(m_hyprLua ? "dispatch hl.dsp.window.set_prop({prop=\"rounding\",value=\"20\",window=\"" + selector + "\"})"
                          : "dispatch setprop " + selector + " rounding 20");
        request(m_hyprLua ? "dispatch hl.dsp.window.set_prop({prop=\"no_blur\",value=\"0\",window=\"" + selector + "\"})"
                          : "dispatch setprop " + selector + " noblur 0");
        request("j/getoption decoration:blur:enabled", [this](const QByteArray &reply) {
            const auto option = QJsonDocument::fromJson(reply).object();
            const bool blur = option["bool"].toBool() || option["int"].toInt() != 0;
            capabilities(blur, true, blur ? QString{} : tr("Размытие отключено в настройках оконного менеджера"));
        });
        restack();
    });
}
void WindowEffects::applyNative() {
    const bool x11 = QGuiApplication::platformName() == "xcb";
    bool blur = false;
#ifdef SANGE_KWINDOWSYSTEM
    blur = KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(QPointF(), m_window->size()), 20, 20);
    KWindowEffects::enableBlurBehind(m_window, m_backdrop != 0, QRegion(shape.toFillPolygon().toPolygon()));
    if (x11) {
        if (m_onTop) KX11Extras::setState(m_window->winId(), NET::KeepAbove);
        else KX11Extras::clearState(m_window->winId(), NET::KeepAbove);
    }
#endif
    const auto signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    if (!signature.isEmpty()) {
        if (!m_events) {
            m_hyprPath = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/hypr/" + signature;
            m_events = new QLocalSocket(this);
            connect(m_events, &QLocalSocket::connected, this, [this] {
                request("j/version", [this](const QByteArray &reply) {
                    const auto version = QJsonDocument::fromJson(reply).object()["version"].toString();
                    m_hyprLua = QVersionNumber::fromString(version) >= QVersionNumber(0, 55);
                    discoverHyprland();
                });
            });
            connect(m_events, &QLocalSocket::readyRead, this, [this] {
                m_eventBuffer += m_events->readAll();
                while (m_eventBuffer.contains('\n')) {
                    const auto end = m_eventBuffer.indexOf('\n');
                    const auto event = m_eventBuffer.left(end);
                    m_eventBuffer.remove(0, end + 1);
                    if (event.startsWith("activewindowv2>>") || event.startsWith("openwindow>>")
                        || event.startsWith("fullscreen>>") || event.startsWith("workspace>>")) {
                        QTimer::singleShot(30, this, &WindowEffects::restack);
                    }
                    if (event.startsWith("configreloaded>>")) schedule();
                }
            });
            m_events->connectToServer(m_hyprPath + "/.socket2.sock");
        } else discoverHyprland();
        return;
    }
    capabilities(blur, x11, !blur && m_backdrop ? tr("Оконный менеджер не предоставляет системное размытие")
                 : !x11 && m_onTop ? tr("Оконный менеджер не поддерживает закрепление этого окна") : QString{});
}
