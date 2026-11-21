[Home](Home) › Troubleshooting

## Sign-in fails or does not survive a restart

Start with [SoundCloud Authentication](SoundCloud-Authentication#if-sign-in-fails). On Linux, unlock the Secret Service and restart Sange. A session cannot be saved when secure storage fails; there is no plaintext fallback. Retry authorization after fixing storage. Expired or revoked sessions need a new sign-in.

If a callback attempt expires, begin a fresh attempt. Authorization has a ten-minute timeout. Use **Retry sign-in** on the browser callback page when offered.

## Windows installer is blocked or disappears

1. Check whether the EXE is still in Downloads.
2. If it disappeared, inspect **Windows Security → Protection history** at the launch time.
3. If present, compare its SHA-256 with `SHA256SUMS` from the same release; see [Installation](Installation#verify-a-download).
4. If the hash differs, download again. If it matches, report the failure stage and Protection history event. On managed PCs, provide that information to your administrator.

The public 2.1.0 installer has no publisher certificate signature. See [Windows installation notes](https://github.com/sykia/sange/blob/main/docs/windows-install.md).

## An older version cannot download updates

Download the current package manually from [Releases](https://github.com/sykia/sange/releases/latest) and install over the existing app. Older binaries can reject the new download path after the repository rename. Preserve application data and do not uninstall first.

## Android will not install an update

Check Android version (9+), available storage and the package source. An update must use the installed app's signing certificate; a locally built APK can have a different certificate. Do not uninstall or clear data as a routine workaround if you want to preserve it. Record the exact Android error when reporting the issue.

## A track will not play

- Inspect its access label. Preview-only or blocked tracks are not full-playback candidates.
- Open the public track on SoundCloud to inspect its availability.
- Check whether other tracks play and record the app's error.
- Retry when a transient network error has cleared.

Sange does not silently substitute a preview. A stream failure alone does not establish a region or subscription restriction. **Data saver** and **Standard** choose among returned streams; neither unlocks unavailable audio. Disabling **Preload** stops speculative loading, while current playback still needs buffering and network access.

For Android background failures, include device/OS, whether the screen was off, the failure time and whether playback resumes after reopening. See [Android playback diagnostics](https://github.com/sykia/sange/blob/main/docs/android-playback-diagnostics.md) for developer investigation.

## Search cannot find a track in a collection

Typing filters loaded rows. Use the collection menu's whole-collection search to fetch additional pages; watch the incomplete/progress status and continue when prompted. Catalogue filters also depend on metadata supplied by SoundCloud. Saved searches do not automatically download a whole collection.

## Requests are delayed or return HTTP 429

HTTP 429 means the service is limiting requests. Sange observes server cooldowns and defers affected requests while keeping the interface responsive. It uses the server's retry/reset information when available; there is no fixed wait time that applies to every response.

Let the cooldown finish instead of repeatedly refreshing or signing in. App-wide cooldowns survive sign-out within the running process. A general API cooldown can also delay stream resolution and preloading; some narrower cooldowns affect only their request group.

Foreground reads may retry up to twice after rate limits, selected temporary network failures or HTTP 502/503/504. Writes do not automatically retry ambiguous failures. After a failed comment or playlist write, inspect the remote result before submitting again. Repeated action clicks do not bypass SoundCloud quotas. See [request coordination details](https://github.com/sykia/sange/blob/main/docs/api-quotas.md).

## Follow controls are unavailable or a subscription did not change

Follow controls wait for account subscription synchronization. Retry the initial load when the control offers it. A failed follow/unfollow request retains the last confirmed state; following your own account is not supported. See [artist following](https://github.com/sykia/sange/blob/main/docs/following.md).

## A track has no waveform or some information is missing

Track information shows only valid supplied fields. An unavailable or unsupported waveform image falls back to the normal progress line, which remains seekable. Missing metadata does not imply a deleted track, and Sange does not synthesize a waveform or infer a release date from the upload date. See [track information](https://github.com/sykia/sange/blob/main/docs/track-information.md).

## SoundCloud links stay in the browser

On desktop, install the package to register the custom protocol. Ordinary HTTP(S) clicks require the optional browser bridge; the package does not take over your browser's global HTTP(S) handler. The extension does not intercept address-bar navigation. On Android, select supported domains under the app's **Open by default** settings or use **Share → Sange**. Pasting the resource URL into Sange's search is another option. See [link setup](Installation#opening-soundcloud-links).

## A downloaded update will not start installation

Read the update error in Settings. A missing, incomplete or changed file blocks installation; download it again. Linux installation requires `pkexec`, the matching package manager and a working authorization agent. On Android, allow APK installation from Sange if prompted, then press **Install now** again. Opening the system installer does not confirm that installation finished. See [update instructions](Installation#updating-from-settings).

## A playlist save is rejected

Editing requires ownership and a complete remote playlist. Reload if another client changed the playlist. The draft is retained on detected conflicts or incomplete reads. The resulting playlist must fit the client's 500-track limit. Retry after resolving the displayed error; concurrent edits from another client can still race a save.

## A plugin is installed but inactive

Check the plugin card's reason: disabled state, missing permissions, missing/incompatible dependency, host version mismatch or runtime error. New plugins start disabled. Dependencies are not installed automatically. An old `requiresHost.maxExclusive` can exclude Sange 2.0.0.

After a quota error, disable and re-enable the plugin. **Clear log** only clears messages. For QML errors, inspect the card and use **Reload code** after installing a corrected package.

### Sange hangs after enabling a QML extension

Fully exit Sange, then launch `sange --no-plugin-code` (or `build/debug/scrip --no-plugin-code` from a source build). Windows uses `sange.exe`. Disable or remove the extension. If a hung process cannot exit normally, end it before launching recovery. Android currently has no in-app pre-start recovery gesture. See [Plugin System](Plugin-System).

## Closing the desktop window keeps music playing

When a system tray is available, closing the window hides it. Restore Sange from its tray icon or choose **Exit** in the tray menu to stop the application.

## Discord activity is missing

Enable the optional integration in Sange and activity sharing in Discord. The Discord desktop client must be running locally. Clearing a custom Application ID restores the shared default. Android does not support this integration. Some presentation details and buttons depend on Discord's client and viewer. See [Discord guide](https://github.com/sykia/sange/blob/main/docs/discord-presence.md).

## Report a problem

Open [GitHub Issues](https://github.com/sykia/sange/issues) and include:

- Sange version, OS/version and installation source.
- Steps to reproduce, expected result and actual result.
- The exact visible error; a screenshot when useful.
- Whether it affects one track/account/plugin or several.

Current source also includes **Settings → Found a bug? → Copy latest log / Open GitHub Issues**. If your build has these controls, copy and review the report before pasting it into the issue. Nothing is uploaded automatically. Local diagnostics are filtered, but arbitrary third-party messages may still contain personal information. Never post tokens, callback URLs, secrets or private account data.

---
[FAQ →](FAQ) · [SoundCloud Authentication](SoundCloud-Authentication)
