# Track information and waveform navigation

Track information reuses the desktop About track section and an Android menu
sheet. The sheet snapshots the selected track (not the playing track), scrolls
independently, respects safe areas and closes before underlying screens on Back.
Description is plain text and appears once. Artist credits remain separate from
the uploader; the uploader still opens their profile. No field-level requests
are made.

Only correctly typed, nonempty fields are shown. BPM must be finite and positive
(up to 1000). Release dates use the API's release_year/month/day, with validated
calendar days and ISO precision YYYY, YYYY-MM or YYYY-MM-DD. Upload timestamps
and the release catalogue number are never used as release dates. License codes
have readable labels; unknown codes are explicitly marked. Tags are space
separated with double-quoted multiword tokens; incomplete quoted tails are
ignored, duplicates removed, and input/token counts bounded.

## Official source and supported format

The [SoundCloud public OpenAPI specification](https://github.com/soundcloud/api/blob/master/openapi/api.yaml)
defines the fields and tag quoting and illustrates waveform_url with an HTTPS
PNG on wave.sndcdn.com. It does not document a sample-array schema, a JSON
waveform endpoint, or waveform-to-audio timing offsets. SCrip therefore requests
only the supplied PNG URL, stretches the full image over the full track duration
and colors its alpha silhouette. It never derives another endpoint, downloads
audio for analysis, or synthesizes a decorative waveform. Other formats and
unavailable images silently retain the working progress line. Opaque PNGs are
unsupported because they cannot provide a usable alpha silhouette.

TrackProgress is shared by the desktop track page and expanded Android player.
The compact player and live spectrum are unchanged. Clicking or releasing a
horizontal drag seeks; a vertical/cancelled gesture does not. Previewing does not
send repeated seek commands. Track identity changes invalidate an in-flight
gesture. Arrow keys move five seconds; Home/End select the endpoints. For the
current track, seeking preserves pause and uses the existing pending-seek logic
while streams load. For another selected track, the desktop page follows its
existing lyric-navigation policy: play that selected track, then queue the seek.

## Transport and limits

The native waveform item owns a separate network manager, with no API OAuth
headers, cookies, shared API cache or credential reuse. Only HTTPS/443 PNG URLs
on wave.sndcdn.com and legacy w1–w4.sndcdn.com are accepted, without user info,
query or fragment. Redirects are manual, revalidated at every hop, and limited to
three. Requests have an eight-second transfer timeout and a ten-second wall
clock deadline per hop. Bodies are capped at 1 MiB both by advertised length and
actual bytes. Only PNG signatures are decoded, after checking dimensions (8192
by 1024, at most two million pixels). Display images are reduced to at most 2048
by 256. Ancillary metadata chunks are stripped before decoding (including
compressed text and ICC profiles), and chunk counts are bounded. An in-memory LRU cache is capped at 8 MiB; URLs/images are never persisted
or sent to plugins or system media integration. Source changes abort the old
reply and invalidate its generation. Closing the containing page clears its
source. Rendering uses cached tinted images and clipping, without rebuilding
wave geometry for position updates.

## Local checks

`track_details_tests` covers metadata types, partial/invalid dates, quoted tags,
license fallback, PNG failures and bounds, URL policy, request headers, redirect
validation, cache reuse, body limits, cancellation and stale replies using a
fake network. `tst_trackprogress.qml` exercises fallback, click/drag, keyboard,
track changes during a gesture and touch scrolling. Android layout fixtures
exercise the information sheet at phone and landscape sizes, selection identity
and Back. Existing playback tests cover pending seeks and paused local playback.

Full suite:

```sh
cmake --build --preset debug
bash tests/run-hls-seek.sh --generate-only
SCRIP_HLS_TEST_PLAYLIST="$PWD/build/hls-seek-fixture/test.m3u8" \
  dbus-run-session -- ctest --test-dir build/debug --output-on-failure
git diff --check
```

Device builds and real-device gesture behavior must additionally be checked on
Android and Windows when their toolchains/devices are available.
