#pragma once
#include <QObject>
#include <QTimer>
#include <QNetworkAccessManager>
class SoundCloud;
class ArtworkNetworkFactory;

// Только Android: системная медиасессия использует тот же плеер и очередь.
class AndroidMedia : public QObject {
    Q_OBJECT
public:
    AndroidMedia(SoundCloud *soundcloud, ArtworkNetworkFactory *artwork, QObject *parent = nullptr);
    ~AndroidMedia() override;
private:
    void publish();
    SoundCloud *m_soundcloud;
    QNetworkAccessManager *m_network;
    QTimer m_timer;
    QString m_artworkUrl, m_artworkPath, m_lastState;
    bool m_playbackActive = false, m_publishPending = false;
};
