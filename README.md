<p align="center">
  <img src="docs/logo.svg" width="112" height="112" alt="SCrip logo">
</p>

<h1 align="center">SCrip</h1>

<p align="center">Your SoundCloud music, in a dedicated player for Windows, Linux and Android.<br>Discover personal mixes, explore artist stations and keep your favorites close.</p>

<p align="center">
  <a href="https://github.com/sykia/SCrip/releases/download/v1.6.1/SCrip-1.6.1-Windows-x64.exe"><img src="https://img.shields.io/badge/Windows_10%2F11-EXE-414650?style=for-the-badge" alt="Download for Windows 10 and 11 x64"></a>
  <a href="https://github.com/sykia/SCrip/releases/download/v1.6.1/SCrip-1.6.1-debian13-amd64.deb"><img src="https://img.shields.io/badge/Debian_13-DEB-95445F?style=for-the-badge" alt="Download for Debian 13 x86-64"></a>
  <a href="https://github.com/sykia/SCrip/releases/download/v1.6.1/scrip-1.6.1-1-x86_64.pkg.tar.zst"><img src="https://img.shields.io/badge/Arch_Linux-PKG-39738D?style=for-the-badge" alt="Download for Arch Linux x86-64"></a>
  <a href="https://github.com/sykia/SCrip/releases/download/v1.6.0/SCrip-1.6.0-Android-universal.apk"><img src="https://img.shields.io/badge/Android_9%2B-APK-3D8C68?style=for-the-badge" alt="Download for Android 9 and later"></a>
</p>

<p align="center">
  <a href="CHANGELOG.md">Changelog</a> ·
  <a href="https://github.com/sykia/SCrip/releases/tag/v1.6.1">Release files &amp; SHA-256</a> ·
  <a href="docs/plugins.md">Plugins</a>
</p>

## Get started

Install the package for your platform and sign in through the official SoundCloud page in your browser. SCrip uses the official SoundCloud API.

Desktop packages are at **1.6.1**; Android is at **1.6.0**. The universal APK supports ARM64, ARMv7 and x86-64. Install it over your existing release to keep your local data.

## Find your next favorite

Start a personal mix from Home, browse its track list and save a discovery to your likes or a playlist. Open an artist station to explore related music, or catch up with new releases.

![SCrip Home with personal mixes and track lists](docs/screenshots/home.png)

## Make the queue your own

Browse your favorites by artwork and open an artist's profile from their name. Drag tracks into order or choose **Play next**. On desktop, minimize SCrip to the system tray to control playback and like tracks from the icon menu.

![SCrip library with favorite tracks](docs/screenshots/library.png)

## Take your music with you

Android includes a dedicated touch interface, a full-screen player, background playback and system media controls.

<p align="center">
  <img src="docs/screenshots/android-home-frame.svg" width="32%" alt="SCrip Home on Android">
  <img src="docs/screenshots/android-player-frame.svg" width="32%" alt="SCrip player on Android">
</p>

## Build from source

SCrip uses **C++20, Qt Quick and CMake**. For a desktop build, install CMake 3.21+, Ninja, a C++20 compiler and Qt 6.5+ with Quick, Network, Multimedia, Widgets, Test and QuickTest. Linux also requires pkg-config and libsecret development files (`libsecret-1-dev` on Debian/Ubuntu or `libsecret` on Arch), plus a running, unlocked Secret Service for sign-in.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

Run `build/debug/scrip` on Linux or `build/debug/scrip.exe` on Windows. Build output stays in `build/`. The desktop interface is in `qml/Main.qml`, the Android interface in `qml/android/Main.qml`, and the entry point in `src/main.cpp`.

See [SoundCloud integration and secure storage](docs/soundcloud-security.md), the [plugin guide](docs/plugins.md), the [Plugin API reference](docs/plugin-api-v2-reference.md) and [Android playback diagnostics](docs/android-playback-diagnostics.md) for technical details. For installer issues, see [Windows troubleshooting](docs/windows-install.md).

## License

SCrip is independent software and is not an official SoundCloud product.

Copyright (c) 2026 sykia. The source code is licensed under **GNU GPL v3.0 only**. You may use, study, modify and redistribute it under the terms of [LICENSE](LICENSE).

Third-party components retain their own licenses; see [Third-party notices](THIRD_PARTY_NOTICES.md). The [name and original logo](TRADEMARKS.md) may be copied and modified under the GPL without additional restrictions. [Release licensing materials](licensing/README.md) document the distribution process.
