#include "desktopmedia.h"
#include "soundcloud.h"
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QCoreApplication>
#include <QTimer>
#include <utility>
class MprisRoot : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ no CONSTANT)
    Q_PROPERTY(bool CanRaise READ yes CONSTANT)
    Q_PROPERTY(bool HasTrackList READ no CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ desktop CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ empty CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ empty CONSTANT)
public:
    explicit MprisRoot(DesktopMedia *media) : QDBusAbstractAdaptor(media), m(media) {}
    bool no() const { return false; } bool yes() const { return true; }
    QString identity() const { return "SCrip"; } QString desktop() const { return "scrip"; }
    QStringList empty() const { return {}; }
public slots:
    void Raise() { m->raise(); } void Quit() {}
private: DesktopMedia *m;
};
class MprisPlayer : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ status)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(double MinimumRate READ rate CONSTANT)
    Q_PROPERTY(double MaximumRate READ rate CONSTANT)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(bool CanGoNext READ canNext)
    Q_PROPERTY(bool CanGoPrevious READ canPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ hasTrack)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ yes CONSTANT)
public:
    MprisPlayer(DesktopMedia *media, SoundCloud *sc) : QDBusAbstractAdaptor(media), m(media), c(sc) {
        connect(m, &DesktopMedia::seeked, this, &MprisPlayer::Seeked);
        connect(m, &DesktopMedia::updated, this, [this] {
            const QVariantMap values{{"PlaybackStatus", status()}, {"Metadata", metadata()}, {"CanGoNext", canNext()},
                {"CanGoPrevious", canPrevious()}, {"CanPlay", canPlay()}, {"CanPause", hasTrack()}, {"CanSeek", canSeek()}, {"Volume", volume()}};
            QVariantMap changed;
            for (auto it = values.begin(); it != values.end(); ++it) if (last.value(it.key()) != it.value()) changed.insert(it.key(), it.value());
            last = values;
            if (changed.isEmpty()) return;
            auto msg = QDBusMessage::createSignal("/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "PropertiesChanged");
            msg << QString("org.mpris.MediaPlayer2.Player") << changed << QStringList{};
            QDBusConnection::sessionBus().send(msg);
        });
    }
    QString status() const { return m->snapshot().value("status").toString(); }
    QVariantMap metadata() const {
        const auto s = m->snapshot();
        QVariantMap data{{"mpris:trackid", QVariant::fromValue(QDBusObjectPath(s.value("trackId").toString()))}};
        if (!hasTrack()) return data;
        data.insert("xesam:title", s.value("title")); data.insert("xesam:artist", QStringList{s.value("artist").toString()});
        data.insert("mpris:length", s.value("duration").toLongLong() * 1000);
        if (!s.value("artwork").toString().isEmpty()) data.insert("mpris:artUrl", s.value("artwork"));
        return data;
    }
    double rate() const { return 1; } void setRate(double) {}
    double volume() const { return c->state().value("volume").toDouble(); } void setVolume(double value) { c->volume(value); }
    qlonglong position() const { return c->position() * 1000; }
    bool canNext() const { return m->snapshot().value("canNext").toBool(); }
    bool canPrevious() const { return m->snapshot().value("canPrevious").toBool(); }
    bool canPlay() const { return m->snapshot().value("canPlay").toBool(); }
    bool hasTrack() const { return m->snapshot().value("hasTrack").toBool(); }
    bool canSeek() const { return m->snapshot().value("canSeek").toBool(); }
    bool yes() const { return true; }
public slots:
    void Next() { m->command("next"); } void Previous() { m->command("previous"); }
    void Pause() { m->command("pause"); } void Play() { m->command("play"); }
    void PlayPause() { m->command("toggle"); } void Stop() { m->command("stop"); }
    void Seek(qlonglong offset) {
        if (!canSeek()) return;
        const auto current = c->position();
        const auto delta = offset / 1000;
        if (delta > c->duration() - current) { Next(); return; }
        m->command("seek", delta < -current ? 0 : current + delta);
    }
    void SetPosition(const QDBusObjectPath &track, qlonglong value) { if (value >= 0) m->command("seek", value / 1000, track.path()); }
    void OpenUri(const QString &) {}
signals: void Seeked(qlonglong position);
private: DesktopMedia *m; SoundCloud *c; QVariantMap last;
};
struct DesktopMedia::Platform { QString service; bool registered = false; };
DesktopMedia::DesktopMedia(SoundCloud *controller, QWindow *window, QObject *parent)
    : QObject(parent), m_controller(controller), m_window(window) {
    initializePlatform();
    auto pending = std::make_shared<bool>(false);
    connect(controller, &SoundCloud::catalogChanged, this, [this, pending] {
        if (std::exchange(*pending, true)) return;
        QTimer::singleShot(0, this, [this, pending] { *pending = false; emit updated(); });
    });
    connect(controller, &SoundCloud::progressChanged, this, &DesktopMedia::updated);
    connect(controller, &SoundCloud::playbackSeeked, this, [this] { emit seeked(m_controller->position() * 1000); });
}
void DesktopMedia::initializePlatform() {
    m_platform = std::make_unique<Platform>();
    new MprisRoot(this); new MprisPlayer(this, m_controller);
    auto bus = QDBusConnection::sessionBus();
    m_platform->service = "org.mpris.MediaPlayer2.scrip.instance" + QString::number(QCoreApplication::applicationPid());
    if (!bus.registerObject("/org/mpris/MediaPlayer2", this, QDBusConnection::ExportAdaptors)) { qWarning("MPRIS object registration failed"); return; }
    m_platform->registered = bus.registerService(m_platform->service);
    if (!m_platform->registered) { bus.unregisterObject("/org/mpris/MediaPlayer2"); qWarning("MPRIS service registration failed"); }
}
DesktopMedia::~DesktopMedia() {
    if (m_platform && m_platform->registered) {
        auto bus = QDBusConnection::sessionBus(); bus.unregisterService(m_platform->service); bus.unregisterObject("/org/mpris/MediaPlayer2");
    }
}
#include "mpris.moc"
