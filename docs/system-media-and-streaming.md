# System media and streaming preferences

Desktop uses the existing `SoundCloud` controller for every system command.
Linux exports MPRIS on the session bus as
`org.mpris.MediaPlayer2.scrip.instance<PID>` at `/org/mpris/MediaPlayer2`.
Windows uses `ISystemMediaTransportControlsInterop::GetForWindow` for the Qt
window, with native WinRT ABI interfaces from the platform SDK (no additional
library). macOS uses MediaPlayer Now Playing and remote command handlers with
callbacks queued to the Qt GUI thread. It publishes text metadata, duration,
position and playback state; macOS artwork is not currently published.
Android keeps its existing MediaBridge and playback service.

The desktop adapters publish title, credited artist, duration, playback intent,
position and command capabilities. Linux and Windows also publish public unsigned
SoundCloud artwork. It never
exports the controller's complete state, OAuth credentials or stream URLs.
MPRIS positions and Seeked use microseconds; SMTC timeline values use 100 ns;
the controller uses milliseconds. Position updates do not produce MPRIS
PropertiesChanged. SetPosition rejects stale track identities. Next/Previous
preserve paused playback and use the controller's queue/history, repeat, shuffle
and wave logic. Capability reads do not consume shuffle decisions. There is no
additional global key hook or Qt media-key shortcut. System controls live as
long as the application, independently of window visibility. Stop/account
changes clear metadata through the controller; shutdown unregisters the bus
object/service or revokes SMTC callbacks and disables the session. Initialization
failure is logged and does not disable playback.

## Preferences

The existing playback settings contain a two-segment **Standard / Data saver**
selector using native RadioButtons with theme colors and keyboard focus, and a
**Preload** switch on desktop and Android. Values use QSettings keys
`player/playbackQuality`, `player/preloadEnabled`, `player/preloadWifiOnly`.
Existing installations keep Standard, preloading enabled, no Wi-Fi restriction.
Standard retains the previous AAC preference: AAC 160, AAC 96, then MP3 128.
Data saver tries AAC 96, MP3 128, then AAC 160. These are candidate names from
SoundCloud's streams response, not a claim about the currently decoded audio.
Only present full-stream candidates are tried; preview is never a fallback.
Unavailable/invalid/failed candidates fall through the existing stream resolver.
No fixed data usage or unverified quality badge is presented.

Quality applies to subsequent source resolution, without replacing the current
source. Prepared next playback is invalidated and resolved again. Disabling
preload cancels the pending next-stream request (including work waiting for an
OAuth token), clears the spare decoder and its proxy resources, and aborts the
proxy's speculative HLS download. Decoder-requested downloads for the current
source remain available. Re-enabling resumes preparation and HLS warming.
The switch controls both preparing the next track and speculative whole-track
HLS segment downloads; it does not disable buffering required by Qt/FFmpeg,
wave/catalog requests, artwork, updates or explicit downloads.

Crossfade requires preloading. Turning it off during a ramp releases the
outgoing decoder, keeps the already selected incoming track at normal volume,
and prepares no replacement. Quality changes during a ramp also retain the
incoming source. Android continues to disable crossfade.

Android exposes Wi-Fi-only preload when Qt reports support for transport-medium
information. A saved restriction remains visible even if support later vanishes.
Only an explicit WiFi medium permits preloading; Unknown or unavailable
information denies it. This does not assert that Wi-Fi is unmetered. Network
changes reapply the policy. The existing Android diagnostic environment override
`SCRIP_ANDROID_PRELOAD=0` still suppresses next-track preparation.

## Verification and boundaries

Controller tests cover preference persistence, full-only selection and fallback,
request cancellation, token-wait cancellation, local WAV system commands,
paused Next, stale seeks, metadata sanitization, logout, and policy changes
during a real two-decoder crossfade. StreamCache tests use an actual local HTTP
server to verify that disabled speculative requests stay absent while demand
requests work, and that active speculative transfers are aborted. Existing
queue/repeat/shuffle/wave/account tests and real fMP4 HLS seek tests remain in
CTest.

`mprisSessionBusProtocol` uses another connection to the real session D-Bus and
exercises native GetAll serialization, metadata PropertiesChanged and Stop routing. SoundCloud responses in
controller tests use a test transport; this is not a live SoundCloud playback
or Windows SMTC test. Physical media keys, desktop-shell UI, real Windows SMTC,
and Android network transitions require device testing. Build results for this
change are recorded in the implementation report, not inferred from mocked
transport tests.

## Official references

- [MPRIS Player interface](https://specifications.freedesktop.org/mpris/latest/Player_Interface.html)
- [Qt D-Bus adaptors](https://doc.qt.io/qt-6/usingadaptors.html)
- [Windows desktop SMTC interop](https://learn.microsoft.com/en-us/windows/win32/api/systemmediatransportcontrolsinterop/nf-systemmediatransportcontrolsinterop-isystemmediatransportcontrolsinterop-getforwindow)
- [Windows SMTC integration](https://learn.microsoft.com/en-us/windows/apps/develop/media-playback/integrate-with-systemmediatransportcontrols)
- [Qt network transport information](https://doc.qt.io/qt-6/qnetworkinformation.html)
- [SoundCloud API guide](https://developers.soundcloud.com/docs/api/guide)
- [SoundCloud audio streaming formats](https://help.soundcloud.com/hc/en-us/articles/53385714099611-Audio-Streaming-Formats)

Track access metadata distinguishes full (`playable`), snippet-only (`preview`)
and metadata-only (`blocked`) access. Preview-only rows are labelled, remain
inspectable and can be opened on SoundCloud. Full playback never silently falls
back to preview, and preview-only tracks are not preloaded as full audio. Unknown
access is not treated as a restriction. Stream errors do not infer a region or
subscription cause. The selected row stays in the queue so the user can inspect
it or explicitly advance to the next track.
