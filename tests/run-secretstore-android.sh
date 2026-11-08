#!/usr/bin/env bash
# Нужны SDK, JDK, Gradle и уже запущенный эмулятор/тестовое устройство.
set -eu
cd "$(dirname "$0")/.."
root="$PWD"
: "${ANDROID_HOME:?Укажите каталог Android SDK}"
: "${JAVA_HOME:?Укажите каталог JDK}"
gradle="${SCRIP_GRADLE:-gradle}"
work="$root/build/security-android-test"
mkdir -p "$work/res/xml"
cp packaging/android/res/xml/backup_rules.xml packaging/android/res/xml/data_extraction_rules.xml "$work/res/xml/"
cat > "$work/settings.gradle" <<'GRADLE'
pluginManagement { repositories { google(); mavenCentral(); gradlePluginPortal() } }
rootProject.name = 'SCripSecretVaultTest'
GRADLE
cat > "$work/build.gradle" <<'GRADLE'
plugins { id 'com.android.application' version '8.10.1' }
repositories { google(); mavenCentral() }
android {
    namespace 'org.scrip.vaulttest'
    compileSdk 35
    defaultConfig { applicationId 'org.scrip.vaulttest'; minSdk 28; targetSdk 35; versionCode 1; versionName '1' }
    sourceSets.main.java.srcDirs = ['../../tests/android', '../../packaging/android/src']
    sourceSets.main.java.includes = ['**/SecretVault.java', '**/SecretVaultCheck.java']
    sourceSets.main.manifest.srcFile 'AndroidManifest.xml'
    sourceSets.main.res.srcDirs = ['res']
}
GRADLE
cat > "$work/AndroidManifest.xml" <<'XML'
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <application android:label="SCrip Vault Test" android:allowBackup="false" android:fullBackupContent="@xml/backup_rules" android:dataExtractionRules="@xml/data_extraction_rules" />
    <instrumentation android:name="org.scrip.vaulttest.SecretVaultCheck" android:targetPackage="org.scrip.vaulttest" />
</manifest>
XML
"$gradle" -p "$work" --offline assembleDebug > "$root/build/security-java-build.log" 2>&1
adb="$ANDROID_HOME/platform-tools/adb"
"$adb" install -r "$work/build/outputs/apk/debug/SCripSecretVaultTest-debug.apk" > "$root/build/security-android-install.log" 2>&1
"$adb" shell am instrument -w org.scrip.vaulttest/org.scrip.vaulttest.SecretVaultCheck > "$root/build/security-android-vault-tests.log" 2>&1
cat "$root/build/security-android-vault-tests.log"
rg -q 'result=PASS:' "$root/build/security-android-vault-tests.log"
