#pragma once
#include <QQuickPaintedItem>
#include <QImage>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QNetworkReply>

// Dedicated, unauthenticated PNG transport. Never shares the API manager.
class TrackWaveform : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(qreal progress MEMBER m_progress NOTIFY progressChanged)
    Q_PROPERTY(QColor ink MEMBER m_ink NOTIFY colorsChanged)
    Q_PROPERTY(QColor accent MEMBER m_accent NOTIFY colorsChanged)
public:
    explicit TrackWaveform(QQuickItem *parent = nullptr);
    QUrl source() const { return m_source; }
    void setSource(const QUrl &url);
    bool ready() const { return !m_image.isNull(); }
    void paint(QPainter *painter) override;
    static bool allowed(const QUrl &url);
    static QImage decode(const QByteArray &bytes);
signals:
    void sourceChanged();
    void readyChanged();
    void progressChanged();
    void colorsChanged();
private:
    friend class TrackDetailsTests;
    void request(const QUrl &url, quint64 generation, int redirects);
    void tint();
    QNetworkAccessManager m_network;
    QNetworkAccessManager *m_transport = &m_network;
    QPointer<QNetworkReply> m_reply;
    QUrl m_source;
    QImage m_image, m_remaining, m_played;
    qreal m_progress = 0;
    QColor m_ink = Qt::gray, m_accent = Qt::white;
    quint64 m_generation = 0;
};
