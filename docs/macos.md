# macOS builds (2.0.1 preparation)

The macOS port uses the desktop Qt Quick interface with the Basic controls style, macOS Keychain for OAuth records and MediaPlayer for Now Playing and transport commands. It does not require libsecret or D-Bus. Storage keeps the existing `Qsae/SCrip` identity. Closing the window keeps playback running; the menu bar provides Quit (Command-Q) and Open Sange. The desktop Discord IPC client also supports native macOS Discord sockets.

## Build and package

Use macOS 14.4 or newer, Xcode command-line tools, CMake 3.21+, Ninja and Qt 6.12.0 with Quick, Widgets, Network, Multimedia and ShaderTools (plus Test/QuickTest for tests). Qt's package must contain the requested architecture.

```sh
cmake -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4
cmake --build build/macos --parallel 3
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ctest --test-dir build/macos --output-on-failure
bash packaging/macos/package.sh build/macos arm64
```

Use `x86_64` throughout for Intel. `macdeployqt` must be on PATH. The packaging script copies the app, QML imports, Qt libraries/plugins and legal notices, checks Mach-O architecture/dependency paths and writes a binary inventory, then produces an ad-hoc-signed DMG and SHA-256 file under `build/macos/package/`. This is a testing package, without Developer ID or Apple notarization. Gatekeeper may require explicit user approval. Do not instruct users to disable Gatekeeper globally.

The `macos-build.yml` workflow builds and tests on separate Apple Silicon and Intel runners and uploads packages, logs and fixture UI screenshots as Actions artifacts. It does not publish a GitHub release. The deployed app receives a ten-second offscreen startup check; this does not validate actual sound or visual appearance. Tests use fixture credentials; CI runs the native Keychain round-trip test in a disposable keychain. On a local Mac, run `build/macos/secretstore_tests roundTrip` explicitly; it can prompt for Keychain access.

The updater selects the DMG matching the application architecture, verifies its SHA-256 and opens it in Finder. Replacing the app in Applications remains a user action; the running bundle is not overwritten automatically.

## Release validation

Before releasing 2.0.1, test the downloaded DMG on a Mac without Qt installed:

- Installation from the DMG to Applications and first launch through Finder; architecture and minimum OS compatibility.
- Sign-in in the default browser, callback, restart with saved session, sign-out and Keychain-denied/locked behavior.
- Actual audio, pause/resume, next/previous, queue, seek/HLS, quality switching, output device changes and sleep/wake.
- Now Playing metadata, media keys, seeking, paused queue navigation and no stale metadata after quit.
- Retina layout, resizing, fullscreen, dialogs, clipboard, Command-F, Command-Q, closing/restoring from menu bar and Dock.
- `scrip:`/`soundcloud:` links, second-instance forwarding and Discord presence if enabled.
- Diagnostics copying/export filtering and persistent logs after force quit.
- An inventory of the exact bundled Qt/FFmpeg libraries, license texts and matching source/build materials, integrated into the release compliance audit.
- Developer ID signing and notarization for friction-free public distribution, including stapling and testing the final downloaded artifact.

A successful CI run establishes compilation, automated tests, packaging and offscreen startup only. Hardware playback and interactive checks remain a separate release gate.

References: [Qt deployment](https://doc.qt.io/qt-6/macos-deployment.html), [Apple Keychain search](https://developer.apple.com/documentation/security/searching-for-keychain-items), [Apple macOS distribution](https://developer.apple.com/macos/distribution/).
