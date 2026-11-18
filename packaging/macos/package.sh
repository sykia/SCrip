#!/usr/bin/env bash
set -euo pipefail
build_dir="${1:-build/macos}"
architecture="${2:-$(uname -m)}"
case "$architecture" in arm64|x86_64) ;; *) echo 'Expected arm64 or x86_64' >&2; exit 1;; esac
project_dir="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="$(cd "$build_dir" && pwd)"
version="$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$build_dir/Sange.app/Contents/Info.plist")"
package_dir="$build_dir/package"
mkdir -p "$package_dir"
rm -rf "$package_dir/Sange.app"
ditto "$build_dir/Sange.app" "$package_dir/Sange.app"
mkdir -p "$package_dir/Sange.app/Contents/Resources/legal"
cp "$project_dir/LICENSE" "$project_dir/THIRD_PARTY_NOTICES.md" "$project_dir/TRADEMARKS.md" "$package_dir/Sange.app/Contents/Resources/legal/"
cp "$project_dir/packaging/macos/DEPENDENCIES.md" "$package_dir/Sange.app/Contents/Resources/legal/"
source_dir="${SANGE_RELEASE_SOURCES:?Set SANGE_RELEASE_SOURCES to the corresponding release sources directory}"
python3 "$project_dir/packaging/macos/collect_notices.py" "$source_dir" "$package_dir/Sange.app/Contents/Resources/legal/upstream"
iconset="$build_dir/Sange.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$project_dir/qml/assets/blob.png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z "$double" "$double" "$project_dir/qml/assets/blob.png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$package_dir/Sange.app/Contents/Resources/Sange.icns"
macdeployqt "$package_dir/Sange.app" "-qmldir=$project_dir/qml" -always-overwrite
# QML scanning pulls in QtSql's optional drivers, including third-party clients
# absent from the Qt kit. Sange does not use those databases; retain only SQLite.
python3 - "$package_dir/Sange.app/Contents/PlugIns/sqldrivers" <<'PYTHON'
from pathlib import Path
import sys
for plugin in Path(sys.argv[1]).glob('*.dylib'):
    if plugin.name != 'libqsqlite.dylib':
        plugin.unlink()
PYTHON
{
    echo 'Qt 6.12.0 official clang_64 online installer kit; no downstream library patches.'
    echo 'Application and dependency rebuild instructions: DEPENDENCIES.md and corresponding source archives.'
    echo 'Xcode toolchain:'
    xcodebuild -version
    echo 'Qt installed build configuration:'
    find "${QT_ROOT_DIR:?}/mkspecs" -maxdepth 1 -name 'q*.pri' -exec cat {} \;
    echo 'FFmpeg binary version and configure arguments:'
    for library in "$package_dir/Sange.app/Contents/Frameworks/"libav*.dylib; do
        strings "$library" | grep -E '^((--prefix|--disable|--enable)|FFmpeg version|Lav[cfu]|9\.0\.1)' || true
    done
} > "$package_dir/Sange.app/Contents/Resources/legal/BUILD-INFO.txt"
# Test packages use ad-hoc signing. Public distribution requires Developer ID + notarization.
codesign --force --deep --sign - "$package_dir/Sange.app"
codesign --verify --deep --strict "$package_dir/Sange.app"
python3 "$project_dir/packaging/macos/check_bundle.py" "$package_dir/Sange.app" "$architecture" > "$package_dir/bundle-$architecture.json"
ln -sfn /Applications "$package_dir/Applications"
image="Sange-$version-macOS-$architecture.dmg"
# Only the app and Applications shortcut belong in the disk image.
stage="$build_dir/dmg-stage"
mkdir -p "$stage"
rm -rf "$stage/Sange.app"
ditto "$package_dir/Sange.app" "$stage/Sange.app"
ln -sfn /Applications "$stage/Applications"
hdiutil create -volname Sange -srcfolder "$stage" -ov -format UDZO "$package_dir/$image"
(cd "$package_dir" && shasum -a 256 "$image" > "$image.sha256")
python3 "$project_dir/packaging/macos/compliance.py" --bundle "$package_dir/Sange.app" --image "$package_dir/$image" --architecture "$architecture" --assets "$source_dir" --manifest "$package_dir/macOS-$architecture-compliance.json"
