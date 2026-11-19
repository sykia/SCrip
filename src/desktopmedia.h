#pragma once
#include <QObject>
#include <QVariantMap>
#include <memory>
#include <QTemporaryDir>
#include <QPointer>
class QNetworkAccessManager;
class QNetworkReply;
class SoundCloud;
class QWindow;
class DesktopMedia : public QObject {
    Q_OBJECT
public:
    DesktopMedia(SoundCloud *controller, QWindow *window, QObject *parent = nullptr);
    ~DesktopMedia() override;
    QVariantMap snapshot() const;
    void setArtworkNetwork(QNetworkAccessManager *network);
    void command(const QString &action, qint64 position = 0, const QString &track = {});
    void raise();
signals:
    void updated();
    void seeked(qint64 microseconds);
private:
    SoundCloud *m_controller;
    QWindow *m_window;
    void initializePlatform();
    void updateArtwork();
    QNetworkAccessManager *m_artworkNetwork = nullptr;
    QPointer<QNetworkReply> m_artworkReply;
    QTemporaryDir m_artworkDirectory;
    QString m_artworkSource, m_artworkFile;
    struct Platform;
    std::unique_ptr<Platform> m_platform;
};
