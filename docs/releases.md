# Build and release runbook

Read this before preparing a Sange release. This records the verified 2.1.0
workflow and its failure modes. Recheck tool availability and repository state;
local build directories are caches, not version-controlled dependencies. A
previous release request does not authorize publishing a future release.

## Start with the existing environment

- Inspect `git status`, `git remote -v`, current versions and existing Actions runs.
- Keep SDKs, downloads, logs, staging directories and screenshots under `build/`.
  Preserve working CMake caches and installed toolchains instead of downloading
  or configuring them again. Do not run concurrent builds in the same directory.
- The existing local desktop builds are `build/debug` and `build/release`.
- The Windows cross-build cache is `build/media-windows`; its Qt/Clang sysroot
  is `build/win-kit/clang64`. Validate that cache before reusing it on another
  machine. Native Windows installation still needs GitHub Actions.
- Android SDK, JDK, Qt 6.10.1 kits, OpenSSL and emulator tools are under
  `build/android-tools`. Patched FFmpeg is under `build/android-ffmpeg-seek`.
  `scripts/build_android.sh` builds the three-ABI universal APK.
- The local NSIS executable is `build/nsis-local/usr/bin/makensis`; set
  `NSISDIR` to `build/nsis-local/usr/share/nsis` when invoking it.
- Version-specific helpers and evidence from 2.1.0 are in
  `build/release-2.1.0-review/`; the final materials are in
  `build/compliance-2.1.0-final/`. These ignored directories may disappear.
  Inspect and parameterize helpers before reuse; they contain historical names,
  commits and paths. Never execute an old signing helper without inspection.

```sh
# Configure only when the debug cache is absent or needs updating.
cmake --preset debug
cmake --build --preset debug

# Existing local release cache for Arch Linux.
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release --parallel 2

# Existing Windows cross-build cache; not a bootstrap command.
cmake -S . -B build/media-windows -DCMAKE_BUILD_TYPE=Release
cmake --build build/media-windows --parallel 2

bash scripts/build_android.sh
```

Linux, Windows cross-compilation and Android can run concurrently in their own
build directories if memory permits. Start native macOS CI early: its two
architectures, dependency downloads and package audits dominated release time.
Reuse successful runs for the same application sources. Documentation-only
follow-ups do not require rebuilding every platform; record source provenance.
The local Ninja cache has previously reported `premature end of file;
recovering`, causing recompilation. Inspect build logs rather than assuming an
incremental build will be instant or deleting the entire build tree.

## Prepare versions before building

Update together:

- `project(Sange VERSION ...)` and the increasing Android version code in
  `CMakeLists.txt` (2.1.0 used 20100).
- All three installer version fields in `packaging/windows/installer.nsi`.
- `packaging/linux/control` and `packaging/linux/PKGBUILD`.
- `CHANGELOG.md`, `release-notes/<version>.md`, README downloads and current
  installation/wiki instructions. Preserve historical release records.

Search packaging scripts and workflows for the previous release number before
starting CI. The macOS source selector once still required the 2.0.1 archive;
`packaging/macos/compliance.py` now reads the version from `Info.plist`, and the
macOS workflow derives source archive names from CMake. Preserve that behavior.
The dormant `macos-promote.yml` is specific to the historical 2.0.1 release;
do not trigger it for a new release.

Freeze and record the application source commit before building. Create one
shared `Sange-<version>-source.tar.gz` and pin its SHA-256 in the final PKGBUILD.
The descriptor can distinguish the application-code commit from the archive
commit, as `licensing/releases/2.1.0/release.json` does. If these differ, verify
that the actual application sources match. Do not move a published tag.

## Checks and native workflows

```sh
bash tests/run-hls-seek.sh --generate-only
# Use an isolated session/keyring and a working silent audio sink for integration
# tests. The old build/run-release-tests.sh shows the setup, but contains local
# paths and a sink name that must be checked first.
ctest --test-dir build/debug --output-on-failure
bash tests/run-secretstore-linux.sh
python -m unittest discover -s tests -p 'test_*.py'
node --test tests/worker.test.mjs
python scripts/check_licenses.py --repo .
python scripts/check_secrets.py --mode tracked
git diff --check
```

