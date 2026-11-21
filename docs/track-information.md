# Track information and waveform navigation

Track information reuses the desktop About track section and an Android menu
sheet. The Android track actions menu uses a bottom-edge Qt Quick Drawer: a
downward swipe dismisses it from the header or an action row when its list is at
the top, while a scrolled list keeps vertical scrolling. The track actions header stays
fixed above the scrolling action list. Explicit per-edge Drawer padding keeps the
cover and rows inside the rounded sheet on Qt Basic, including safe-area insets.
Likes use an accent icon and count without a full-width selected background.
The information sheet snapshots the selected track (not the playing track), scrolls
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
waveform endpoint, or waveform-to-audio timing offsets. Sange therefore requests
only the supplied PNG URL, stretches the full image over the full track duration
and normalizes its alpha silhouette to a centered outward envelope. The resting
wave is smoothed and kept compact; holding the progress control reveals the
detailed envelope. Both waveform and fallback reserve the same 52-pixel control
height, so missing or late images do not move the player controls. It never derives another endpoint, downloads
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

## Availability and visibility

Desktop collection rows and Android track rows show restricted API playback
(`Only preview` or `Unavailable through API`) and explicitly private tracks.
Track information shows full API playback status, visibility when supplied, and
an explanation of restrictions. Missing fields remain unknown rather than being
interpreted as deletion or a subscription requirement. Explicit `streamable: false`
is treated as unavailable even alongside `access: playable`.

SoundCloud documents `access` as `playable`, `preview`, or `blocked`:
https://developers.soundcloud.com/docs/api/guide
A blocked track can be paywalled, region-restricted, or restricted by its creator;
the access field does not identify the cause. A preview alone is not conclusive
proof of a Go+ requirement. Private visibility (`sharing: private`) is independent
of playback availability.

A missing track or HTTP 404 cannot reliably distinguish deletion, a private track
without permission, removal, or a hidden upload. Tracks omitted entirely from API
collections cannot be reconstructed from those responses. No deleted-track label
is inferred, and the UI does not promise that a subscription enables API playback.
See https://help.soundcloud.com/hc/en-us/articles/115003563948-Can-t-find-a-track-anymore
and https://help.soundcloud.com/hc/en-us/articles/115003448167-Embedding-a-SoundCloud-Go-Track.
Availability reflects the last received metadata and current API session; it is
not an additional background audit of the library.

Preview-only collection rows also display a theme-accent dot in a subtle circular badge beside the title on
desktop and Android. Its tooltip explains that only a fragment is available and
that the full version may require Go+. The indicator supports keyboard focus and
an accessible text label; on touch screens the tooltip appears while pressed.
It is shown from received `access: preview` metadata before playback is attempted.

The preview badge follows the active theme accent, with a translucent background
and outline. Hover, press, and keyboard focus strengthen the same accent treatment.
