# Interface motion

Desktop and Android use the window's `motion` duration as their timing source.
Desktop retains the existing 120–500 ms preference; Android uses 280 ms.
Disabling interface motion sets the duration to zero. Press compression is also
removed, while feedback colors and keyboard focus remain available.

`InteractionFeedback.qml` provides a lightweight, input-transparent highlight
for mouse hover, touch/keyboard presses, disabled states and keyboard focus.
It uses a HoverHandler without taking a pointer grab; it does not replace the
control's click, drag or accessibility behavior. Press feedback arrives faster
than release feedback. The overlay uses no shaders, textures or offscreen layers.

`SurfaceTransition.qml` provides paired opacity and scale transitions for desktop
sheets, track details, custom lyrics, the queue and plugin tools. Hidden endpoints are initialized once; interrupted transitions continue from
their current opacity and scale instead of snapping to a fixed start value.
Android moves sheet backgrounds and contents together through the full sheet
height while preserving the bottom anchor. The full-screen player follows a
downward drag, returns after a short drag, and continues closing from the release
position after a deliberate swipe.

Theme colors interpolate with a subtle veil instead of a fully black frame.
Disabling motion during a theme change commits the requested theme and clears the
veil. Collection action overlays fade without removing their hit targets mid-fade;
they stop accepting input once mostly transparent. Slider handles fade and enlarge
without animating their position, so seeking continues to follow the pointer.

Page entrances no longer restart while already running. Recycled grid delegates
appear directly instead of running individual slide/fade animations on top of
the page entrance. Hover changes color without enlarging text or oscillating at
pointer boundaries. Mouse-wheel animation uses continuous retargeting.

The motion tests cover pointer clicks, disabled/reduced feedback, opacity
continuity on reversal, and interrupted popup transitions.
Existing desktop screen tests cover virtualized collections and seeking; the Android
layout fixture loads the same shared components and checks sheet gestures. Native
Windows and Android device performance still requires testing on those platforms.

## Validation

- `cmake --build --preset debug` completed successfully.
- `soundcloud`, `plugins` and `artwork_ui` CTest targets passed after refinement.
- Android gesture coverage includes short-drag return and long-drag dismissal.
- Debug APKs built with Qt 6.10.1 for ARM64 and emulator x86_64.
- The x86_64 APK launched on the Android 35 emulator; home/search/navigation
  smoke checks produced no QML or Android runtime errors. The emulator's
  SwiftShader renderer showed visual artifacts; the host GPU renderer produced
  the expected image. This is not a physical-device frame-time measurement.

Sidebar navigation uses one neutral fill for selected, hover and press states.
It keeps text and geometry fixed, avoids stacked accent overlays, and reserves
the outline for keyboard focus. State changes retarget the current fill opacity.

## Interaction refinement

Shared surface buttons compress subtly on press, settle more slowly on release,
and interpolate enabled and text-color states. Profile statistics and tab backgrounds
blend interaction states; profile collection menus use reversible surface transitions.
Playback quality selection moves vertically in stacked phone layouts as well as
horizontally on desktop.

Search filters reveal existing content through an animated clipped viewport.
Closing disables fields immediately and moves keyboard focus out of the hidden
form. Drafts survive interrupted transitions. A rotating chevron follows the same
state, and zero-duration motion keeps expansion immediate.

Collection views animate model moves and displaced items without animating delegate
creation during scrolling. Android collection-tab changes share the page entrance;
seeking and touch scrolling remain directly controlled. Disabling motion during a
page entrance stops it and restores the final opacity and offset immediately.

Refinement validation: the QML suite passed 66 checks, including interrupted filter
reveal, preserved drafts, reduced motion, touch scrolling and seeking. Physical
Android and native Windows frame pacing still require device validation.
