# SCrip appearance system

The desktop interface uses opaque theme-colored capsules with rounded corners.
Navigation, content panels, settings groups, player controls, queue and plugin
cards retain their existing layout and rounded geometry.

Light and dark appearances, user themes, accent colors, artwork radius, avatar
roundness, motion preferences and plugin appearance precedence remain supported.
Liquid-glass effects and their settings have been removed, including backdrop
captures, optical shaders, background transparency and native compositor blur.
No extra shader tooling or desktop compositor integration is required.

The Android entry point remains in `qml/android/Main.qml`; the desktop entry
point is `qml/Main.qml`. Shared plugin cards use ordinary opaque backgrounds.

## Track pages and player input

The floating playback dock consumes unused pointer and wheel events so tracks
scrolling behind it cannot receive accidental clicks. Artwork and titles open a
central track page from the dock, library, search results, home shelves
and queue. Opening a page does not start playback; explicit play controls remain
available. The page uses theme colors, rounded surfaces, large artwork, artist
navigation, available description text, like and queue actions. Escape or Back
returns to the previous view.

## Central track view and audio controls

The track page is non-modal and anchored inside the main content area, above the
playback dock. Navigation, the inspector and playback controls remain visible and
interactive. Navigating to another section closes the page; the underlying page
keeps its scroll state and is hidden while the track view is open.

The five-band equalizer uses independent stereo biquads at 60 Hz, 250 Hz, 1 kHz,
4 kHz and 12 kHz with a range of -12 to +12 dB. Gains and the enabled state are
stored in user settings. Positive gains reserve automatic headroom, with a final
sample limiter. Reset restores flat gains. The bypass uses the original output.

On Qt 6.8+ with the FFmpeg backend, decoded buffers are processed into floating
point PCM and played through a bounded QAudioSink queue. Each decoder has its own
filter state and sink to retain crossfade volume ramps. Native outputs remain
muted timing sources while processing is enabled. Source changes, pause, stop and
seek clear buffered processing. Unsupported formats or output errors disable the
equalizer and return playback to the native output. Older Qt builds retain native
playback and show that the equalizer is unavailable.

The spectrum uses a 2048-point Hann-windowed FFT and 20 logarithmic frequency
bands. Channel powers are combined without canceling opposite stereo phases.
It analyzes decoded audio (processed audio when equalization is enabled), with
smoothed decay and no simulated signal. Analysis is active only while the current
track page is visible; equalization continues independently in the background.

Validation includes flat response, bass cut and boost, stereo isolation,
frequency discrimination and silence; real local PCM playback through the Linux
audio device, pause, seek, simultaneous decoder routing; and page boundaries in
wide and narrow layouts. Windows and Android runtime audio validation remains
outstanding.

Qt API references:
[QAudioBufferOutput](https://doc.qt.io/qt-6/qaudiobufferoutput.html),
[QMediaPlayer audio buffer output](https://doc.qt.io/qt-6/qmediaplayer.html#audioBufferOutput-prop),
[QAudioSink](https://doc.qt.io/qt-6/qaudiosink.html).

## Android mobile appearance

The Android interface uses neutral charcoal surfaces in dark mode and neutral
white surfaces in light mode. A floating capsule groups Home, Feed and Library,
with a separate circular Search button and animated selection indicator. Home
provides 154 dp artwork shelves. Track rows use 54 dp
artwork and 80 dp height. The mini-player and expanded player use neutral surfaces.

Empty list messages are part of the scrolling footer, after the profile card and
its tabs, so they cannot overlap profile information. Track actions appear in a
height-limited scrollable bottom sheet with an artwork header, explicit favorite
state, leading icons and left-aligned action labels. System safe-area insets,
theme preferences and existing account data remain supported.

Android track menus look up lyrics through the shared lyrics backend. A lyrics
action appears when text is available and opens a full-height, scrollable reader.
The reader supports saved custom lyrics and provider lyrics. Android controls use
their own pressed backgrounds without the additional shared feedback overlay;
track rows use a subtle inset highlight.

The Android mini-player uses a compact 72 dp card with 48 dp artwork, a short
progress track below the metadata and a contrasting 44 dp play/pause button.
Favorite controls remain available, and tapping or swiping up opens the player.

The Android lyrics reader uses a vertically constrained Flickable with explicit
text content height, native touch inertia and a soft boundary rebound. The title
and source remain fixed while the text scrolls; opening the reader resets it to
the beginning.