An ordinary CTest success can hide skipped integration methods. Require passes
for decoded HLS seeking, stream-cache seeking, MPRIS session D-Bus and real
local equalizer audio where supported. Use `SCRIP_HLS_TEST_PLAYLIST` pointing at
`build/hls-seek-fixture/test.m3u8`. Do not access the user's keyring or settings
for test fixtures. For direct QML test execution, match CTest's environment:
`QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software` and
`QT_QUICK_CONTROLS_STYLE=Basic`.

- `linux-cpp.yml`: desktop CI; `workflow_dispatch` also builds the Debian 13
  binary inside Debian 13. Do not package the Arch-host binary as a Debian build.
- `macos-build.yml`: Apple Silicon and Intel builds, CTest, disposable Keychain,
  deployed-package audits and Cocoa startup with the developer Qt kit hidden.
- `windows-installer-test.yml`: installer rollback/integration coverage.
- `windows-release.yml`: verify the actual draft installer with `tag` and
  `previous_tag`; use the previous Windows release, not a macOS-only release.
- `macos-audio-verify.yml`: investigate Intel audio test failures on the exact
  `source_ref`, without rebuilding the DMG. It repeats the original audio case
  three times and runs the complete SoundCloud suite.

During 2.1.0, Intel initially failed the equalizer pause/seek case; unchanged
sources subsequently passed all three isolated repeats and the complete suite.
Investigate failures and retain the evidence; never silently skip or weaken a
check to publish. macOS offscreen has one documented synthetic touchscreen-flick
skip. A later packaging step can succeed despite a failed test step: inspect
individual step conclusions and logs, not just the current progress label.

Do not rerun an entire successful workflow just to poll progress or regenerate
metadata. Inspect an existing run first, and rerun only when a failure or source
change warrants it. Linux CI uses a larger timeout and Qt caching because
package installation previously exhausted its old 20-minute limit.

## Signing and upgrade compatibility

Android must retain `org.scrip.app` and the published release certificate.
Signing material lives outside the repository under the user's configuration
`SCrip/release-signing/` directory (`android-release.jks` and a password file).
Use `apksigner --ks-pass file:<external-password-file>`; never print or copy
passwords into the repository. Old `build/android-tools/sign-universal.sh`
uses a debug keystore and is unsuitable for release upgrades.

Compare `apksigner verify --print-certs` certificate SHA-256 with the actual
previous published APK before upload. Confirm version name/code and all three
ABIs. Test installation with `adb install -r` in a separate emulator and verify
that sentinel data survives; do not replace a user's device installation.
`tests/android/run_lifecycle.sh` uses separate instrumentation package IDs.

Windows retains its SCrip install directory, executable and registry identity;
Linux retains the `scrip` package/executable; storage identities stay unchanged.
Check upgrades in native environments. For isolated desktop startup, use
separate config/data/cache directories and `TMPDIR`; otherwise the single-instance
socket can forward to the user's running app and immediately exit successfully.

## Package materials and publication

1. Create a draft release targeting an explicit full commit SHA. Short commit
   IDs can be rejected by the release API. Do not mark it latest yet.
2. Build/package all supported platforms and assemble file inventories. Existing
   Windows deployment payloads can be reused only after dependency/toolchain
   identity has been verified; do not omit newly required runtime libraries.
3. Prepare `licensing/releases/<version>/release.json` and the materials overlay.
   Include every changed or missing component source and notice relative to the
   pinned base materials, not just the new application archive. Reconstruct the
   materials from the base plus overlay and audit the actual unpacked packages.
4. For macOS, retain both DMGs, bundle inventories, compliance manifests and
   corresponding pinned Qt/FFmpeg/provisioning sources. Different gzip versions
   can produce different archive hashes for identical Git content: compare all
   file/link contents before sharing a canonical archive, then record both
   native and distributed hashes/provenance in manifests. Never change a hash
   without proving source equivalence.
