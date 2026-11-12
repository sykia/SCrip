# SoundCloud integration and data storage

## Personal local mode

With `SCRIP_AUTH_MODE=local`, SCrip calls the official SoundCloud API directly. This mode is for the owner of a registered application using their own keys on their own computer. Secrets are not embedded in source code, QML or the executable.

OAuth access/refresh tokens, expiry time and the local Client Secret are stored only through the internal `SecretStore`, which is inaccessible to QML and the Plugin API:

- **Windows:** DPAPI `CryptProtectData` in the current user's context, without `CRYPTPROTECT_LOCAL_MACHINE`. Only an encrypted blob is saved atomically in `QStandardPaths::AppLocalDataLocation/auth/*.dpapi`; access inherits the profile ACL.
- **Linux:** Secret Service through system libsecret (`libsecret-1` dependency). Building requires pkg-config and libsecret headers (Debian/Ubuntu: `libsecret-1-dev`; Arch: `libsecret`). A running, unlocked service such as GNOME Keyring or compatible KWallet is required. Each operation has a 10-second timeout with GCancellable cancellation. On-disk and backup keyring encryption depends on system service settings: use a password-protected keyring.
- **Android (API 28+):** AES-256-GCM with a new random IV for every write; the key is created in Android Keystore and is non-exportable. The entry name is included in AAD. Encrypted files are saved atomically in private `noBackupFilesDir/oauth`. Hardware-backed key protection depends on the device; StrongBox is not required.

On first launch, old `GenericConfigLocation/SCrip/soundcloud.json` and `session.json` files are migrated in full, including an existing session. On Android this is private `files/settings/SCrip`; on Linux, usually `~/.config/SCrip`. Each entry is removed from its old location only after a successful write and read-back verification. If interrupted after writing, the system copy takes precedence and the next launch finishes deleting the old one. Corrupt JSON and symbolic links are not migrated. Read, write, verification or deletion failures are shown without system details or secret values. There is no plaintext JSON fallback. Incomplete migration may leave old files on disk: exclude them from archives. Restart SCrip after unlocking storage.

Refreshed OAuth tokens must be confirmed by storage before API requests continue. Save failures stop authorization with a message; the new session is not reported as saved. If the server has already rotated the refresh token but saving fails, signing in again may be necessary after restart. Transactions across the OAuth server and local storage cannot be atomic.

Logout removes the session from system storage and memory; deletion errors are not hidden. Local SoundCloud application parameters are retained separately from the session. Settings, library and plugin data are neither migrated nor deleted.

Android Backup and device transfer exclude the old OAuth directory through `fullBackupContent` and `dataExtractionRules`; `noBackupFilesDir` is excluded by the system. Other settings and plugin data retain their previous backup rules. The app cannot delete existing external backups. Windows backups retain only the DPAPI blob; protection of the complete profile and Windows/Linux system stores depends on OS and backup policies. Protection does not cover malicious processes running as the user or root.

Plugins v1/v2 remain declarative, without JavaScript execution, arbitrary file/network operations or OAuth access. The internal player snapshot contains only allowed API fields; profiles, stream URLs and arbitrary API response fields are not exposed to plugins. OAuth/API error bodies and storage exceptions are excluded from messages and logs. TLS, HTTPS allowlists and redirect restrictions remain in place.

