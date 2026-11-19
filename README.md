<p align="center">
  <img src="docs/logo.svg" width="112" height="112" alt="Sange logo">
</p>

<h1 align="center">Sange</h1>

<p align="center">Your SoundCloud music, in a dedicated player for macOS, Windows, Linux and Android.<br>Discover personal mixes, explore artist stations and keep your favorites close.</p>

<p align="center">
  <a href="https://github.com/sykia/sange/releases/download/v2.1.0/Sange-2.1.0-macOS-arm64.dmg"><img src="https://img.shields.io/badge/macOS-Apple_Silicon-414650?style=for-the-badge" alt="Download for macOS Apple Silicon"></a>
  <a href="https://github.com/sykia/sange/releases/download/v2.1.0/Sange-2.1.0-macOS-x86_64.dmg"><img src="https://img.shields.io/badge/macOS-Intel-414650?style=for-the-badge" alt="Download for macOS Intel"></a>
  <a href="https://github.com/sykia/sange/releases/download/v2.1.0/Sange-2.1.0-Windows-x64.exe"><img src="https://img.shields.io/badge/Windows_10%2F11-EXE-414650?style=for-the-badge" alt="Download for Windows 10 and 11 x64"></a>
  <a href="https://github.com/sykia/sange/releases/download/v2.1.0/Sange-2.1.0-debian13-amd64.deb"><img src="https://img.shields.io/badge/Debian_13-DEB-95445F?style=for-the-badge" alt="Download for Debian 13 x86-64"></a>
  <a href="https://github.com/sykia/sange/releases/download/v2.1.0/scrip-2.1.0-1-x86_64.pkg.tar.zst"><img src="https://img.shields.io/badge/Arch_Linux-PKG-39738D?style=for-the-badge" alt="Download for Arch Linux x86-64"></a>
  <a href="https://github.com/sykia/sange/releases/download/v2.1.0/Sange-2.1.0-Android-universal.apk"><img src="https://img.shields.io/badge/Android_9%2B-APK-3D8C68?style=for-the-badge" alt="Download for Android 9 and later"></a>
</p>

<p align="center">
  <a href="CHANGELOG.md">Changelog</a> ·
  <a href="https://github.com/sykia/sange/releases/latest">Release files &amp; SHA-256</a> ·
  <a href="docs/plugins.md">Plugins</a> ·
  <a href="https://github.com/sykia/sange/wiki">Wiki</a>
</p>

<p align="center">
  <a href="https://github.com/sykia/sange/releases/latest"><img src="https://img.shields.io/github/v/release/sykia/sange?label=Sange&amp;color=3D8C68" alt="Latest Sange release"></a>
  <a href="https://github.com/sykia/sange/actions/workflows/linux-cpp.yml"><img src="https://github.com/sykia/sange/actions/workflows/linux-cpp.yml/badge.svg" alt="Linux build and tests"></a>
  <a href="https://github.com/sykia/sange/actions/workflows/licenses.yml"><img src="https://github.com/sykia/sange/actions/workflows/licenses.yml/badge.svg" alt="Licensing and release checks"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--only-414650" alt="License: GPL-3.0-only"></a>
</p>

## Get started

Install the package for your platform and sign in through the official SoundCloud page in your browser. Sange uses the official SoundCloud API.

**2.1.0** adds persistent English/Russian interface selection, playback recovery, media-session improvements and fixes for search, lyrics, recommendations and streaming. Packages are available for Windows, Debian 13, Arch Linux, Android and macOS. See the [release notes](release-notes/2.1.0.md). macOS requires 14.4+; disk images are not Apple-notarized.

Upgrading from SCrip? Install over your existing application to keep your settings and music data. See the [upgrade notes](release-notes/2.0.0.md) if an older build cannot download updates.

## Your music, your rhythm

- **Discover:** start a continuous personal wave, adjust the balance between familiar music and discoveries, explore mixes and artist stations.
- **Explore:** search tracks, playlists and people; browse your feed, likes, recent tracks and profiles.
- **Organize:** edit playlists, reorder the queue, choose **Play next**, remove duplicates or save the queue as a playlist.
- **Keep listening:** use system media controls, desktop shortcuts and Android background playback.
- **Make it yours:** customize the appearance, add [plugins](docs/plugins.md) and enable optional [Discord Rich Presence](docs/discord-presence.md) on desktop.

## On desktop

Home brings the personal wave and mixes together. The feed keeps music from your subscriptions within reach, with the player and queue alongside it.

![Sange desktop Home with personal wave, mixes and playback queue](docs/screenshots/home-frame.svg)

![Sange desktop feed with track artwork and playback queue](docs/screenshots/feed-frame.svg)

## On Android

A dedicated touch interface with discovery, search, a full-screen player, background playback and system media controls.

<p align="center">
  <img src="docs/screenshots/android-home-frame.svg" width="40%" alt="Sange Home on Android with personal wave and artist stations">
  <img src="docs/screenshots/android-search-frame.svg" width="40%" alt="Sange track search on Android">
</p>

## Build from source

Sange uses **C++20, Qt Quick and CMake**. For a desktop build, install CMake 3.21+, Ninja, a C++20 compiler and Qt 6.5+ with Quick, Network, Multimedia, ShaderTools and Widgets, plus Test and QuickTest for the default test-enabled build. Linux also requires Qt DBus, pkg-config and libsecret development files (`libsecret-1-dev` on Debian/Ubuntu or `libsecret` on Arch), plus a running, unlocked Secret Service for sign-in.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

Run `build/debug/scrip` on Linux or `build/debug/scrip.exe` on Windows. For platform-specific setup, see [macOS](https://github.com/sykia/sange/blob/v2.0.1/docs/macos.md) or the [Android build script](scripts/build_android.sh). macOS release builds use Qt 6.12.0.

## Documentation

[Wiki](https://github.com/sykia/sange/wiki) · [Plugins](docs/plugins.md) · [Playback](docs/system-media-and-streaming.md) · [Diagnostics](https://github.com/sykia/sange/blob/v2.0.1/docs/diagnostics.md) · [Windows troubleshooting](docs/windows-install.md)

## License

Sange is independent software and is not an official SoundCloud product.

Copyright (c) 2026 sykia. The source code is licensed under **GNU GPL v3.0 only (GPL-3.0-only)**. You may use, study, modify and redistribute it under the terms of [LICENSE](LICENSE).

Third-party components retain their own licenses; see [Third-party notices](THIRD_PARTY_NOTICES.md). The [name and original logo](TRADEMARKS.md) may be copied and modified under the GPL without additional restrictions. [Release licensing materials](licensing/README.md) document the distribution process.
