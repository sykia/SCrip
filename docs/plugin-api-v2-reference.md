# Plugin API v2: usage and implementation contract

SCrip accepts declarative API 2 JSON plugins. Executable QML/JS, libraries and processes are unsupported. The executable format is rejected with `unsupported-runtime`: a secure isolated environment has not been implemented. The [architecture](plugin-api-v2.md) explains the trust boundary.

## Installation and management

Open **Settings → Plugins**. Choose **Install / update from file…**, select JSON, and review the name, version, SHA-256, setting changes and permissions. Files are unsigned: a checksum and matching ID do not prove authorship. Approve the listed permissions. New plugins are installed disabled; choose **Enable** in the card.

Each card shows the description, ID, version, status and reason, permissions, settings and errors. Revoking a required permission stops execution, removes panels and cancels timers. Granting it again allows execution to resume, but does not restore canceled timers. After a quota error, disable and re-enable the plugin to restart it. **Clear log** removes diagnostic messages without clearing the execution block.

**Update from file** uses the same preview. Updates require the same ID and a higher version. Settings are retained by stable ID if compatible with the old type and new limits; others revert to defaults with a preview warning. Removed settings are retained for restoration if their IDs return. Data is preserved. Groups and toggles from previously installed packages become independent contributions and settings; new files in the older format cannot be installed. Use the existing ID to update a package: a different ID creates a separate installation.

Dependencies are neither downloaded nor enabled automatically. The card shows why activation is blocked. Disable dependent plugins before an incompatible dependency update. Removal lists dependent plugins and asks to confirm disabling them. Configuration can be kept for reinstallation; the retained entry appears as a removed package, counts toward the limit and can be deleted permanently.

The user's theme and explicitly chosen radius take precedence over corresponding plugin properties. Among plugins, installation order applies: the last active contribution wins. Cards show which properties are applied or overridden. On Android, **Use plugin appearance** clears user overrides; on desktop, reset appearance and select the default theme.

## Working examples

| File | Purpose |
| --- | --- |
| [Sleep timer](../examples/plugins/sleep-timer.json) | Player → Add-ons: start/cancel a pause after a configurable 1–180 minutes |
| [Queue tools](../examples/plugins/queue-tools.json) | Previous/next through the built-in player, with an error when unavailable |
| [Calm studio](../examples/plugins/calm-studio.json) | Green theme and a likes/playlists profile panel, with independent toggles |

Panels are available in `home.panels`, `profile.panels` and `player.tools` on desktop and Android. The last slot opens through the player's **Add-ons** button. Commands run only while the plugin is active and permissions are valid.

Timers are one-shot and do not survive disabling, logout or restart. They use the Qt event loop, rather than a system alarm. Android sleep may delay execution until the app resumes. The scheduled time appears in the panel; precise background pausing is not guaranteed. Each plugin may have two timers; starting the same ID replaces the previous timer.

## Manifest

See the JSON examples. Required fields: `api=2`, `kind=declarative`, `id`, `name`, `version`, `requiresHost`, `permissions`, `settings`, `contributions`, `rules`. `description` and `dependencies` are optional. Unknown fields, duplicate keys, invalid types, commands/references and exceeded limits are rejected before installation. UTF-8, up to 128 KiB, depth ≤16. Text is PlainText; paths, arbitrary URLs, eval, HTML and resources are unavailable. ID: `[a-z][a-z0-9.-]{0,47}`. Name: ≤48 printable characters; description: ≤1000.

Versions are exactly `major.minor.patch`, with numbers 0–65535 and no leading zeros or suffixes. `requiresHost={min,maxExclusive}` requires both bounds. API compatibility is checked separately; a matching SCrip version does not add support for a new API. `dependencies` allows up to eight `{id,min,maxExclusive}` entries, v2 only; cycles are forbidden. A missing dependency or incompatible host blocks the whole package.

