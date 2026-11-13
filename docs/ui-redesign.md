# SCrip appearance system

The interface combines quiet, opaque content surfaces with optical glass on floating navigation, player controls and overlays. Dark and light appearances share the same geometry and interaction model. No new font or image dependency is required.

## Components and compatibility

- `qml/design/Theme.qml`: graphite and cool neutral palettes, readable accent text, error colors, radii, spacing, typography and reduced-motion timings.
- `qml/components/GlassSurface.qml`: bounded backdrop capture, optical shader and opaque fallback.
- `AppCheckBox`, `AppSwitch`, `AppComboBox` and `Section`: shared controls and settings sections. Focus, disabled states, keyboard interaction and native QML accessibility remain available.
- Desktop: revised navigation and window controls, floating playback dock, settings sections, artwork placeholders, themed catalog/profile/library/playlist/queue surfaces and an expanded player.
- Android: shared palettes and controls, glass navigation and mini-player, themed player, sheets, settings and plugin views. The player adapts its artwork placement in landscape. System bar icon appearance follows the selected theme through a small UI-only JNI method.
- Shared plugin panels, management and review dialogs use the same surfaces. Native file/color dialogs retain platform behavior.

Settings categories and stored theme definitions are unchanged. User accents, theme tint, artwork radius, avatar roundness, panel radius, plugin appearance precedence, motion and scrolling preferences remain supported. The only added persisted setting is `glassQuality` (0 minimal, 1 balanced, 2 high). Switching appearance updates bindings without recreating the player or changing navigation.

SoundCloud networking, OAuth, secret storage, playback, queue, crossfade, caching, updates and plugin API logic were not rewritten. Pre-existing working-tree changes in those areas were retained.

## Optical material

`GlassSurface` receives an explicit backdrop item that excludes the glass and its foreground. It rejects ancestor/self captures. Desktop overlays sample the shell, the dock samples a content layer that includes the page and inspector but excludes the dock, and mobile overlays/navigation sample the body. The capture rectangle is mapped into source coordinates. Popup overlays are excluded from their own source. Nested player sheets sample the visible player content rather than the covered page.

A `ShaderEffectSource` captures only the panel region at 40% (balanced) or 75% (high) resolution. `glass.frag` uses 17 weighted texture samples, a rounded signed-distance mask, approximately 1.5 physical pixels of edge refraction, a neutral surface tint and restrained edge highlights. Text and controls render after the optical pass. Transparent source pixels are composited against the material color before tinting; output uses premultiplied alpha.

Shaders are packaged with `qt_add_shaders`/qsb. The implementation uses Qt Quick's RHI pipeline; no compositor blur, custom render thread, CPU readback or additional graphics dependency is required. Captures follow source dirtiness. Invisible effects release their source and minimized windows stop live updates. Lists retain virtualization and never allocate a blur per delegate. Minimal mode and Qt Quick's software backend use an opaque material.

The original `WaveCard.qml` and `wave.frag` are retained. Their shape, audio inputs, timing, viewport/minimize suspension and interaction have not been replaced. Regression tests still cover wave controls and suspension.

Qt references: [ShaderEffectSource](https://doc.qt.io/qt-6/qml-qtquick-shadereffectsource.html), [ShaderEffect](https://doc.qt.io/qt-6/qml-qtquick-shadereffect.html).

## Validation and evidence

Generated artifacts and logs belong in `build/`:

- `build/debug/`: preset build, CTest log and software-rendered UI screenshots.
- `build/visual-gpu/`: desktop screenshots and OpenGL test log from NVIDIA GeForce RTX 4070. Tests exercise home in both appearances, settings, search/catalog, profiles, expanded player, library, playlists, queue, narrow windows and virtualized collections up to 5,000 tracks.
- `build/mobile-gpu/`: the actual mobile QML rendered on Linux with fixture data, including navigation, both player appearances, settings and swipe/back behavior. These are UI fixtures, not an authenticated Android session.
- `build/android-redesign/`: Qt 6.10.1 Android x86_64 Debug APK and native Android 35 emulator screenshots of welcome/settings in both appearances. A separate AVD protects the existing installation and its data.

The preset uses Qt 6.12 on Linux. CTest covers the existing playback/network/cache/plugin suites, artwork and wave UI tests, plus new palette contrast, extreme accent colors, reduced motion and recursive capture prevention checks. Native window controls are located by their semantic names rather than old pixel coordinates.

## Remaining validation limits

Windows runtime and physical Android devices have not been tested. Authenticated end-to-end OAuth and real account playback were not exercised in the clean Android emulator; fixture tests cover the associated UI paths. Android MediaSession/background playback was retained but needs physical-device regression testing. A single 3-second Linux sample on the restored, paused home screen with the original wave animating used 3.67% of one CPU core and approximately 396 MiB RSS (`build/debug/idle-sample.json`). This is not a comparative benchmark. No claim of measured 60 FPS, energy use or universal GPU compatibility is made.

The emulator's deprecated `swiftshader_indirect` mode produced geometry artifacts, including on ordinary Qt rectangles; host GPU mode rendered correctly. This remains an emulator/backend limitation. Both Vulkan and Direct3D require target-platform runtime validation. Performance strategies above describe implemented resource limits, not a completed cross-device benchmark.

This is an implemented UI update, not certification that every production scenario on every platform has passed.

## Changed implementation files

New files: `qml/design/Theme.qml`, `qml/components/{GlassSurface,AppCheckBox,AppSwitch,AppComboBox,Section}.qml`, `qml/shaders/glass.frag`, `tests/qml/tst_design.qml`, and this document.

Updated: `CMakeLists.txt` (QML/resource aliases, qsb and test resources), both `Main.qml` entry points, `Artwork.qml`, `android/RoundedImage.qml`, shared plugin views/actions, `src/androidmedia.{h,cpp}`, `packaging/android/src/org/scrip/app/SCripActivity.java`, and the existing desktop/mobile UI tests in `tests/tst_soundcloud.cpp` and `tests/tst_plugins.cpp`.

Pre-existing modified/untracked files, including the wave implementation, README and SoundCloud playback changes, remain in the working tree. No release, version bump or destructive Git operation was performed.
