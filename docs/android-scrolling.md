# Android scrolling

Android scrolling uses Qt Quick's `Flickable` implementation, including the
inherited behavior in `ListView` and the internal viewport of `ScrollView`.
Keep the platform defaults for `flickDeceleration` and `maximumFlickVelocity`:
Qt estimates release velocity, integrates motion over elapsed time and handles
press-to-stop and direction changes. Gesture distance and velocity are measurable;
physical finger pressure is not a portable input to Flickable.

Do not animate `contentX` or `contentY` in response to touch movement or replay
pointer positions through a timer. The desktop-only `SmoothWheel` animation is
disabled on Android. Programmatic navigation (such as following a timed comment)
and delegate insertion/reordering transitions are separate from touch inertia.

## Viewport audit

- Home: vertical ScrollView with explicitly horizontal shelf ListViews.
- Search and library: vertical search filters, horizontal collection filters,
  and the shared tracks/profile ListView; existing scroll-key restoration stays
  in place. Profile tabs retain a horizontal Flickable.
- Settings, network diagnostics and plugin tools: ScrollView/Flickable viewports.
- Playlist preview, playlist editor, saved searches, selection destinations,
  people, recommendation exclusions and queue: existing Flickable/ListView
  viewports retain their models, selection and position handling.
- Track actions: the content belongs exclusively to its Flickable. Drag the
  fixed header to dismiss; a short pull settles with `SheetMotion`.
  This avoids a parent handler stealing scrolls and manually re-flicking a list.
- Player dismissal permits the popup background to hand over its item grab,
  while retaining pointer-handler arbitration. Release velocity contributes to
  dismissal. The cover fits the actual implicit height of the controls, so long
  two-line titles and uploader rows do not push actions below the safe area.
- The home wave animation/analysis pauses while the player, queue or playlist
  preview covers it. Device performance testing should use a Release APK;
  a release certificate on a Debug build does not enable compiler optimizations.
- Track details and comments use their existing viewports. Lyrics retain native
  overscroll/rebound instead of a custom 320 ms return animation. Sheet drag
  offsets settle with `SheetMotion`, carrying signed release velocity.

No horizontal edge handler is installed, so these changes do not intercept the
Android system back gesture. Popup back/escape handling remains in Qt Controls.
Desktop touch, touchpad and discrete wheel behavior is preserved.

## Verification

Build with `cmake --build --preset debug`. The `androidHeaderReservesSpace`
case in `plugin_tests` loads the real Android QML with a network-free model. It
covers header dismissal, short-pull return, scrolling inside a compact action
sheet, different release velocities and press-to-stop. Run it from `build/debug`
with `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software` so generated fixtures
and screenshots remain under `build/`.

A host test does not establish device frame pacing. On Android hardware verify
60/90/120/144 Hz where supported, diagonal gestures over home shelves/profile
tabs, slow/fast/reversed flicks, tap-to-stop without accidental activation,
playlist reordering, plugin views and system edge-back navigation. Check both
portrait and landscape, long lists, pagination and restored scroll positions.

## Direct manipulation audit (2026-11)

The remaining artificial movement was at the surface/navigation boundary:
mini-player dragging triggered `open()` after 45 px instead of revealing the
player, player release switched to a fixed-duration exit transition, and action
sheet rebound only stopped after DragHandler activation. Profile tabs used a
Behavior on the property owned by Flickable. People rows delayed presses 120 ms.

- **Player / mini-player:** the opening gesture owns the surface until release;
  its scene-space displacement writes the sheet offset directly. The modal grab
  is deferred while the dock gesture owns the pointer. Reversal and holding the
  finger do not start an animation. Dismissal keeps a baseline at activation so
  grabbing a partially settled sheet does not reset its position.
