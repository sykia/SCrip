#!/usr/bin/env bash
# Запускать из корня проекта после сборки универсального APK; нужен запущенный эмулятор.
set -eu
base="$PWD/build/android-tools"
export JAVA_HOME="${JAVA_HOME:-$base/jdk}"
export ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$base/sdk}"
export ANDROID_HOME="$ANDROID_SDK_ROOT"
export GRADLE_USER_HOME="${GRADLE_USER_HOME:-$base/gradle}"
export PATH="$JAVA_HOME/bin:$PATH"
serial="${ANDROID_SERIAL:-emulator-5554}"
case "$serial" in emulator-*) ;; *) echo 'Для проверки используйте отдельный Android-эмулятор'; exit 1 ;; esac
tools="$ANDROID_SDK_ROOT/build-tools/35.0.0"
platform="$ANDROID_SDK_ROOT/platforms/android-35/android.jar"
testdir="$PWD/build/android-lifecycle"
mkdir -p "$testdir/classes" "$testdir/dex"
cat > "$testdir/probe.gradle" <<'GRADLE'
allprojects {
    afterEvaluate { project ->
        if (project.plugins.hasPlugin('com.android.application')) {
            project.android.defaultConfig.applicationId = 'org.scrip.lifecycle'
            project.android.applicationVariants.all { variant ->
                variant.outputs.all { outputFileName = 'scrip-probe.apk' }
            }
        }
    }
}
GRADLE
(cd build/android-universal/android-build && ./gradlew assembleRelease --init-script "$testdir/probe.gradle")
cat > "$testdir/AndroidManifest.xml" <<'XML'
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="org.scrip.lifecycletest">
    <uses-sdk android:minSdkVersion="28" android:targetSdkVersion="35" />
    <application android:label="SCrip lifecycle tests" />
    <instrumentation android:name="org.scrip.test.PlaybackLifecycleTest" android:targetPackage="org.scrip.lifecycle" />
</manifest>
XML
javac -source 8 -target 8 -cp "$platform" -d "$testdir/classes" tests/android/PlaybackLifecycleTest.java
"$tools/d8" --lib "$platform" --output "$testdir/dex" "$testdir/classes/org/scrip/test/PlaybackLifecycleTest.class"
"$tools/aapt" package -f -M "$testdir/AndroidManifest.xml" -I "$platform" -F "$testdir/test-unsigned.apk"
(cd "$testdir/dex" && "$tools/aapt" add "$testdir/test-unsigned.apk" classes.dex)
if [ ! -f "$testdir/test.keystore" ]; then
    keytool -genkeypair -keystore "$testdir/test.keystore" -storepass android -keypass android -alias lifecycle -dname 'CN=SCrip lifecycle test' -keyalg RSA -validity 3650
fi
"$tools/apksigner" sign --ks "$testdir/test.keystore" --ks-pass pass:android --out "$testdir/test.apk" "$testdir/test-unsigned.apk"
"$tools/zipalign" -P 16 -f 4 build/android-universal/android-build/build/outputs/apk/release/scrip-probe.apk "$testdir/aligned.apk"
"$tools/apksigner" sign --ks "$testdir/test.keystore" --ks-pass pass:android --out "$testdir/probe.apk" "$testdir/aligned.apk"
"$ANDROID_SDK_ROOT/platform-tools/adb" -s "$serial" install -r "$testdir/probe.apk"
"$ANDROID_SDK_ROOT/platform-tools/adb" -s "$serial" install -r "$testdir/test.apk"
result=$("$ANDROID_SDK_ROOT/platform-tools/adb" -s "$serial" shell am instrument -w org.scrip.lifecycletest/org.scrip.test.PlaybackLifecycleTest)
printf '%s\n' "$result"
[[ "$result" == *'PASS:'* && "$result" != *'FAIL:'* ]]
