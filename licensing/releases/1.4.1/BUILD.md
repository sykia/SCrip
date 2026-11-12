# Building Android 1.4.1

Corresponding application sources: `sources/SCrip-1.4.1-source.tar.gz`. The archive contains the code revision used to build the APK; its revision is recorded in `release.json`. The publisher's signing key is not needed to modify sources and is not distributed.

Dependencies: Qt 6.10.1 (Android ARM64, ARMv7, x86-64 and host Qt for building), FFmpeg 7.1.2, OpenSSL 3.1.8 with android_openssl b71f1470962019bd89534a2919f5925f93bc5779; JDK 17, CMake, Ninja, Android SDK 35, Build Tools 35.0.0 and NDK r27c (27.2.12479018). Sources, licenses, configurations and rebuild instructions for these unchanged dependencies are in the `Android` directory of the [base materials](https://github.com/sykia/SCrip/releases/download/v1.4.0/compliance.tar.gz). Verify versions and SHA-256 against `Android-compliance.json`. All FFmpeg ABI configurations are recorded in `SCrip-configurations.txt` inside its source archive. NDK materials include the base LLVM revision, Android patches and build scripts. Java runtime dependencies are defined in `packaging/android/build.gradle` and `maven-coordinates.json` in the Java source archive.

Extract the application sources, install dependencies and set `JAVA_HOME`, `ANDROID_SDK_ROOT`, `ANDROID_NDK_ROOT`, `QT_HOST_PATH`, `SCRIP_QT_ANDROID`, `SCRIP_ANDROID_OPENSSL_ROOT`. The OpenSSL directory must contain `ssl_3/<ABI>/libssl_3.so` and `libcrypto_3.so`. From the source root, run:

```bash
bash scripts/build_android.sh
```

Output: `build/android-universal/android-build/build/outputs/apk/release/android-build-release-unsigned.apk`. To install a modified build, create your own Android keystore and use `zipalign -P 16 -f 4`, followed by `apksigner sign --ks <your-keystore> --out SCrip-custom.apk <aligned-apk>`. Run `adb install -r SCrip-custom.apk` on a device with the same signing key or a clean device. Android cannot upgrade an app using an APK with a different signature; uninstalling the previous app erases its local data.

Qt, FFmpeg and OpenSSL modules are dynamically linked. You can rebuild and replace them, then rebuild/sign the APK with your own key. Service tests: `bash tests/android/run_lifecycle.sh` on a separate Android 15 emulator. General tests: `cmake --preset debug`, `cmake --build --preset debug`, `ctest --test-dir build/debug --output-on-failure`.