5. Generate `SHA256SUMS` over the final assets, excluding the checksum file
   itself. Do not upload helper outputs such as `.uninstall.nsh` or APK `.idsig`
   as release packages. Compare every uploaded asset's state, size and server
   SHA-256 with the local file. Check that the exact expected asset set exists.
6. Commit final metadata, push the branch and an explicit version tag, and
   publish the draft as stable/latest only within the user's authorization.
   Keep release notes candid about platform validation limits and failed/repeated
   checks. Publication triggers independent licensing and Windows verification.
7. Fetch the public latest API, verify all updater selections and download all
   six actual packages to check complete byte counts and SHA-256. Monitor the
   published-release audit through completion, including native mounted-DMG
   checks. Queueing for a macOS runner is not a test failure.
8. Update the separate Wiki repository following `docs/wiki.md`; preserve remote
   changes. Reuse the main repository's commit identity per command if needed,
   not global Git configuration. Verify rendered pages with a fresh URL if a
   browser/search cache still displays old content. Finish with clean statuses
   in both repositories and a concise publication report.

Draft tags need not exist as Git refs yet; a tag-specific REST lookup can return
404 for a draft. Use `gh release view` or find its numeric ID in the releases
list, then query `/repos/sykia/sange/releases/<id>`. Completed job logs can be
read before the whole workflow completes. When GitHub CLI rejects ANSI escapes,
use `gh api --allow-escape-sequences .../actions/jobs/<id>/logs` into a log file.

## Updater contract

The application checks `sykia/sange`'s latest release on startup. Download and
installation are user initiated. Each platform needs exactly one matching asset,
positive size no larger than 1 GiB, and a valid `sha256:` digest:

| Platform | Required filename suffix |
| --- | --- |
| Windows | `-Windows-x64.exe` |
| Debian | `-amd64.deb` |
| Arch | `-x86_64.pkg.tar.zst` |
| Android | `-Android-universal.apk` |
| macOS Apple Silicon | `-macOS-arm64.dmg` |
| macOS Intel | `-macOS-x86_64.dmg` |

Canonical download URLs start with
`https://github.com/sykia/sange/releases/download/`. Verify redirect hosts,
complete size and SHA-256, not just HTTP 200 or filename. macOS opens the verified
DMG; replacement in Applications remains a user action. Windows/Linux/Android
2.0.0 and older binaries allow only the former `sykia/SCrip` path and require a
one-time manual in-place upgrade. A new release cannot change those installed
binaries retroactively. macOS 2.0.1 and Sange 2.1.0 use the current path.

## Verified 2.1.0 reference

- Release: https://github.com/sykia/sange/releases/tag/v2.1.0
- Tag/main at publication: `ab39fd883169fbbacd73de798afa89b21f490166`.
- Shared archive commit: `f031a9ac0b412737dc5001e788c4e88028515c7b`.
- Native macOS packages: Actions run `44527295783` (Intel audio retry below).
- Intel unchanged-source verification: `44531386367`.
- Windows 2.0.0 upgrade verification: `44526223160`.
- Debian/native Linux packaging run: `44525467833`.
- Published combined audit: `44532267962`, all three jobs passed.
- Published Windows verification: `44532267941`, passed.
- The local `build/release-2.1.0-review/FINAL-REPORT.md` contains detailed evidence.

Run IDs and version-specific paths above are historical references, not inputs
for a future release. Resolve the new commit, version and current runs each time.

## Executable rename after 2.1.0

The current source changes the Linux/Windows output filename to `sange` /
`sange.exe` at the user's explicit request. The CMake target remains `scrip`;
macOS remains `Sange` and Android keeps its package/library identity. Keep the
SCrip settings, keyring and installation-directory identities for upgrades.
Linux launchers and packaging use the new filename. Windows setup detects both
process names and restores shortcuts to the executable present after rollback;
its migration fixture upgrades `SCrip.exe` to `sange.exe`. Historical 2.1.0
commands/assets above retain their original executable names.
