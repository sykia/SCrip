#include "desktopmedia.h"
#include "soundcloud.h"
#include <QCoreApplication>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <utility>
#include <atomic>
#include <functional>
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.media.h>
#include <windows.foundation.h>
#include <windows.storage.streams.h>
#include <systemmediatransportcontrolsinterop.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace M = ABI::Windows::Media;
namespace F = ABI::Windows::Foundation;
namespace S = ABI::Windows::Storage::Streams;
namespace {
class Text {
public:
    explicit Text(const QString &value) { WindowsCreateString(reinterpret_cast<const wchar_t *>(value.utf16()), value.size(), &s); }
    ~Text() { WindowsDeleteString(s); }
    operator HSTRING() const { return s; }
private: HSTRING s = nullptr;
};
template<class T> HRESULT factory(const wchar_t *name, ComPtr<T> &result) {
    return RoGetActivationFactory(Text(QString::fromWCharArray(name)), IID_PPV_ARGS(result.GetAddressOf()));
}
template<class Interface, class Args> class Handler final : public Interface {
public:
    explicit Handler(std::function<void(Args *)> callback) : fn(std::move(callback)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IAgileObject) || iid == __uuidof(Interface)) { *out = static_cast<Interface *>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Invoke(M::ISystemMediaTransportControls *, Args *args) override { fn(args); return S_OK; }
private: std::atomic<ULONG> refs{1}; std::function<void(Args *)> fn;
};
using ButtonHandler = F::ITypedEventHandler<M::SystemMediaTransportControls *, M::SystemMediaTransportControlsButtonPressedEventArgs *>;
using PositionHandler = F::ITypedEventHandler<M::SystemMediaTransportControls *, M::PlaybackPositionChangeRequestedEventArgs *>;
}
struct DesktopMedia::Platform {
    bool initialized = false, buttons = false, positions = false;
    ComPtr<M::ISystemMediaTransportControls> controls;
    ComPtr<M::ISystemMediaTransportControls2> controls2;
    ComPtr<M::ISystemMediaTransportControlsDisplayUpdater> display;
    EventRegistrationToken buttonToken{}, positionToken{};
    QVariantMap last;
    ~Platform() {
        if (controls) {
            if (buttons) controls->remove_ButtonPressed(buttonToken);
            if (positions) controls2->remove_PlaybackPositionChangeRequested(positionToken);
            if (display) { display->ClearAll(); display->Update(); }
            controls->put_IsEnabled(false);
        }
        display.Reset(); controls2.Reset(); controls.Reset();
        if (initialized) RoUninitialize();
    }
    void publish(const QVariantMap &s) {
        if (!controls || !display) return;
        const bool has = s.value("hasTrack").toBool();
        controls->put_IsEnabled(has || s.value("canPlay").toBool());
        controls->put_IsPlayEnabled(s.value("canPlay").toBool()); controls->put_IsPauseEnabled(has); controls->put_IsStopEnabled(has);
        controls->put_IsNextEnabled(s.value("canNext").toBool()); controls->put_IsPreviousEnabled(s.value("canPrevious").toBool());
        controls->put_PlaybackStatus(!has ? M::MediaPlaybackStatus_Stopped : s.value("status") == "Playing" ? M::MediaPlaybackStatus_Playing : M::MediaPlaybackStatus_Paused);
        if (last.value("trackId") != s.value("trackId") || last.value("title") != s.value("title")
            || last.value("artist") != s.value("artist") || last.value("artwork") != s.value("artwork")) {
            display->ClearAll();
            if (has) {
                display->put_Type(M::MediaPlaybackType_Music);
                ComPtr<M::IMusicDisplayProperties> music;
                if (SUCCEEDED(display->get_MusicProperties(&music))) {
                    music->put_Title(Text(s.value("title").toString())); music->put_Artist(Text(s.value("artist").toString()));
                }
                if (!s.value("artwork").toString().isEmpty()) {
                    ComPtr<F::IUriRuntimeClassFactory> uriFactory; ComPtr<F::IUriRuntimeClass> uri;
                    ComPtr<S::IRandomAccessStreamReferenceStatics> streams; ComPtr<S::IRandomAccessStreamReference> cover;
                    if (SUCCEEDED(factory(L"Windows.Foundation.Uri", uriFactory))
                        && SUCCEEDED(uriFactory->CreateUri(Text(s.value("artwork").toString()), &uri))
                        && SUCCEEDED(factory(L"Windows.Storage.Streams.RandomAccessStreamReference", streams))
                        && SUCCEEDED(streams->CreateFromUri(uri.Get(), &cover))) display->put_Thumbnail(cover.Get());
                }
            }
            if (FAILED(display->Update())) qWarning("SMTC metadata update failed");
        }
        if (controls2) {
            ComPtr<IInspectable> instance; ComPtr<M::ISystemMediaTransportControlsTimelineProperties> timeline;
            if (SUCCEEDED(RoActivateInstance(Text("Windows.Media.SystemMediaTransportControlsTimelineProperties"), &instance))
                && SUCCEEDED(instance.As(&timeline))) {
                const qint64 duration = s.value("duration").toLongLong() * 10000;
                const qint64 position = s.value("position").toLongLong() * 10000;
                timeline->put_StartTime({0}); timeline->put_EndTime({duration}); timeline->put_Position({position});
                timeline->put_MinSeekTime({s.value("canSeek").toBool() ? 0 : position});
                timeline->put_MaxSeekTime({s.value("canSeek").toBool() ? duration : position});
                controls2->UpdateTimelineProperties(timeline.Get());
            }
        }
        last = s;
    }
};
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
    const HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
    m_platform->initialized = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) { qWarning("SMTC initialization failed"); return; }
    ComPtr<ISystemMediaTransportControlsInterop> interop;
    if (!m_window || FAILED(factory(L"Windows.Media.SystemMediaTransportControls", interop))
        || FAILED(interop->GetForWindow(reinterpret_cast<HWND>(m_window->winId()), IID_PPV_ARGS(&m_platform->controls)))
        || FAILED(m_platform->controls->get_DisplayUpdater(&m_platform->display))) { qWarning("SMTC unavailable"); return; }
    QPointer<DesktopMedia> guard(this);
    ComPtr<ButtonHandler> buttons;
    buttons.Attach(new Handler<ButtonHandler, M::ISystemMediaTransportControlsButtonPressedEventArgs>([guard](auto *args) {
        M::SystemMediaTransportControlsButton button;
        if (FAILED(args->get_Button(&button))) return;
        QString action;
        switch (button) {
        case M::SystemMediaTransportControlsButton_Play: action = "play"; break;
        case M::SystemMediaTransportControlsButton_Pause: action = "pause"; break;
        case M::SystemMediaTransportControlsButton_Stop: action = "stop"; break;
        case M::SystemMediaTransportControlsButton_Next: action = "next"; break;
        case M::SystemMediaTransportControlsButton_Previous: action = "previous"; break;
        default: return;
        }
        QMetaObject::invokeMethod(qApp, [guard, action] { if (guard) guard->command(action); }, Qt::QueuedConnection);
    }));
    m_platform->buttons = SUCCEEDED(m_platform->controls->add_ButtonPressed(buttons.Get(), &m_platform->buttonToken));
    if (!m_platform->buttons) qWarning("SMTC button registration failed");
    if (SUCCEEDED(m_platform->controls.As(&m_platform->controls2))) {
        ComPtr<PositionHandler> positions;
        positions.Attach(new Handler<PositionHandler, M::IPlaybackPositionChangeRequestedEventArgs>([guard](auto *args) {
            F::TimeSpan value;
            if (FAILED(args->get_RequestedPlaybackPosition(&value))) return;
            QMetaObject::invokeMethod(qApp, [guard, value] { if (guard) guard->command("seek", value.Duration / 10000); }, Qt::QueuedConnection);
        }));
        m_platform->positions = SUCCEEDED(m_platform->controls2->add_PlaybackPositionChangeRequested(positions.Get(), &m_platform->positionToken));
    }
    connect(this, &DesktopMedia::updated, this, [this] { m_platform->publish(snapshot()); });
    m_platform->publish(snapshot());
}
DesktopMedia::~DesktopMedia() = default;
