[Home](Home) › Plugin System

Sange supports API 2 JSON packages in two forms: **declarative** plugins and **trusted QML/JavaScript** extensions. Both are installed from a JSON file.

## Install and manage

1. Open **Settings → Plugins → Install / update from file…**.
2. Select the package's JSON file; on Android use the system picker.
3. Review its ID, version, SHA-256, requested permissions and setting changes.
4. Confirm installation, then **Enable** the plugin. New installations are disabled by default.

Use the card to change settings, revoke permissions, inspect errors or remove the package. Revoking a required permission stops it. Dependencies are not downloaded or enabled automatically. A checksum and matching ID do not prove authorship: packages are unsigned.

Updates use the same file workflow, the same ID and a higher version. Compatible settings and plugin data are retained. There is no plugin store or automatic plugin update checking.

## Declarative API

Declarative packages use validated commands, panels, appearance contributions and event rules. They cannot execute JavaScript, read arbitrary files, make arbitrary network requests or access OAuth credentials.

| Permission | Capability |
| --- | --- |
| `ui.panels` | Panels in `home.panels`, `profile.panels`, `player.tools` |
| `ui.theme` | Accent, artwork style and artwork corner radius |
| `player.observe` | Allowed playback events, including track titles and artists |
| `player.control` | Approved playback commands |
| `navigation` | Open supported app destinations |
| `storage` | Own bounded JSON key/value data |
| `timers` | Own one-shot event-loop timers |

All declared permissions are required for activation. Observation can expose listening information. Declarative dependencies do not inherit each other's permissions.

### Commands and events

| Commands | Arguments / purpose |
| --- | --- |
| `player.pause`, `player.resume`, `player.next`, `player.previous` | No arguments |
| `player.seek`, `player.seekRelative` | `positionMs` / `offsetMs` |
| `player.volume` | `value` from 0 to 1 |
| `player.repeat`, `player.shuffle` | `enabled` boolean |
| `navigation.open` | `destination`: likes, history, feed, playlists, profile, search; buttons only |
| `timer.start` | `timerId`, `minutesSetting`; buttons only |
| `timer.cancel` | `timerId` |
| `storage.set`, `storage.remove` | `key`, plus literal `value` for set |

Rules listen for `player.trackChanged`, `player.playbackChanged`, `player.progress`, `player.queueChanged` and `timer.expired`. Conditions use exact typed equality; actions use the same approved commands. There are no loops, arbitrary callbacks or event-value interpolation.

### Minimal working package

Save as `pause-button.json`, install it and enable it. The **Pause** button appears under the player's **Add-ons**.

```json
{
  "api": 2,
  "kind": "declarative",
  "id": "pause-button",
  "name": "Pause button",
  "version": "1.0.0",
  "requiresHost": { "min": "2.0.0", "maxExclusive": "3.0.0" },
  "permissions": ["ui.panels", "player.control"],
  "settings": [],
  "contributions": [{
    "id": "controls",
    "type": "panel",
    "slot": "player.tools",
    "title": "Playback",
    "buttons": [{ "label": "Pause", "action": { "command": "player.pause" } }]
  }],
  "rules": []
}
```

Complete examples: [sleep timer](https://github.com/sykia/sange/blob/main/examples/plugins/sleep-timer.json), [queue tools](https://github.com/sykia/sange/blob/main/examples/plugins/queue-tools.json), [Calm Studio](https://github.com/sykia/sange/blob/main/examples/plugins/calm-studio.json).

### Limits

| Item | Limit |
| --- | --- |
| Package | 128 KiB UTF-8 JSON; nesting depth 16 |
| Per package | 16 settings, 8 contributions, 16 rules, 8 dependencies |
| Timers | 2 per plugin; configured duration 1–180 minutes |
| Own data, including orphan settings | 128 keys, 256 KiB; nesting depth 8 |
| Registry | 24 entries, including retained removed configurations; 16 MiB |

Versions use `major.minor.patch`. `requiresHost` needs both `min` and `maxExclusive`; choose a range actually tested. Unknown fields, duplicate keys, invalid commands and exceeded limits are rejected. Event/action quotas can stop a plugin with an error; disable and enable it to reset that block.

Timers do not survive disabling, logout or restart. Android sleep may delay them until the app resumes; they are not precise background alarms. User appearance overrides take precedence; among active plugins, later installed contributions win.

## Trusted QML extensions

**`kind: "qml"` requires `code.execute`, granting full trusted in-process access. This is not a sandbox.** Other manifest permissions do not restrict executable code. An extension can access exposed application objects, use Qt networking, block the UI or crash the app. Install code only from authors you trust.

A runtime contribution embeds QML source. Its root must be a Qt Quick `Item` declaring `required property var sdk`. The SDK exposes `id`, `active`, `window`, `player`, `plugins`, `settings`, `setSetting(key, value)` and `find(objectName)`.

Package the repository example from the source root:

```sh
python3 scripts/package_qml_plugin.py \
  examples/plugins/glass-lab.manifest.json \
  examples/plugins/glass-lab.qml \
  build/plugins/glass-lab.json
```

Install the generated JSON, not the empty-source template. The packager embeds QML only, not sidecar assets or compiled shaders. **Reload code** recreates the installed source; it does not reread an external QML file. Updates still require repackaging and reinstalling with a higher version.

Use lifecycle-owned objects and reversible `Binding` overrides guarded by `sdk.active`. Arbitrary assignments and external side effects are not rolled back automatically. See the [QML SDK](https://github.com/sykia/sange/blob/main/docs/plugin-runtime.md) for lifecycle and UI discovery details.

### Recover from faulty code

Fully exit the desktop app, then launch:

```sh
scrip --no-plugin-code
```

For a source build, use `build/debug/scrip --no-plugin-code` (Windows: `build/debug/scrip.exe`). Disable or remove the faulty extension in Settings. There is no in-app Android pre-start recovery gesture.

## Reference

[Declarative API reference](https://github.com/sykia/sange/blob/main/docs/plugin-api-v2-reference.md) · [QML SDK](https://github.com/sykia/sange/blob/main/docs/plugin-runtime.md) · [Manifest validator](https://github.com/sykia/sange/blob/main/src/pluginmanifest.cpp) · [Examples](https://github.com/sykia/sange/tree/main/examples/plugins)

---
[Building from Source →](Building-from-Source) · [Troubleshooting](Troubleshooting)
