# Development progress

## Stage 1 — Android performance (completed November 8, 2026)

Completed:

- Reviewed Android QML, artwork loading, lists, Repeater, effects, player handlers and Android MediaSession. This interface has no Loader; main tracks and queue already use ListView. Small tab Repeaters were left unchanged.
- Confirmed the chain: positionChanged → set(position) → changed → sc.state → all api bindings, including list models. Android now uses catalogState/catalogChanged; position and duration have separate properties and progressChanged. Older state/changed remain for compatibility. Queue reordering notifies both interfaces.
- Android MediaSession no longer schedules publication on every position tick: the one-second timer and immediate catalog/state publication remain. Buttons, track changes and playback controls are retained.
- Decoded artwork is limited to physical display size with DPR accounted for. Previously, even 44×44 thumbnails requested decoding of artwork up to 500×500. Asynchronous loading, disk cache, masks, shadows and animations remain.
- A regression test checks separate position/duration notifications, ignoring the spare player and repeated values, and notifications for other data. Previously every new tick notified the catalog through shared changed; the test now confirms no catalogChanged while retaining legacy changed.

Validation:

- `cmake --build --preset debug` — passed.
- `ctest --test-dir build/debug --output-on-failure` — both suites passed.
- `cmake --build build/mobile-preview -j2` — passed; app launched, Home with artwork visually checked, no QML errors in startup logs.
- `cmake --build build/android --target scrip -j2` — passed, libscrip_arm64-v8a.so (Qt 6.8.3); local CMake used the existing build/android-tools/openssl. No APK was built.
- `git diff --check` — passed. Logs are in build/stage1/.

Limitations:

- `build/android-tools/sdk/platform-tools/adb devices` found no devices. Before/after frame time/FPS, Android GPU profiling, scrolling and real playback on a device were not measured; numerical speedups and absence of all Android regressions are not claimed.
- Large catalog changes still notify the entire map. Moving to QAbstractListModel requires separate profiling and was not included without evidence of need.
- User changes predating the stage were kept outside its commit.

Remaining: check long lists during playback, horizontal collections, player opening and seeking on a device using Qt Quick Profiler/Android frame timeline.

The next stage required the user's “Continue”: Plugin API v2 architecture with v1 compatibility and safe separation of declarative and executable extensions. Plugins were unchanged in this stage.

## Stage 2 — Plugin API v2 architecture (completed November 8, 2026)

Completed: reviewed plugins.cpp/.h, documentation, both QML interfaces, main.cpp integration and v1 tests. The contract and implementation plan were recorded in [plugin-api-v2.md](plugin-api-v2.md): manifests/versions, v1 adapter, dependencies, permissions, lifecycle, bounded events/commands/rules, settings/data, atomic updates, UI slots, content URIs and a test matrix.

Decision: declarative JSON without user QML/JS; executable packages are rejected until separate platform isolation is implemented and validated. An executable runtime, store and automatic updates are not claimed. Identified differences: Android rejected content:// and omitted profile panels; theme priority and action routing differed across platforms. Fixes belonged to stage 3.

This stage changed documentation only; API v2 was not yet implemented. User edits were preserved. `cmake --build --preset debug`, both CTest suites and `git diff --check` passed; the current debug build was launched and left open. No runtime tests were added because execution was unchanged. Android/Windows v2 checks were deferred until implementation; the specification does not prove platform functionality. Implementation by architecture groups was the next stage, requiring “Continue”.

## Stage 3 — declarative API v2 implementation (November 8, 2026)

Completed:

- Strict UTF-8/JSON parser checking duplicates, depth, types, limits, versions, references and permissions. Executable packages are rejected; user QML/JS is not loaded.
- v2 registry, v1 migration retaining enablement and flags, QSaveFile, locking and detection of other-instance changes. Installation/update preview, permission consent, settings/data retention, removed-setting restoration, dependencies/cycles, safe disabling and removal with retained configuration.
- Bounded player events, command dispatcher, one-shot timers and private data. Per-action checks, quotas, queue/timer cancellation on disable/logout, protection against synchronous and asynchronous loops. Frequent backend ticks neither refresh the whole catalog nor copy it into the manager; progress is requested once per second only with subscriptions.
- Shared QML manager, home/profile panels and player tools. Fixed Android profile panels, action routing, content:// reads without local-path conversion, user appearance priority and theme-conflict messages.
- Working sleep-timer, queue-tools and calm-studio examples; [implemented API reference](plugin-api-v2-reference.md). Existing user changes were excluded from this stage.

