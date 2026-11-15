# Trusted QML plugin SDK

## Design and implementation plan

The goal is executable extensions which can introduce behavior and UI that the host did not anticipate. exteraGram's [SDK](https://plugins.exteragram.app/docs) and [method hooks](https://plugins.exteragram.app/docs/xposed-hooking) are the inspiration. SCrip uses its native Qt Quick/JavaScript engine instead of adding Python, Chaquopy or Java hooks.

Implementation stages delivered together:

1. Extend the existing installer, registry and explicit permission review with `kind: "qml"` and `code.execute`.
2. Instantiate author-supplied QML in the main engine with an SDK object, access to the host window/player and named UI discovery.
3. Reconcile instances on enable, disable, permission changes, updates, removal and contribution toggles. Provide manual reload and report compilation/creation errors in the plugin card.
4. Supply a runnable author example, a package command and lifecycle tests shared by desktop and Android code paths.

This is an in-process trusted runtime, not a sandbox. `code.execute` grants the effective authority of the application: other manifest permissions do not restrict executable code. Code can access exposed application objects, installed QML modules, JavaScript networking and URLs supported by Qt. It can block the UI thread or crash the app. Qt does not provide a portable way to intercept arbitrary compiled C++ methods from QML; this SDK supports QObject properties, invokable methods, QML functions, signals, `Connections` and reversible `Binding` overrides. It does not claim Xposed compatibility.

## Author workflow

Keep QML as a normal editable source file. Use a manifest template containing `api: 2`, `kind: "qml"`, `permissions: ["code.execute"]`, and a contribution `{ "id": "main", "type": "runtime", "source": "" }`. Other metadata, settings, dependencies and version ranges follow the existing API 2 contract. A runtime contribution may use a boolean `enabledSetting`. Executable and declarative contributions may coexist in the same QML plugin.

Package the included example from the repository root:

```sh
python3 scripts/package_qml_plugin.py \
  examples/plugins/glass-lab.manifest.json \
  examples/plugins/glass-lab.qml \
  build/plugins/glass-lab.json
```

Select the resulting JSON in the plugin manager, review full access, install and enable. The editable template itself is not an installable plugin until its source is embedded. On Android select the same generated JSON using the system file picker. No sidecar path or retained document permission is needed. Packages remain limited to 128 KiB of UTF-8 JSON; registry and contribution limits still apply.

Edit the QML, package again and reinstall to update the source. Update the manifest version for releases. **Reload code** recreates installed code and retries a failed component; it does not reread an external source file. This also provides a way to reset transient plugin state. Disabling a contribution or revoking full access unloads its code.

## Component contract

A runtime source must have a Qt Quick `Item` root and declare `required property var sdk`. The host supplies `sdk` and assigns a visual parent before `Component.onCompleted`. The root's QObject owner is the SDK. Root geometry and stacking are author-controlled; no host card or layout is imposed. Use `parent.width`/`parent.height` for responsive geometry, and transparent noninteractive Items for overlays that should pass input through.

```qml
import QtQuick

Item {
    required property var sdk
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    property var dock: sdk.find("playbackDock")

    Binding {
        target: dock
        property: "opacity"
        value: 0.6
        when: sdk.active && !!dock
        restoreMode: Binding.RestoreBindingOrValue
    }
    Connections {
        target: sdk.player
        function onCatalogChanged() { console.log("Catalog changed") }
    }
}
```

| SDK member | Contract |
| --- | --- |
| `id` | Owning plugin ID |
| `active` | True while loaded; becomes false before destruction to release guarded overrides |
| `window` | Actual main window, including public QML properties and functions |
| `player` | Actual SoundCloud QObject used by the host; signals and invokables are available |
| `plugins` | Actual plugin manager; trusted code has full manager access |
| `settings` | Current own settings map; reactive to manager changes |
| `setSetting(key, value)` | Validated, persisted own setting update; returns error text or empty string |
| `find(objectName)` | First matching visual descendant or QObject child of the window; null when absent |

The SDK object is supplied as an initial property, using Qt's [QQmlComponent API](https://doc.qt.io/qt-6/qqmlcomponent.html). Sources use a synthetic `scrip-plugin:/<plugin>/<contribution>.qml` URL for diagnostics. Relative sidecar imports are not packaged; installed Qt modules and explicitly addressed resources are available.

## UI discovery and custom rendering

Shared names on desktop and Android: `applicationWindow`, `navigationColumn`, `playbackDock`. Desktop additionally exposes existing names such as `applicationBackground`, `mainContentArea`, `inspectorColumn`, `libraryScroll`; Android exposes `playerSheet`. `find` searches the current visual tree as well as QObject ownership. Lazily created or destroyed elements may be absent. Reacquire references when their host view is recreated.

Use standard Qt Quick components, `Canvas`, animations, custom menus/dialogs and `QtQuick.Effects.MultiEffect` to write new visuals. Custom `ShaderEffect` rendering requires a Qt-compatible compiled `.qsb` shader URL and backend-compatible shader code; this JSON packager currently embeds QML only, not shader binaries or assets. Glass Lab demonstrates an author-defined menu, blurred gradient and reversible dock modification; it is not a physical backdrop-refraction shader. GPU effects depend on the graphics backend; software rendering is not a GPU appearance test.

QML IDs are lexical and are not a reflection API. Use object names, exposed properties or the actual window reference. Internal object names outside the shared contract may change between app versions. Private C++ state, native libraries and arbitrary C++ method replacement are not provided by this SDK.

## Lifetime and recovery

`Component.onCompleted` is the load callback; `Component.onDestruction` is the unload callback. Children, declared Timers, Connections and Bindings are destroyed with the root. Before destruction the host sets `sdk.active` to false; guard reversible overrides with `when: sdk.active && ...` so Qt restores them while their targets still exist. Destruction alone does not restore an active Binding. The SDK remains alive during the root's destruction callback. Store reversible UI modifications in `Binding` objects with restore mode and an `sdk.active` guard rather than imperative assignments. Arbitrary assignments are not rolled back automatically. Multiple plugins overriding the same property should coordinate; unloading overrides out of order cannot guarantee conflict-free restoration.

Objects explicitly created outside the root, raw JavaScript signal connections, requests and external side effects remain the author's cleanup responsibility. There is no forced termination for synchronous JavaScript. Compilation/creation failures stop the plugin and appear in its existing error log. Nonfatal binding errors and later JavaScript exceptions follow Qt's normal diagnostic behavior; the host does not promise to catch or isolate them.

Desktop recovery: start with `--no-plugin-code` (or set `SCRIP_NO_PLUGIN_CODE=1`) to skip executable runtime loading for that process, then disable/remove the faulty plugin through the existing manager. Android development builds can use the environment switch when the launcher supports it; there is not yet an in-app pre-start recovery gesture. The executable engine is shared across platforms, but Android hardware, APK packaging, GPU output and Windows deployment require their own platform validation.
