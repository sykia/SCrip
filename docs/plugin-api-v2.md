# Plugin system architecture

SCrip uses a single declarative runtime and the API 2 manifest format. The implementation and full contract are described in the [reference](plugin-api-v2-reference.md). This document defines architectural boundaries, rather than a separate runtime.

## Trust boundary

A plugin is UTF-8 JSON without executable code, resources or paths. C++ strictly validates the manifest and executes a finite set of commands. QML displays only built-in panels and settings. QML/JavaScript, HTML, libraries, processes, arbitrary URLs, file APIs and dynamic evaluation are prohibited.

Contributions and commands are limited by user permissions. Player data is exposed only through explicitly subscribed events. Queues, timers, causal depth, actions and storage have quotas; dependencies are checked before activation. A save failure leaves the active state unchanged. Existing tests cover validation, permissions, commands, events, timers, data and updates.

## Manifests and registry

The API 2 manifest format is the public package contract. The internal registry version, `format: 2`, is stored separately and does not depend on the filename or manifest API. The single storage file is `plugins.json` in AppDataLocation. Changes are written through QSaveFile, checked with SHA-256 and protected by QLockFile. Limits: 24 entries, 16 MiB per registry, 256 KiB of plugin data, 128 keys and depth 8.

## Migrating older data

If `plugins.json` is absent, the manager reads the older `plugins-v1.json` and `plugins-v2.json` files. Entries are validated before any changes; the current registry retains its order, and unique entries from the older registry are appended. Duplicates with different settings or data block migration. Byte-for-byte `.bak` backups are created before the unified registry is committed atomically. The original files are retained.

Older appearance groups become API 2 manifests with a separate boolean setting for each group, standard theme and panel contributions, and the previously allowed actions. They do not load or run in a separate runtime. New installations accept only the declarative API 2 format.
