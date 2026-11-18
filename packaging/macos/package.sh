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
iconset="$build_dir/Sange.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$project_dir/qml/assets/blob.png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z "$double" "$double" "$project_dir/qml/assets/blob.png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$package_dir/Sange.app/Contents/Resources/Sange.icns"
macdeployqt "$package_dir/Sange.app" "-qmldir=$project_dir/qml" -always-overwrite
# Test packages use ad-hoc signing. Public distribution requires Developer ID + notarization.
codesign --force --deep --sign - "$package_dir/Sange.app"
codesign --verify --deep --strict "$package_dir/Sange.app"
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
