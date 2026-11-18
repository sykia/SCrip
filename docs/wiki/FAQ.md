[Home](Home) › FAQ

## Is Sange an official SoundCloud app?

No. It is an independent player using the official SoundCloud API.

## Which platforms are supported?

Windows, Linux and Android. Published 2.0.0 packages cover Windows x64, Debian 13 amd64, Arch Linux x86-64 and Android 9+. macOS is explicitly unsupported by the build. See [Installation](Installation).

## Do I need my own API keys?

No for the default public mode. Owners of registered SoundCloud applications can optionally use personal local mode. See [authentication](SoundCloud-Authentication).

## Does Sange receive my SoundCloud password?

Sign-in occurs on SoundCloud's page in a regular browser. Sange uses OAuth tokens. In public mode, the project Worker handles token exchanges; credentials are stored through the device's secure-storage mechanism. See [authentication](SoundCloud-Authentication).

## Can Sange play every SoundCloud track?

No. Access depends on what SoundCloud returns for the account and track. Preview-only and blocked tracks are distinguished; Sange does not bypass access restrictions or silently substitute previews.

## Is preloading an offline library?

No. It prepares upcoming playback and temporarily caches HLS data. It does not provide a persistent, managed offline collection.

## Does Data saver guarantee a particular bitrate?

No. It changes the preferred order among full-stream candidates returned by SoundCloud. The available formats can vary.

## How are recommendations made?

Sange requests SoundCloud related tracks and orders candidates locally using liked sources, duplicate/artist balancing, preferences and exclusions. It does not train its own recommendation model. See [Features](Features#discovery).

## Why are some collection search results missing?

Typing filters only loaded rows. Whole-collection search is a separate paginated action. Sorting also covers only the loaded portion until more rows arrive.

## Are plugins sandboxed?

Declarative plugins use a restricted command API. QML extensions require `code.execute` and run with full application authority, without a sandbox. Packages are unsigned. See [Plugin System](Plugin-System).

## Will a sleep-timer plugin pause at an exact time on Android?

Not reliably during device sleep. Plugin timers use the event loop and may run after the app resumes. They do not survive restart, logout or disabling the plugin.

## Why do filenames and data still say SCrip or scrip?

Sange 2.0.0 changed the display name while preserving executable, package and storage identities for upgrades. Install over the old version without clearing data; Android also requires the same signing certificate.

## Why does the desktop app stay open when I close its window?

If a tray is available, closing hides the window. Use **Exit** in the tray menu to quit.

## Is Discord integration required?

No. It is optional, disabled by default and available only with a local desktop Discord client. Android has no compatible desktop IPC endpoint.

## Can I change or redistribute Sange?

The source is licensed under [GPL-3.0-only](https://github.com/sykia/sange/blob/main/LICENSE). Redistribution must meet that license and applicable third-party component licenses. See [third-party notices](https://github.com/sykia/sange/blob/main/THIRD_PARTY_NOTICES.md).

---
[Troubleshooting](Troubleshooting) · [Architecture →](Architecture)