`permissions` is a unique set drawn from `ui.panels`, `ui.theme`, `player.observe`, `player.control`, `navigation`, `storage`, `timers`. All declared permissions are required for activation. `player.observe` exposes track titles/artists and playback state, which are personal data. Tokens, profiles, stream URLs, networking and arbitrary files are unavailable. Dependency permissions are not inherited.

`settings` allows up to 16 `{id,type,label,default}` entries. Types: boolean; integer with required min/max (integers within ±2147483647); enum with options (1–16 unique strings of up to 48 characters). Substitution, secret settings and arbitrary text settings are unavailable. Setting references are allowed in the designated `enabledSetting`, `minutesSetting` and typed panel item fields.

`contributions` allows up to eight entries with unique IDs:

- `panel`: `{id,type:"panel",slot,title,text?,buttons?,items?,enabledSetting?}`. Slots are listed above; title ≤48, text ≤1000; up to four `{label,action}` buttons, label ≤32. `enabledSetting` references a boolean. `items` allows up to eight `{type,source,key,valueType,label}` entries. `source` is `setting` or the plugin's own `storage`; `valueType` is `boolean`, `integer` or `text`; `type` is `toggle` for boolean only, or `text` otherwise. Settings are checked against manifest types; storage values are displayed only when the actual JSON type matches. Reading storage requires `storage` permission. Text is limited to 160 characters, with no expressions or substitution.
- `theme`: `{id,type:"theme",values,enabledSetting?}`, at most six per plugin. `values` contains only `accent` (#RRGGBB), `covers` (artwork/artist), `coverRadius` (integer 0–24), with at least one field. Background and text colors cannot be changed. Contributions within one plugin apply in manifest order; the last value for a property wins.

`rules` allows up to 16 `{id,event,conditions,actions}` entries. `conditions` allows up to four `{field,equals}` entries, combined with AND and exact type/value comparison; an empty array matches any event of that kind. `actions` contains 1–4 commands. Templates, loops and calls to another plugin are unavailable. Initial host synchronization does not trigger rules.

| Event | Available condition fields | Permission |
| --- | --- | --- |
| player.trackChanged | urn/title/artist (string), durationMs (number) | player.observe |
| player.playbackChanged | playing/buffering (bool) | player.observe |
| player.progress | positionMs/durationMs (number), at most once per second | player.observe |
| player.queueChanged | count/index (number); queue contents are not exposed | player.observe |
| timer.expired | timerId (string), own timers only | timers |

The full internal `sc.state` is never passed to a package. Events supply rule conditions; arbitrary callbacks and interpolation of event fields into commands are unavailable. Each `timerId` used in actions requires a `timer.expired` rule with an `equals` condition for that ID.

## Commands

| command | Arguments besides command | Permission |
| --- | --- | --- |
| player.pause / resume / next / previous | None | player.control |
| player.seek | positionMs: integer ≥0, ≤9007199254740991; host clamps to duration and checks seekable | player.control |
| player.seekRelative | offsetMs: signed integer within ±9007199254740991; host checks seekable and current position, then clamps the result to duration | player.control |
| player.volume | value: finite number from 0 to 1; uses the app's built-in volume model | player.control |
| player.repeat / shuffle | enabled: bool | player.control |
| navigation.open | destination: likes/history/feed/playlists/profile/search; buttons only | navigation |
| timer.start | timerId, minutesSetting: integer setting ID with min≥1 and max≤180; buttons only | timers |
| timer.cancel | timerId | timers |
| storage.set | key (ID), value (JSON literal) | storage |
| storage.remove | key (ID) | storage |

Rule commands undergo the same permission, activity and availability checks. A button passes only the plugin ID, contribution ID and button index; the host retrieves the validated command itself. Manifests cannot invoke methods by string. Unavailable track/seek errors appear in the card and are returned to the calling UI.

`storage` is private key/value JSON: up to 128 keys and 256 KiB including `orphanSettings`, depth ≤8. Values are literal, with no player-data substitution; this is not a programmable database. The manager displays data and allows clearing it. Over-quota writes are rejected without changing old data. Basic user settings do not require storage permission.

New commands and panel items are recognized only by compatible SCrip releases after an update. `requiresHost` does not automatically add support to older hosts: specify the host version range actually tested.

When releasing a build with new commands, increase the app version beyond the last published release and set `requiresHost.min` to at least that new version. Plugins using commands added after 1.4.2 must require at least `1.5.0`. The manifest currently has no separate Plugin API implementation version.

Events are queued: up to 32 per plugin, with progress events coalesced. Limits are eight actions per event-loop pass, eight actions/second per plugin and 64 overall (fixed one-second window). Overflow or causal-chain depth >4 causes `error` and removes UI and timers. For asynchronous player signals, the host retains a causal tag for up to 30 seconds; unknown external changes may conservatively receive that tag. This is a limited defense against reactive loops, rather than exact backend tracing. Do not design rules that endlessly change state in response to their own changes.

## C++ / QML facade

`Plugins` exposes `installed`, `panels`, `theme`, `error` and the `changed` signal. Installation and updates use `prepareInstall`:

1. `prepareInstall(QUrl)` returns `{operationId,error}`. JSON reading and validation run in a worker, with at most two reads per process.
2. `installPrepared` reports the same operationId and either error or plugin/permissions/changes/previousVersion/sha256. Show confirmation only after this signal.
3. `commitInstall(operationId, QStringList acceptedPermissions)` checks an exact permission match and unchanged registry; success returns an empty string. The preview is single-use and expires after five minutes.
4. `cancelInstall(operationId)` cancels waiting/preview. An already-blocking external Android provider read cannot be forcibly interrupted: the GUI remains free, but the worker slot is released only when the provider returns.

`enable(id,bool)`, `setPermission(id,name,bool)`, `setSetting(id,key,value)`, `remove(id,keepData=false)` and `clearData(id)` return an error string or an empty string. `dependents(id)` returns a transitive list. `invokeButton(id,contributionId,index)` validates the stored contribution and returns an error; `commandFinished` reports requestId/pluginId/ok/code/message. `navigationRequested` contains only an allowed destination. `clearError` clears messages. The C++ player adapter is bound in `main.cpp` and is unavailable to packages.

`installed` contains plugin, enabled (user intent), status/reason, version, grants, permissionRows, settings/settingRows, data/orphanSettings, themeStatus, timers and lastError/log (up to 20 messages in memory). States: disabled, awaiting-permissions, blocked, active, error, removed. Quota errors reset only after explicitly disabling/re-enabling. `enabled=true` does not mean active.

## Storage and validation limits

`plugins.json` resides in AppDataLocation. Manifests are copied into it, so the source file can be moved. QSaveFile commits the candidate atomically before changing memory state; the registry limit is 16 MiB and 24 entries including retained configurations. QLockFile and the current registry's SHA-256 prevent overwriting another instance's changes; conflicts require a restart. Setting/data writes are synchronous; quotas bound their size, but performance on slow storage has not been measured separately.

When `plugins.json` is absent, existing `plugins-v1.json` and `plugins-v2.json` are merged: current registry order is retained and other entries are appended. Originals remain in place, with byte-for-byte backups created atomically alongside them. Older groups become separate settings and contributions. A corrupt source or duplicate ID with different state blocks migration without writing a new file.

Desktop accepts local `file://` URLs without a remote host. Android additionally accepts `content://` from the system picker and reads QFile using the original URI without QFileInfo/isLocalFile checks. Unknown size is bounded by reading at most 128 KiB + 1 byte. URI/grant is not retained. Canceling does not stop a stalled ContentProvider and may delay process exit if the OS never returns the read; at most two such jobs are allowed. Android providers, grant revocation and unknown lengths require device testing; desktop QML tests do not replace it.

There is no automatic plugin update checking, store, publisher signing, executable runtime, precise background alarm or migration scripting. These are explicit limitations. Tests and completed builds are listed in [development progress](development-progress.md).
