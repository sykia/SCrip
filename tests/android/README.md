# Android background service checks

After building with `bash scripts/build_android.sh`, start a separate Android 15 emulator and run from the project root:

```bash
bash tests/android/run_lifecycle.sh
```

Requires SDK Build Tools 35.0.0 and Android SDK 35. Set `JAVA_HOME`, `ANDROID_SDK_ROOT`, `GRADLE_USER_HOME` and `ANDROID_SERIAL` (emulator only) as needed.

The test installs separate `org.scrip.lifecycle` and `org.scrip.lifecycletest` packages without replacing the user's app. It checks the real Java bridge, foreground service, wake lock and MediaSession using synthetic Qt player snapshots: playback, backgrounding, track transitions, buffering beyond 30 seconds, pause, resume and full stop. It also checks preparation cancellation before service creation to rule out a startForegroundService timeout crash. A separate test signing key is created in `build/android-lifecycle/`.

Before testing, the emulator must not contain `org.scrip.lifecycle` or `org.scrip.lifecycletest` packages signed with another key. The test does not remove them automatically. Real stream playback and phone firmware require separate checks.
