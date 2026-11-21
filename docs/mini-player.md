# Desktop mini player

Open/hide the mini player with the dock button, **Ctrl+Shift+M** while Sange has
focus, or **Mini player** in the tray menu. Escape closes the current settings
panel, then hides the widget. Closing the widget never stops playback.
Drag the header to move it. The window is fixed at **272 × 340 logical pixels**;
previously saved larger sizes are ignored. Linux gives it a separate desktop /
WM_CLASS identity so per-application geometry restoration does not confuse it
with the main window. No external desktop configuration is modified.

Playback controls and the seek bar appear over the square cover on hover or
keyboard focus. The footer has metadata, a like button without a counter, and
the playlist button. Sharing copies the SoundCloud link. Playlist selection
captures the current track; track advancement cannot change that selection.
**Create playlist / more** opens the full playlist picker in the main window.
Space toggles playback. Right-click opens in-window settings, without a native
context menu. The header pin and settings checkbox control always-on-top.

## Backdrops

Settings offer **Application theme**, **Clear blur** and **Dark blur**. The latter
two request native blur of other windows behind the widget. Clear blur uses a
near-transparent neutral surface; dark blur adds a black translucent overlay.
Neither mode samples the artwork or takes screenshots of other applications.
Artwork remains visible. Unsupported blur falls back to the application theme
and displays a capability message in settings rather than pretending to blur.

- **Windows:** `SetWindowPos(HWND_TOPMOST/HWND_NOTOPMOST)` applies stacking without
  activation. Windows 11 uses DWM Desktop Acrylic; a dynamically resolved
  composition API provides a Windows 10 fallback where available.
- **macOS:** `NSVisualEffectView` with behind-window blending supplies blur.
  A floating window level, all-Spaces and fullscreen-auxiliary behavior apply
  while pinned. Native effects remain active when another app has focus.
- **Linux X11:** Qt/EWMH KeepAbove supplies stacking. Optional KF6 WindowSystem
  supplies blur for supporting compositors (including KWin).
- **Hyprland (Wayland or XWayland):** asynchronous local IPC identifies only the
  mini player's PID and title, pins it across workspaces, and raises it on focus /
  workspace changes without taking focus. It requests blur permission for that
  window and checks the compositor's blur setting. Versions with Lua dispatchers
  and older dispatcher syntax are supported. No global settings are changed.
- **Other Wayland compositors:** optional KF6 WindowSystem requests blur where
  available. Standard xdg-shell has no universal always-on-top or backdrop-blur
  request; unsupported capabilities are reported explicitly. This implementation
  cannot promise those effects on every Linux compositor or over exclusive
  fullscreen / system-owned surfaces.

Android retains its separate UI and does not load this controller. Optional
Linux libraries are discovered by CMake; Windows and macOS do not depend on them.
Native Windows and macOS runtime validation is still required.

## Verification

`cmake --build --preset debug` is the required local build. Linux verification
uses an isolated profile, native compositor screenshots and a preview executable
under `build/`. It checks fixed dimensions, both backdrop modes, pin/unpin,
stacking after activating the main window, context clicks and close behavior.
No new permanent automated tests were added. Authenticated SoundCloud changes
are not exercised by the isolated preview.

Interactive development launches use a transient user service, independent of
the agent command session. Terminating a command session can otherwise send
SIGTERM to its child application without producing a crash dump.

## Native API references

- [Microsoft DWM backdrops](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type)
- [Apple behind-window blending](https://developer.apple.com/documentation/appkit/nsvisualeffectview/blendingmode-swift.enum/behindwindow)
- [KWindowEffects](https://api.kde.org/kwindoweffects-namespace.html)
- [Hyprland window rules](https://wiki.hypr.land/configuring/core/rules/window-rules/)
- [Wayland background effect protocol](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/staging/ext-background-effect/ext-background-effect-v1.xml)

The native-effect revision was visually verified on Linux/Hyprland with both
XWayland and native Wayland. Compositor captures confirmed clear and dark blur
of the window behind the mini player. In both runs the compositor reported
`SangeMiniPlayer`, pinned=true in blur modes, fixed 272 × 340 dimensions, and
pinned=false after disabling the pin. The main window was raised during the
check; the widget remained above it. Both previews exited normally.

## Artwork motion and interaction

**Artwork follows the music** enables a restrained bass-driven stretch and
rotation of the cover inside a rounded, masked viewport. It uses the existing
spectrum analyzer, with smoothed energy transitions. Playback pause, hidden
windows and settings panels stop the motion. The global animation preference
also disables it; the local preference is retained for later. The analyzer stays
active when either the track inspector or the animated mini player needs it.

Mini-player tooltips and its dock launcher respect the global tooltip preference.
Accessible button names remain available to assistive technology. Hold the
**Sange** text in the header to move the window. The return-to-app button hides
the mini player and restores / activates the main window. Rounded corners use
20 logical pixels for the window and 16 for the artwork; the native backdrop
shape follows the window rounding where supported.

A temporary synthetic-spectrum preview checked energy-driven movement, local and
global disabling, the rounded artwork mask and return-to-app behavior on Linux.
The hover check also verified tooltip visibility toggling false → true → false
with the global preference. Return-to-app restored a minimized main window and
hid the mini player. The final debug build and Linux preview completed normally.
