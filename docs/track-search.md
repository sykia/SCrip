# Advanced track search

The desktop and Android track tabs share a compact expandable filter panel.
Fields are drafts: only Apply or Enter submits them. Reset clears and applies
empty filters. Applied values remain visible when the panel is collapsed.
Profiles, playlists and SoundCloud URL resolution do not use track filters.
Range endpoints share one outlined control, and upload dates share a single row.
A quiet disclosure header shows the active count; reset and apply use separate
positions so the form stays compact on narrow screens.
Android caps the panel height and scrolls its contents on small screens.

## API contract

Verified on 2026-11-16 against the official
[OpenAPI specification](https://github.com/soundcloud/api/blob/master/openapi/api.yaml)
and [API guide](https://developers.soundcloud.com/docs/api/guide).

| UI | GET /tracks parameter | Values |
| --- | --- | --- |
| Genres | `genres` | Comma-separated free-text genres; no documented enum |
| Tags | `tags` | Comma-separated tags |
| Duration, seconds | `duration[from]`, `duration[to]` | Inclusive integer milliseconds; UI converts seconds × 1000 |
| BPM | `bpm[from]`, `bpm[to]` | Inclusive integers |
| Upload dates, UTC | `created_at[from]`, `created_at[to]` | `yyyy-MM-dd HH:mm:ss`; start 00:00:00, end 23:59:59 |
| Full tracks only | `access=playable` | Access enum: playable, preview, blocked |

Empty endpoints are omitted. Numeric inputs must be nonnegative integers; the
client uses a conservative signed 32-bit API range (duration input up to 2147483
seconds). These are client validation limits, not documented SoundCloud musical
limits. Reversed ranges and invalid calendar dates are rejected without requests.
Filter-only searches are supported. Genre and BPM depend on supplied metadata;
missing or inaccurate metadata can omit otherwise relevant tracks. Full access
is relative to the requesting account and region, not a permanent availability
guarantee. No local audio analysis or additional discovery requests are used.

Submitted parameters form the listing cache key. Pagination retains that
snapshot even when `next_href` only supplies a cursor. Existing listing generation
checks reject stale successes and failures. Requests continue through the shared
GET coalescing and quota handling path; editing fields consumes no API quota.

Tests cover request serialization, boundaries, invalid dates/ranges, reset,
unfiltered profile/playlist requests, pagination snapshots and stale replies.
Qt Quick tests exercise the standalone panel at 280, 372 and 960 logical pixels
(the phone widths reserve 40 pixels for page margins), plus Enter and reset.
The Android layout fixture also checks the embedded panel at 320×640 and
412×860, including its scroll viewport height. Screenshots are generated in the
build test working directory. These tests do not launch the Sange application
or replace testing on an Android device. No live API requests are needed.
