# Changelog

## 1.6.1 — November 12, 2026

- Fixed desktop update downloads when the HTTP response size is unknown.
- Request raw package bytes without transport compression; actual size and SHA-256 verification remain mandatory.
- Reset progress and readiness on retry; write failures block installation.
- Windows, Debian and Arch Linux release only. Android remains at 1.6.0.

## 1.6.0 — November 10, 2026

- Extensive playback and interface stability improvements on desktop and Android.
- Fixed rapid track switching: stale request cancellation, next-track preparation and stream caching now follow the current queue.
- Improved light and dark themes, smooth appearance transitions and plugin panel rendering.
- Updated Android icons, fixed playback service behavior and patched FFmpeg HLS seeking.
- Added checks for stream caching, rapid switching and the Android player lifecycle.

## 1.5.3 — Android

- Prepare the next track without silent playback; keep the service running during background buffering and temporary audio-focus loss.
- Save the queue when it changes and the position during playback; resume from the saved position after signing in again.
- Added a loading indicator, player swipes, back navigation and artist-profile navigation. Fixed likes on reposts and interface colors.
- Simplified the plugin screen and the message shown when no update is available for the platform.

## 1.5.0

- Protect the SoundCloud session with system storage; fixed sign-in cancellation, account switching and token-refresh error handling.
- Restore queue and playback position for the same account. Fixed Android player restoration and queue behavior.
- Optimized lists and network requests; fixed the order of Home sections.
- Expanded the plugin API; fixed Android plugin imports and update installation.
- Improved Android system media controls and updating existing Windows/Linux installations.

## 1.4.2

- Replaced the close button in Android's system player card with a like button: an outline heart adds a like, a filled heart removes it. State stays synchronized with the app.
- Network errors and incomplete update downloads are no longer reported as SHA-256 mismatches. Checksum verification remains in place; retrying does not start a parallel download.

## 1.4.1 — November 8, 2026 (Android)

- Keep the Android foreground service and wake lock during track changes and buffering, separately from user pause. Removed a 30-second timer that could stop the service during the next load.
- System controls support a separate full stop that releases players.
- Next-stream preloading automatically retains its buffer in memory; removed a deferred timer that could pause the player after switching.
- Removed the offline library, music downloads and local SoundCloud playback.
- Both players update the output device when headphones connect or disconnect.
- Android hands APKs to the installer through FileProvider with the APK MIME type and read permission; packages are stored outside the clearable cache.

Development checks: desktop and universal APK builds, CTest (2/2), Android 15 instrumentation covering foreground service during transitions and buffering beyond 30 seconds, pause, resume, stop and rapid preparation cancellation. The author tested the corrected build on a phone: background playback and headphones work.

Before release, test upgrading a previous APK signed with the same key on a real Android device, including installation permission. Check wired, Bluetooth and USB headphones: connection before launch and during playback, disconnection, next-track transitions and crossfade.

## 1.4.0 — November 7, 2026

### Playback and library

- Removed the gap below the right queue panel and fixed sizing of the player's right section.
- Added an optional three-second crossfade with next-track preloading on desktop and Android.
- Start Android's media service during track preparation while the app is still open, helping prevent playback from stopping when the app enters the background.
- Android saves available SoundCloud originals for offline listening. Added individual downloads, batch downloads of all downloadable likes, an offline file list and deletion.
- Offline downloads use the highest-quality original SoundCloud permits the author to make downloadable.

### Updates

- Check GitHub Releases at startup, download a suitable package automatically, verify SHA-256 and start installation. Windows and Linux restart the app after installation; Android requires system APK confirmation.
- Increased the app version and Android versionCode to 1.4.0 / 10400.

## 1.3.0 — November 7, 2026

Changes since published version 1.0.0, including mobile development from the working tree. Separate public releases 1.1.0 and 1.2.0 were not published.

### Android

- Dedicated Qt Quick interface: bottom navigation, Home with personal collections, feed, library and search.
- Mini player and full-screen player with artwork, seeking, likes, queue, repeat and shuffle.
- SoundCloud sign-in through the system browser.
- Background playback through a foreground service, notification and lock-screen controls through MediaSession.
- Home-screen widget with artwork and playback controls.
- Audio-focus loss and headphone disconnection handling.
- Next-track preparation, including shuffle mode.
- HTTPS through bundled OpenSSL; Android 9+ and ARM64, ARMv7, x86-64 in one APK.

### Desktop and packaging

- Fetch the next stream URL ahead of time; background errors do not interrupt the current track.
- Separate desktop and mobile initialization; mobile preview mode on desktop.
- Windows packager supports application and installer signing through Windows SDK when a publisher certificate is available.
- CI verifies the published installer's SHA-256, scans it with Microsoft Defender, installs it and waits for the window to open.
- Updated README, screenshots and download information.
- Aligned version 1.3.0 across the app, CMake, APK, Windows installer and Linux packages.

### Retained from 1.0.0

Personal mixes, artist stations, new releases, likes and playlists; a draggable queue with **Play next**; desktop tray, themes and plugins. These features are not new in 1.3.0.