- **Bounded sheets:** `SheetMotion` carries signed `centroid.velocity.y` into an
  exact critically damped spring (a held pointer expires its last movement velocity
  after 100 ms), integrated by `FrameAnimation` using elapsed
  seconds. Projection chooses an endpoint; opening from the dock has its own
  threshold. It is intentionally not list inertia: sheets must settle open or
  closed. There is no duration/easing replay and no Behavior on the driven offset.
  A passive PointHandler stops motion on press before drag recognition. Physical
  closure completes before Popup/Drawer cleanup, avoiding a second exit animation.
  Ordinary click/back opening and closing retain their visual transitions.
- **Lists, grids, horizontal shelves and filter strips:** retain native Qt
  velocity estimation, deceleration, press-to-stop and axis arbitration. No global
  deceleration override, forced snap, custom touch timer or manual re-flick is
  added. Lyrics retain native overscroll. Pages are button navigation, not a
  gesture pager; their entrance decoration remains separate from scrolling.
- **Profile tabs:** only explicit selection reveals a tab with NumberAnimation;
  no Behavior observes `contentX`. Press stops this navigation animation. Layout
  updates do not reposition a viewport while dragging or flicking.
- **Comments, desktop lyrics and shelf arrow navigation:** passive presses stop
  programmatic following/navigation immediately, before movement starts. Native
  user scrolling still owns the viewport. Desktop discrete-wheel smoothing also
  stops on press; touchpad pixel deltas remain native.
- **Reordering:** PlaylistEditor reserves the right-side drag strip; its lifted
  row follows pointer coordinates, while displaced rows and release placement
  animate. The edge-scroll timer operates only during reordering and does not
  synthesize flick inertia. Desktop queue dragging retains its equivalent role.
- **Seeking and sliders:** position previews remain direct; seek commits on
  release. TrackProgress rejects predominantly vertical movement and permits
  Flickable to take the grab. These controls do not receive scroll inertia.
- **Other popups / plugin viewports:** settings, diagnostics, playlist preview,
  queue, saved searches, selection, people, exclusions and plugin panels retain
  native scroll containers. Plugin-provided QML may define its own input policy.

