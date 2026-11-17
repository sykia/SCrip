#pragma once
#include <QObject>
#include <QUrl>

class SoundCloud;
class LinkRouter : public QObject {
    Q_OBJECT
public:
    explicit LinkRouter(SoundCloud *soundcloud, QObject *parent = nullptr);
public slots:
    void openUrl(const QUrl &url);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    SoundCloud *m_soundcloud;
};
