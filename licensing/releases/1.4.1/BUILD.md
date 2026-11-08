# Сборка Android 1.4.1

Соответствующие исходники самого приложения: `sources/SCrip-1.4.1-source.tar.gz`. Архив содержит ревизию кода, из которой собран APK; номер ревизии записан в `release.json`. Ключ подписи издателя не требуется для изменения исходников и не распространяется.

Зависимости: Qt 6.10.1 (Android ARM64, ARMv7, x86-64 и host Qt для сборки), FFmpeg 7.1.2, OpenSSL 3.1.8 с android_openssl b71f1470962019bd89534a2919f5925f93bc5779; JDK 17, CMake, Ninja, Android SDK 35, Build Tools 35.0.0 и NDK r27c (27.2.12479018). Исходники, лицензии, конфигурации и инструкции пересборки этих неизменённых зависимостей — каталог `Android` в [базовых материалах](https://github.com/sykia/SCrip/releases/download/v1.4.0/compliance.tar.gz). Проверяйте версии и SHA-256 по `Android-compliance.json`. Для библиотек FFmpeg конфигурации всех ABI записаны в `SCrip-configurations.txt` внутри их исходного архива. Для NDK включены базовая ревизия LLVM, Android-патчи и скрипты сборки. Java runtime-зависимости определены в `packaging/android/build.gradle` и в `maven-coordinates.json` архива Java-исходников.

Распакуйте исходники приложения, установите зависимости и задайте пути `JAVA_HOME`, `ANDROID_SDK_ROOT`, `ANDROID_NDK_ROOT`, `QT_HOST_PATH`, `SCRIP_QT_ANDROID`, `SCRIP_ANDROID_OPENSSL_ROOT`. В каталоге OpenSSL ожидаются `ssl_3/<ABI>/libssl_3.so` и `libcrypto_3.so`. Затем из корня исходников:

```bash
bash scripts/build_android.sh
```

Результат: `build/android-universal/android-build/build/outputs/apk/release/android-build-release-unsigned.apk`. Для установки изменённой сборки создайте собственный Android keystore и используйте `zipalign -P 16 -f 4`, затем `apksigner sign --ks <ваш-keystore> --out SCrip-custom.apk <выровненный-apk>`. Установите `adb install -r SCrip-custom.apk` на устройство с таким же ключом или на чистое устройство. Android не обновляет приложение APK с другой подписью; удаление предыдущей установки стирает её локальные данные.

Модули Qt, FFmpeg и OpenSSL связаны динамически. Их можно пересобрать, заменить и собрать/подписать APK своим ключом. Тесты службы: `bash tests/android/run_lifecycle.sh` на отдельном эмуляторе Android 15; общие тесты: `cmake --preset debug`, `cmake --build --preset debug`, `ctest --test-dir build/debug --output-on-failure`.