Checks actually performed:

- `cmake --build --preset debug` — passed; both CTest suites passed (Qt Test reports 18 passed for plugins and 25 for soundcloud, including init/cleanup).
- 13 Python unit tests passed. Example JSON passes the actual validator; examples are installed/enabled in tests.
- QML checks for the manager at widths 320/960, confirmation dialog, all three slots and command invocation from a real QML button — no warnings. Snapshots in build/debug/plugin-manager-*.png and plugin-review*.png were visually checked.
- `cmake --build build/mobile-preview -j2` — passed.
- Android arm64 and current debug APK — passed: `JAVA_HOME=build/android-tools/jdk GRADLE_USER_HOME=build/android-tools/gradle cmake --build build/android --target apk -j2` (the actual command used absolute paths). Initial packaging attempts found old Gradle 8.10 and a corrupt local archive; only the generated wrapper in build/android was changed to the verified existing gradle-mirror.zip with Gradle 8.14.3. Source Gradle settings were unchanged. APK: build/android/android-build/build/outputs/apk/debug/android-build-debug.apk. This is a local debug build, not a release.
- `git diff --check` — passed; logs in build/stage3/.

Limitations and remaining acceptance:

- adb found no devices. Actual SAF/content provider behavior (unknown length, denied/revoked grant, slow provider), background timers and Android playback were not checked; building an APK does not replace these checks. Blocking provider reads cannot be forcibly canceled: the GUI remains free, but workers and process exit may wait for the OS; at most two such reads are allowed.
- Windows was not run. Cross-platform changes use Qt 6/C++20, but runtime absence of regressions on those systems is unconfirmed.
- Registry writes are atomic but synchronous; maximum-size registry performance on slow storage was not measured. The sleep timer is not a precise Android alarm. An executable runtime, publisher signatures, store and automatic update downloads are intentionally absent and are not advertised as ready.
- Next work with a device: platform acceptance from the architecture — SAF, background operation, installation/update/permission revocation, disabling and removal on Android; then the same Windows scenarios. Available local checks are complete; full platform acceptance remains open.

The current desktop debug build and mobile preview were launched after the final build and left open.

## Android header and status bar fix (November 8, 2026)

The overlapping-title cause was confirmed on Qt 6.8.3: SafeArea is available from Qt 6.9, so the old height binding failed and the header lost its height. Added a compatible top inset with fallback 0 while retaining the base 66 logical pixels. The geometry test using the real Android Main.qml failed on zero height before the fix and passes afterward on Qt 6.8.3 and Qt 6.10 at portrait/wide sizes. Snapshots in build/preview-683/android-header.png and build/debug/android-header.png were reviewed.

At an additional request, SCripActivity was added: after resume/focus, it enables setAppearanceLightStatusBars(true) through the existing AndroidX Core. This produces dark system icons on a light background; bottom system bar settings are unchanged. Android instrumentation was extended to check the flag (compiled with javac, not run because adb found no devices). API documentation: [WindowInsetsControllerCompat](https://developer.android.com/reference/androidx/core/view/WindowInsetsControllerCompat), [Qt ApplicationWindow](https://doc.qt.io/qt-6/qml-qtquick-controls-applicationwindow.html).

Validation: debug preset and mobile preview built, plugins suite passes on both Qt versions, updated arm64 debug APK built. Logs: build/header-fix/. Actual system icon appearance requires phone testing. Bottom buttons, appearance and navigationFooter handlers were retained byte-for-byte relative to the working file at task start; pre-existing bottom-panel changes are excluded from the fix commit.