Qt references: [Flickable](https://doc.qt.io/qt-6.10/qml-qtquick-flickable.html),
[FrameAnimation](https://doc.qt.io/qt-6/qml-qtquick-frameanimation.html).
`SpringAnimation.velocity` limits maximum speed; it does not seed signed release
velocity, which is why the bounded-surface controller carries velocity explicitly.

`tst_sheetmotion.qml` checks signed release speed/reversal, interruption,
endpoints and refresh-rate-independent integration. The Android fixture additionally
checks that a dock swipe stays still under a held finger and follows reversal
1:1. These host checks do not replace touch latency/frame-pacing measurements on
physical Android hardware.

### Player overlay regression

The player moves the Popup's `y` as a single surface, with `unifiedSurface`
disabling separate background/content translations. It has no dimmer, so the
uncovered region shows the underlying page. Its background is one gradient
rectangle, without a redundant opaque fill. Gesture-driven enter/exit animations
have explicitly zero duration: relying on `Transition.enabled` alone allowed the
Controls popup transition to keep modifying `reveal` during direct manipulation,
adding a second displacement. Ordinary button/back transitions retain animation.
The Android fixture checks the background's scene position against the held drag
offset and verifies that content keeps its padding relative to that background.

### Wave exposure and metadata bounds

The Android home wave uses continuous exposure derived from player `y / height`,
so shader opacity follows both directions of the surface gesture, including a
mid-gesture reversal. Foreground changes use a short reversible opacity fade.
The shader stays allocated until opacity reaches zero; then its loader unloads,
phase updates pause, and audio analysis stops. Phase belongs to WavePanel rather
than the transient shader instance, preserving continuity when revealing it again.
Reduced-motion mode disables the timed foreground fade. `tst_wavevisibility.qml`
covers partial exposure, reversal, suspension, phase retention and reduced motion.

Player titles use plain text, fixed line spacing and a two-line layout height
limit. Android color-emoji fallback metrics must not inflate playerDetails,
collapse the cover or push transport controls outside the viewport. The Android
layout regression includes a long title with color emoji and skin-tone sequences.

Device investigation found a second failure beyond container sizing: Qt's emoji
fallback reported a 538 px shaped line at a 21 px font size, with a 425.75 px
ascent, while the primary font metrics were about 28 px high. Merely capping
Label height or switching Qt/native text rendering did not correct the baseline.
`TextGeometry` queries QTextLayout's actual per-line ascent. Android metadata
labels use it during line layout to align fallback runs to the primary font
baseline, keeping emoji and skin-tone sequences intact. Single-line labels keep
an explicit viewport width and clipping so long metadata cannot overpaint adjacent
buttons. This work runs on text/font/width layout changes, never per animation
frame. The regression also checks the normalized title baseline and an emoji
artist name. The affected track was verified on the connected Android device.

### Shared metadata labels

The first device fix covered player/dock labels but missed the independent
track-action header. `MetadataLabel.qml` centralizes fallback baseline/line-height
normalization for Android labels, including the actions header, track information,
people, playlist previews/editor, saved collections, recommendation exclusions and
comments. It preserves explicit single-line viewport widths, ellipsis and clipping;
multiline content keeps its requested line spacing. Desktop fallback normalization
is disabled. The Android fixture uses emoji in both action-menu header fields and
checks their height; `tst_metadatalabel.qml` checks ellipsis, multiline bounds and
plain-text interpretation. The actual actions drawer must be inspected on the
device in addition to the player when validating this regression.

## Swipe dismissal across mobile surfaces

`SwipeDismiss.qml` installs a header-only vertical handler outside the popup's
layout and scroll viewport. Its Translate is appended to the whole popup item,
so the background, controls and hit area follow one position. The shared bounded
SheetMotion carries recent signed velocity, stops on a new press, and restores
short pulls; it does not replace Flickable scrolling. Existing transforms and
button taps are preserved. Header coverage in playlist previews includes the
cover when the header fits; in compact layouts with a scrollable header it is
restricted to the top row.

The Android Sheet base enables dismissal for queue, queue actions, track details,
comments, lyrics, destinations, account and creation panels. Player and track
Drawer retain their existing specialized handlers; startup is excluded. Shared
playlist/mix/station previews, playlist menus, people, exclusions, queue-save and
playlist editor popups use the same handler in mobile mode, as does the Android
plugin-tools header. Editor dismissal calls its existing `dismiss()` guard and
is disabled while saving or reordering; a confirmation restores the underlying
surface instead of discarding edits. Desktop gestures remain unchanged.

`tst_swipedismiss.qml` covers direct movement, holding, reversal, short-pull
return, whole-surface positioning, list ownership and guarded dismissal. The
Android fixture also drives touch events on the actual playlist preview header,
then reopens it to check its track actions still work.

### Coherent click presentation

Mobile album/playlist/mix/station previews now rise from below the viewport over
320 ms while fading in; background, content and hit area share the same popup-item
transform. People and the Android Sheet base use that whole-surface presentation
path too. `SwipeDismiss.entranceOffset` composes click presentation with the
separate physical drag offset, rather than applying animations to pointer updates.
Reopening a closing popup keeps its current reveal/opacity. Explicit reduced
motion still produces immediate transitions. Recommendation/exclusion menus,
playlist menus and destination pickers now have opacity/scale transitions where
previously none were defined; their mobile header gestures are consistent with
other menus. Collection tools share header dismissal as well.

A passive handler spans the popup to stop settling on any new press; only its
header child recognizes dismissal drags. Header-button taps and native list
scrolls have dedicated tests. Centered surfaces travel far enough to clear the
viewport on dismissal, rather than stopping one card-height down the screen.
The production fixture checks an intermediate playlist entrance frame and verifies
that background and content remain aligned before exercising touch dismissal.

Mobile dismissal handlers accept touchscreen input only. This avoids a passive
handler over the header consuming mouse presses before Qt Controls buttons can
see them; external mice and desktop wheel input retain native routing. The
regression uses touch sequences for sheet gestures and verifies both mouse and
touch taps on a header button, plus a new body touch stopping an active settle.
