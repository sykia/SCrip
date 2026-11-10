#!/usr/bin/env bash
# Универсальная Release-сборка Android. Запуск из корня исходников.
set -eu
base="${SCRIP_ANDROID_TOOLS:-$PWD/build/android-tools}"
export ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$base/sdk}"
export ANDROID_HOME="$ANDROID_SDK_ROOT"
export ANDROID_NDK_ROOT="${ANDROID_NDK_ROOT:-$ANDROID_SDK_ROOT/ndk/27.2.12479018}"
export JAVA_HOME="${JAVA_HOME:-$base/jdk}"
export PATH="$JAVA_HOME/bin:$PATH"
export GRADLE_USER_HOME="${GRADLE_USER_HOME:-$base/gradle}"
export SCRIP_ANDROID_OPENSSL_ROOT="${SCRIP_ANDROID_OPENSSL_ROOT:-$base/openssl}"
export QT_HOST_PATH="${QT_HOST_PATH:-$base/qt/6.10.1/gcc_64}"
qt_android="${SCRIP_QT_ANDROID:-$base/qt/6.10.1/android_arm64_v8a}"
"$PWD/scripts/build_android_ffmpeg.sh"
"$qt_android/bin/qt-cmake" -S . -B build/android-universal -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH="$QT_HOST_PATH" -DANDROID_PLATFORM=android-28 \
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON '-DQT_ANDROID_ABIS=arm64-v8a;armeabi-v7a;x86_64' \
    -DQT_ANDROID_TARGET_SDK_VERSION=35 -DQT_ANDROID_COMPILE_SDK_VERSION=35
cmake --build build/android-universal --target apk -j4
