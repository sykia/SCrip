#!/usr/bin/env bash
# Изолированная Secret Service: пользовательская связка ключей не затрагивается.
set -eu
cd "$(dirname "$0")/.."
root="$PWD"
work=$(mktemp -d "$root/build/secretstore-test.XXXXXX")
trap 'rm -rf "$work"' EXIT
export XDG_DATA_HOME="$work/data" XDG_CONFIG_HOME="$work/config" XDG_CACHE_HOME="$work/cache"
export XDG_RUNTIME_DIR="$work/runtime" GNOME_KEYRING_CONTROL="$work/control"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME"
mkdir -m 700 "$XDG_RUNTIME_DIR" "$GNOME_KEYRING_CONTROL"
DBUS_SESSION_BUS_ADDRESS="unix:path=$work/unavailable" "$root/build/debug/secretstore_tests" unavailable
dbus-run-session -- bash -eu -c '
    printf "%s" "fixture-keyring-password" | gnome-keyring-daemon --unlock --components=secrets --control-directory "$GNOME_KEYRING_CONTROL" >/dev/null
    "$1/build/debug/secretstore_tests" roundTrip
' bash "$root"
