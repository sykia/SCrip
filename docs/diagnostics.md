# Local diagnostics and bug reports

On Linux, Windows, macOS and Android, Settings includes **Found a bug?** below the account controls and before Plugins. **Copy latest log** copies a diagnostic report; **Open GitHub Issues** opens a prefilled bug report at `https://github.com/sykia/sange/issues/new`. Paste the copied log into the report and describe the problem and reproduction steps. Nothing is uploaded automatically; signing into GitHub and submitting the issue happen in the browser.

## Storage and contents

Logs use Qt's per-user `QStandardPaths::AppLocalDataLocation` under `logs/`, outside the repository. On Android this is app-private storage. The active file is `current.log`, with two rotating archives (`previous-1.log`, `previous-2.log`). Each file rotates at 512 KiB, with at most one additional bounded entry. Files receive owner-only permissions where supported. Reports contain the latest 64 KiB of the active file and 16 KiB of each archive, in chronological order, plus application, Qt, OS and architecture versions and UTC timestamps. A partial first line is discarded when reading a tail.

The Qt message handler records warnings, errors and informational messages from all threads. Application events cover startup, shutdown, application activity and playback state changes. SoundCloud failures include HTTP/network error codes and retry attempts, without request URLs or response bodies. Playback errors include the backend error code. Logging does not deliberately collect accounts, track names, listening history, request bodies, headers or settings.

Before writing, the filter removes full HTTP/file/content/application URLs, typical credential fields, Bearer/OAuth credentials, email addresses and the user's home path. The same filter is applied again on export. Arbitrary third-party messages cannot be guaranteed free of personal information: review copied text before posting to a public issue. Qt's existing message handler is preserved for normal platform output; that external output is separate from these filtered files.

## Hangs and interrupted sessions

A worker thread checks a one-second main-loop heartbeat. If the active app stops processing events for at least five seconds, it writes one warning and records recovery when the loop resumes. Monitoring pauses when the app is inactive, avoiding normal Android background suspension. This detects main-loop stalls, not every playback/network stall; it is not a native crash dump or stack trace. Debugger stops or system sleep can also appear as heartbeat gaps.

A `session.active` marker survives abnormal termination and is removed on orderly destruction. A subsequent launch records that the previous session ended unexpectedly. This can mean a crash, forced exit, power loss or Android process reclamation; it does not establish the cause. Existing log entries remain available after restarting a hung or crashed app. If all threads or the OS stop, no new log entry can be guaranteed. Startup messages before the application identity and desktop single-instance check are outside this logger's lifetime.

## Verification

`diagnostics_tests` disables PCRE JIT to also exercise platforms where executable-memory optimizations are unavailable. It covers long log lines and prefixed credential keys without quadratic key scanning. It checks filtering, persistent history, rotation bounds, clipboard contents, interrupted/clean shutdown markers, main-loop stall detection and recovery, and panel layout at desktop and 320 px mobile widths. The existing Android layout fixture also checks report buttons and captures the settings footer. Run `cmake --build --preset debug` and `ctest --test-dir build/debug -R '^(diagnostics|plugins)$' --output-on-failure`.
