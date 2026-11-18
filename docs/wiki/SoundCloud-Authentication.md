[Home](Home) › SoundCloud Authentication

## Sign in

1. Start sign-in from Sange's account controls.
2. Complete authorization on the official SoundCloud page in your regular browser.
3. Let the browser return to the local callback and return to Sange.

Sange uses Authorization Code with PKCE S256 and a random state. The callback listener binds to `127.0.0.1`, rather than network interfaces. Authorization waits for at most ten minutes and can be canceled. The browser flow supports SoundCloud's browser sign-in methods, including Google; Sange does not ask for your SoundCloud password.

## Public application mode

This is the default. You do not need to supply application keys.

The authorization code and PKCE verifier, or refresh token, go over HTTPS to the project's authentication Worker. The Worker supplies application credentials and calls SoundCloud's token endpoint. The configured Worker is `scrip-auth.scrip-desktop-aca3d9.workers.dev`; its callback URI is `http://127.0.0.1:43821/callback`.

Profiles, collections and streams are fetched directly from SoundCloud. OAuth authorization headers are not forwarded to audio CDNs. The session is stored on your device.

## Where credentials are stored

| Platform | Storage mechanism |
| --- | --- |
| Windows | User-scoped DPAPI encryption |
| Linux | Secret Service through libsecret |
| Android | AES-256-GCM encrypted files with a non-exportable Android Keystore key |

There is no plaintext fallback. Linux needs a running, unlocked Secret Service. Hardware-backed Android key protection depends on the device. These mechanisms do not isolate the app from malicious code running with the user's authority; [trusted QML plugins](Plugin-System#trusted-qml-extensions) execute with application authority.

Expired tokens are refreshed through a serialized flow. Refreshed credentials must be saved successfully before API requests continue. A storage failure after server-side token rotation can require signing in again.

## If sign-in fails

| Symptom | What to do |
| --- | --- |
| Browser did not open | Check that a browser is configured; start sign-in again. |
| Authorization denied or exchange failed | Use **Retry sign-in** on the callback page when offered, or restart sign-in in Sange. |
| Callback timed out | Start a new attempt; the old attempt expires after ten minutes. |
| Linux secure storage unavailable | Start/unlock the Secret Service, then restart Sange. |
| Session cannot be saved | Resolve the secure-storage error, then retry. There is no plaintext fallback. |
| Session expired or was revoked | Sign in again. |

The browser retry starts fresh PKCE/state and is single-use. An invalid callback state cannot authorize an exchange. Do not share callback URLs, codes, tokens or application secrets in a bug report.

## Sign out

Sign-out clears local authentication and account state immediately, independently of network access. Sange also attempts remote access revocation. If remote revocation fails, follow the app's message and remove access through SoundCloud's connected-app settings. Local sign-out does not sign you out of the SoundCloud website in your browser.

## Personal local mode

For owners of a registered SoundCloud application, `SCRIP_AUTH_MODE=local` bypasses the Worker and exchanges tokens directly with SoundCloud. Set your own `SOUNDCLOUD_CLIENT_ID` and `SOUNDCLOUD_CLIENT_SECRET` outside the repository before launching. Environment values override saved application parameters. This is an advanced mode, not a requirement for regular installation.

Implementation: [SoundCloud controller](https://github.com/sykia/sange/blob/main/src/soundcloud.cpp), [secure store](https://github.com/sykia/sange/blob/main/src/secretstore.cpp), [authentication Worker](https://github.com/sykia/sange/tree/main/worker).

---
[Troubleshooting →](Troubleshooting) · [FAQ](FAQ)
