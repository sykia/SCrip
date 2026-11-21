#pragma once
#include <QObject>
#include <QPointer>
#include <QWindow>
#include <QTimer>
#include <functional>
class QWindow;
class QLocalSocket;

// Native desktop effects for the mini player; never captures the desktop.
class WindowEffects final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool blurAvailable READ blurAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool topAvailable READ topAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString status READ status NOTIFY capabilitiesChanged)
public:
    explicit WindowEffects(QObject *parent = nullptr);
    bool blurAvailable() const { return m_blurAvailable; }
    bool topAvailable() const { return m_topAvailable; }
    QString status() const { return m_status; }
    Q_INVOKABLE void showWindow(QWindow *window);
    Q_INVOKABLE void configure(QWindow *window, bool onTop, int backdrop);
signals:
    void capabilitiesChanged();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void schedule();
    void apply();
    void applyNative();
    void capabilities(bool blur, bool top, const QString &status = {});
    QPointer<QWindow> m_window;
    QTimer m_refresh;
    bool m_onTop = true;
    int m_backdrop = 0;
    bool m_blurAvailable = false, m_topAvailable = false;
    QString m_status;
#ifdef Q_OS_LINUX
    void prepareNative(QWindow *window);
    void request(const QByteArray &command, std::function<void(QByteArray)> done = {});
    void discoverHyprland();
    void restack();
    QString m_hyprPath, m_address;
    QLocalSocket *m_events = nullptr;
    QByteArray m_eventBuffer;
    bool m_discovering = false;
    bool m_hyprLua = true;
    int m_discoveryAttempts = 0;
#endif
};
