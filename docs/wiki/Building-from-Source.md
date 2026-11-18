[Home](Home) › Building from Source

## Desktop requirements

| Requirement | Details |
| --- | --- |
| Compiler | C++20 |
| CMake | 3.21 or later |
| Build tool | Ninja for the `debug` preset |
| Qt | 6.5 or later: Quick, Network, Multimedia, ShaderTools, Widgets |
| Default test build | Qt Test and QuickTest |
| Linux | Qt DBus, pkg-config, libsecret development files |
| Windows | Windows SDK for native media controls; a compatible Qt/compiler kit |

Qt 6.8+ enables additional PCM-based audio features and HLS regression tests. Availability also depends on the multimedia backend. The CMake minimum does not imply every feature works identically with Qt 6.5.

Linux libsecret development packages are `libsecret-1-dev` on Debian/Ubuntu and `libsecret` on Arch. Running the app also needs an unlocked Secret Service for authentication. Building alone does not require unlocking your keyring.

## Configure, build and test

```sh
git clone https://github.com/sykia/sange.git
cd sange
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

If CMake cannot find Qt, configure with your kit's prefix:

```sh
cmake --preset debug -DCMAKE_PREFIX_PATH=/path/to/Qt/kit
```

Run on Linux:

```sh
./build/debug/scrip
```

Run in Windows PowerShell with the Qt runtime available:

```powershell
.\build\debug\scrip.exe
```

The executable retains its `scrip` name. Generated files belong in `build/`. A source build does not install system URL handlers or create a distributable installer.

### Build options

| CMake option | Purpose |
| --- | --- |
| `-DBUILD_TESTING=OFF` | Omit desktop test targets and Qt Test/QuickTest requirements |
| `-DSCRIP_MOBILE_PREVIEW=ON` | Use the Android QML interface in a desktop build |

Use a separate build directory for a mobile preview:

```sh
cmake -S . -B build/mobile-preview -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DSCRIP_MOBILE_PREVIEW=ON
cmake --build build/mobile-preview
```

Desktop preview does not validate Android services, permissions, GPU behavior or APK packaging.

## Windows packaging

The repository contains native Windows build and release workflows in [`.github/workflows`](https://github.com/sykia/sange/tree/main/.github/workflows), plus the installer and packager in [`packaging/windows`](https://github.com/sykia/sange/tree/main/packaging/windows). Distribution needs the Qt/runtime deployment and packaging steps, in addition to compiling the executable. See [Windows installation notes](https://github.com/sykia/sange/blob/main/docs/windows-install.md) for certificate-signing support.

## Android build

The supplied scripts use a **Linux host**. They expect an existing toolchain, rather than installing the SDK automatically:

- Host Qt 6.10.1 and matching Android Qt kits for `arm64-v8a`, `armeabi-v7a`, `x86_64`.
- Android SDK with platform/build tooling for API 35; NDK `27.2.12479018`.
- JDK, Gradle dependencies and Ninja.
- Android OpenSSL libraries under `ssl_3/<ABI>`.
- FFmpeg 7.1.2 sources, patched and built by the supplied helper.

The default toolchain root is `build/android-tools`. Override locations as needed:

| Variable | Location |
| --- | --- |
| `SCRIP_ANDROID_TOOLS` | Shared toolchain root |
| `ANDROID_SDK_ROOT`, `ANDROID_NDK_ROOT` | SDK and NDK |
| `JAVA_HOME`, `GRADLE_USER_HOME` | JDK and Gradle cache |
| `QT_HOST_PATH` | Host Qt kit |
| `SCRIP_QT_ANDROID` | Primary Android Qt kit, default arm64 |
| `SCRIP_ANDROID_OPENSSL_ROOT` | Android OpenSSL root |

From the source root, with those dependencies prepared:

```sh
bash scripts/build_android.sh
```

The script first builds the patched FFmpeg `libavformat`, then configures `build/android-universal` and builds the `apk` target. Minimum Android API is 28; configured target/compile API is 35. Inspect [`scripts/build_android.sh`](https://github.com/sykia/sange/blob/main/scripts/build_android.sh) and [`scripts/build_android_ffmpeg.sh`](https://github.com/sykia/sange/blob/main/scripts/build_android_ffmpeg.sh) for the exact layout. This is a build step, not a release-signing procedure. Updating an installed release requires its signing identity.

## Validation boundaries

CTest covers controller logic, plugin validation/runtime, stream caching and QML fixtures. Some integration checks need additional audio, D-Bus, keyring or HLS fixtures and may skip without them. [Linux CI](https://github.com/sykia/sange/blob/main/.github/workflows/linux-cpp.yml) provisions those dependencies and requires selected integration checks to pass.

Real secure-storage checks have separate runners in `tests/run-secretstore-linux.sh` and `tests/run-secretstore-android.sh`. Desktop tests do not establish Windows or Android device behavior.

---
[Architecture →](Architecture) · [Troubleshooting](Troubleshooting)
