# Playlist preview

`qml/PlaylistPopup.qml` provides a shared playlist and album preview. Desktop uses a centered modal; Android uses a bottom sheet with safe-area padding and system Back support. Colors, typography, cover rounding and motion follow the active interface theme.

Opening a collection preserves the underlying catalog and its scroll position. `SoundCloud::loadPlaylist` maintains separate preview tracks, loading, error and pagination state. Request generations discard responses from previously opened collections. Pagination removes duplicate track URNs and supports retrying failed pages.

The preview shows artwork, title, creator and track count, followed by a virtualized track list. Playback starts from the selected track and uses the loaded preview tracks as its queue. Shuffle creates a shuffled queue; additional pages can be loaded before playback. Each track has a three-line actions button. Desktop and Android expose favorites, queue insertion, playlist saving, artist profile, track station, SoundCloud and copy actions. Android reuses its existing track action sheet; desktop uses a matching compact menu. Artist stations, track stations, home mixes and home shelf headings open the same preview. Stations load related tracks without touching playback; local mixes reuse their existing tracks without a network request. A station seed is included once at the start of its list. Opening another preview invalidates in-flight station requests.

Desktop includes a copy-link action when SoundCloud supplies a permalink.

Validation includes catalog preservation, stale-response handling, pagination deduplication, desktop rendering and Android layout loading.

The preview fades and moves as a single surface with an eased entrance of at least 280 ms, following the interface motion duration, and a shorter exit. Reversing an in-flight transition preserves its current opacity and offset. The modal backdrop fades with the surface. Known track counts reserve list space before loading; unknown counts resize smoothly as results arrive. Disabling interface motion removes these animations.

The Android track action sheet moves its popup as a whole. Its background, clipping and text keep the same relative position throughout entrance and exit; the backdrop fades alongside it.
