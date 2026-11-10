#!/usr/bin/env bash
# Исправленный MOV/HLS-demuxer FFmpeg 7.1.2 для Qt 6.10.1; остальные библиотеки Qt сохраняются.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
base="${SCRIP_ANDROID_TOOLS:-$root/build/android-tools}"
work="$root/build/android-ffmpeg-seek"
source_dir="${SCRIP_FFMPEG_SOURCE:-$work/original/ffmpeg-7.1.2}"
ndk="${ANDROID_NDK_ROOT:-$base/sdk/ndk/27.2.12479018}"
ssl="${SCRIP_ANDROID_OPENSSL_ROOT:-$base/openssl}/ssl_3"
llvm="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
if [[ ! -f "$source_dir/libavformat/mov.c" && -z "${SCRIP_FFMPEG_SOURCE:-}" ]]; then
    mkdir -p "$work/original"
    archive="$work/ffmpeg-7.1.2.tar.xz"
    curl --fail --location --output "$archive" https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz
    echo "089bc60fb59d6aecc5d994ff530fd0dcb3ee39aa55867849a2bbc4e555f9c304  $archive" | sha256sum --check
    tar -xJf "$archive" -C "$work/original"
fi
[[ -f "$source_dir/libavformat/mov.c" ]] || { echo "Нужны исходники FFmpeg 7.1.2" >&2; exit 1; }
[[ "$(cat "$source_dir/RELEASE")" == "7.1.2" ]] || { echo "Поддерживается FFmpeg 7.1.2" >&2; exit 1; }
# Копия исходников и patch входят в воспроизводимый результат сборки.
mkdir -p "$work"
patch_digest="$(sha256sum "$root/packaging/android/patches/ffmpeg-7.1-hls-seek.patch" | cut -d' ' -f1)"
if [[ ! -f "$work/src/.scrip-seek-patched" || "$(cat "$work/src/.scrip-seek-patched")" != "$patch_digest" ]]; then
    mkdir -p "$work/src"
    cp -a "$source_dir/." "$work/src/"
    if ! grep -q 'Discard current fragment index' "$work/src/libavformat/mov.c"; then
        patch -d "$work/src" -p1 < "$root/packaging/android/patches/ffmpeg-7.1-hls-seek.patch"
    fi
    echo "$patch_digest" > "$work/src/.scrip-seek-patched"
fi
if (($# == 0)); then set -- arm64-v8a armeabi-v7a x86_64; fi
for target_abi in "$@"; do
    case "$target_abi" in
        arm64-v8a) arch=aarch64; triple=aarch64-linux-android; qt=android_arm64_v8a ;;
        armeabi-v7a) arch=arm; triple=armv7a-linux-androideabi; qt=android_armv7 ;;
        x86_64) arch=x86_64; triple=x86_64-linux-android; qt=android_x86_64 ;;
        *) echo "Неизвестный ABI: $target_abi" >&2; exit 1 ;;
    esac
    out="$work/$target_abi"
    mkdir -p "$out"
    cd "$out"
    cc="$llvm/${triple}28-clang"
    if [[ ! -f config.h ]]; then
        ../src/configure --disable-programs --disable-doc --disable-debug --disable-asm \
            --disable-autodetect --enable-openssl --enable-zlib --enable-pic --enable-static \
            --enable-shared --enable-cross-compile --target-os=android --arch="$arch" \
            --cc="$cc" --ar="$llvm/llvm-ar" --ranlib="$llvm/llvm-ranlib" \
            --extra-cflags="-I$ssl/$target_abi/include -I$ssl/include" \
            --extra-ldflags="-L$ssl/$target_abi" > configure.log 2>&1
    fi
    make -j"${SCRIP_BUILD_JOBS:-4}" libavformat/libavformat.a libavformat/libavformat.ver > make.log 2>&1
    "$cc" -shared -Wl,-soname,libavformat.so -Wl,-z,max-page-size=16384 \
        -Wl,--version-script,libavformat/libavformat.ver -Wl,--no-undefined \
        -o libavformat.so -Wl,--whole-archive libavformat/libavformat.a -Wl,--no-whole-archive \
        -L"$base/qt/6.10.1/$qt/lib" -lavcodec -lavutil \
        -L"$ssl/$target_abi" -l:libssl_3.so -l:libcrypto_3.so -lm -lz -ldl -pthread
    "$llvm/llvm-strip" --strip-unneeded libavformat.so
    echo "Готово: $target_abi/libavformat.so"
done
