# SCrip plugins

SCrip supports declarative JSON plugins. The application renders panels and handles the commands listed in each manifest. Plugin QML, JavaScript, libraries, processes, arbitrary URLs and files are not executed.

To install a plugin, select its JSON file in **Plugins**, review the requested permissions and confirm installation. New plugins are disabled by default. Settings and individual permissions can be changed in the plugin card. Appearance contributions are applied in installation order, and independent contributions can be toggled separately.

## Manifest structure

Required fields: `api: 2`, `kind: "declarative"`, `id`, `name`, `version`, `requiresHost`, `permissions`, `settings`, `contributions`, `rules`. The `description` field and `dependencies` array are optional. See the [examples directory](../examples/plugins) for complete files and the [API reference](plugin-api-v2-reference.md) for exact limits.

`theme` contributions define the accent, artwork style and corner radius. `panel` contributions create sections on Home, profiles or the player panel. A boolean setting can control each contribution independently. Buttons invoke only commands allowed by the manifest and approved by the user.

## Updates and storage

Updates are installed from JSON files. Settings are retained by stable IDs, compatible permissions remain granted, and plugin data is preserved. A plugin removed with its configuration retained can be reinstalled with the same ID.

The registry is stored in `plugins.json` in the application data directory. Saves are atomic, concurrent changes are protected by a lock, and the registry is limited to 16 MiB and 24 entries. On first launch, data from older registries is migrated to `plugins.json`; original files are retained and backups are created alongside them. Older appearance groups receive separate setting toggles.

## Access boundary

Manifests are strictly validated before installation. Limits apply to size, JSON depth, settings, contributions, rules, dependencies, data and command counts. Plugins receive only the listed permissions and cannot access SoundCloud objects, tokens, the network client or arbitrary filesystem locations.
