#pragma once
#include <QObject>
#include <QTimer>
#include <QNetworkAccessManager>
class SoundCloud;
class ArtworkNetworkFactory;

// Только Android: системная медиасессия и виджет используют тот же плеер и очередь.
class AndroidMedia : public QObject {
    Q_OBJECT
public:
    AndroidMedia(SoundCloud *soundcloud, ArtworkNetworkFactory *artwork, QObject *parent = nullptr);
    ~AndroidMedia() override;
    Q_INVOKABLE bool addWidget();
    Q_INVOKABLE int widgetStyle() const;
    Q_INVOKABLE void setWidgetStyle(int style);
private:
    void publish();
    SoundCloud *m_soundcloud;
    QNetworkAccessManager *m_network;
    QTimer m_timer;
    QString m_artworkUrl, m_artworkPath, m_lastState;
    bool m_playbackActive = false, m_publishPending = false;
};