System mechanism documentation: [DPAPI](https://learn.microsoft.com/en-us/windows/win32/SecCrypto/example-c-program-using-cryptprotectdata), [libsecret](https://gnome.pages.gitlab.gnome.org/libsecret/), [Android Keystore](https://developer.android.com/privacy-and-security/keystore), [Android Backup](https://developer.android.com/identity/data/autobackup).

`SOUNDCLOUD_CLIENT_ID` and `SOUNDCLOUD_CLIENT_SECRET` override saved parameters. When using environment variables, change them outside the app. Sign-in and settings screens do not display keys. The interface receives only a connection-readiness flag. Volume and library appearance settings contain no secrets and are saved in QSettings.

## Sign-in

- Authorization Code + PKCE S256; verifier and state are generated using the system RNG.
- The listener binds only to loopback `127.0.0.1`, not network interfaces.
- The `/callback` path and nonempty state for the current attempt are checked.
- Waiting is limited to ten minutes and can be canceled. Codes are not logged.
- In local mode, code exchange and token refresh use only `secure.soundcloud.com` over HTTPS.
- API requests use only `api.soundcloud.com`; `next_href` is validated.
- Refresh is serialized so a single-use refresh token is not consumed concurrently.
- Late search and old-session responses cannot replace newer results.
- OAuth headers are not forwarded to audio CDNs: the player receives a signed HTTPS URL.
- `/disconnect` revokes app access without ending the SoundCloud browser session.

The official page opens in a regular browser rather than Qt WebView. This allows SoundCloud's supported sign-in methods, including Google, without bypassing Google's restrictions on embedded user agents.

## Artwork loading

On the network used during testing, system resolution of `i1.sndcdn.com` returned an unreachable node. For images, the client queries the public name through Cloudflare DNS-over-HTTPS and uses its official `.cloudfront.net` CNAME. System DNS settings are unchanged. CDN certificate verification remains enabled; OAuth tokens and keys are sent to neither the DNS service nor the image server. If DNS-over-HTTPS is unavailable, the regular SoundCloud address remains the fallback. Requests have bounded timeouts. This is a separate QML image transport, not the API transport.

## Public version

By default, the client sends the code with its PKCE verifier or refresh token over HTTPS to `scrip-auth.scrip-desktop-aca3d9.workers.dev/oauth/token`. The Worker supplies application credentials and calls a fixed SoundCloud endpoint. The redirect URI is fixed to `http://127.0.0.1:43821/callback`. The session is stored on the user's computer. The Worker rate-limits exchanges by IP and provides no guest tokens. Profiles, collections and music are fetched directly.

Source and configuration are in `worker/`. To redeploy, run `npx wrangler@4 deploy -c worker/wrangler.jsonc`. Existing secrets are retained. After rotating the SoundCloud key, set the new value with `npx wrangler@4 secret put CLIENT_SECRET -c worker/wrangler.jsonc`. `GET /health` checks Worker availability and presence of its secrets.

## Repository

`.gitignore` excludes `build/`, local editor settings, caches, `.env`, keys and secret directories. Run `python3 scripts/check_secrets.py` to check the Git index without printing detected values. GitHub Actions also checks tracked files. This is a supporting check, not protection for arbitrary images or history.

## Secure storage stage validation

`cmake --build --preset debug` and `ctest --test-dir build/debug --output-on-failure` check OAuth, migration, read/write/verification/deletion failures, session restoration, secret non-disclosure in state/errors and plugin compatibility. Tests inject storage through the C++ constructor; there is no production switch to an unencrypted test store.

`tests/run-secretstore-linux.sh` checks real libsecret in an isolated D-Bus session and GNOME Keyring, plus failure when the service is unavailable. Requires `dbus-run-session` and `gnome-keyring-daemon`. The user's keyring is untouched. `secretstore_tests roundTrip` can also be run manually on Windows.

`tests/run-secretstore-android.sh` runs `tests/android/SecretVaultCheck.java` instrumentation in a separate test app/UID: write/read, distinct IVs, no plaintext secrets in files, rejection of ciphertext tampering or key loss, deletion and path traversal prevention. It does not use installed SCrip data.

For Android, set `ANDROID_HOME`, `JAVA_HOME` and, if necessary, `GRADLE_USER_HOME` and `SCRIP_GRADLE` (path to an existing Gradle/gradlew). Start a test emulator and run `tests/run-secretstore-android.sh`. The test APK builds offline from the existing cache. It is a separate `org.scrip.vaulttest` app, not SCrip.
