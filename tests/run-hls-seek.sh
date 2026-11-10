#!/usr/bin/env bash
# Синтетический fMP4 HLS: до исправления FFmpeg 7.1.2 тест падает на seek 60 с.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
fixture="$root/build/hls-seek-fixture"
mkdir -p "$fixture"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i sine=frequency=440:sample_rate=44100 \
    -t 90 -c:a aac -b:a 160k -f hls -hls_time 10 -hls_playlist_type vod \
    -hls_segment_type fmp4 "$fixture/test.m3u8"
SCRIP_HLS_TEST_PLAYLIST="$fixture/test.m3u8" "${1:-$root/build/debug/scrip_hls_seek_tests}"
