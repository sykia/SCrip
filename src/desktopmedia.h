#pragma once
#include <QObject>
#include <QVariantMap>
#include <memory>
class SoundCloud;
class QWindow;
class DesktopMedia : public QObject {
    Q_OBJECT
public:
    DesktopMedia(SoundCloud *controller, QWindow *window, QObject *parent = nullptr);
    ~DesktopMedia() override;
    QVariantMap snapshot() const;
    void command(const QString &action, qint64 position = 0, const QString &track = {});
    void raise();
signals:
    void updated();
    void seeked(qint64 microseconds);
private:
    SoundCloud *m_controller;
    QWindow *m_window;
    void initializePlatform();
    struct Platform;
    std::unique_ptr<Platform> m_platform;
};
