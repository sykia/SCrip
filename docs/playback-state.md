# Playback position and favorite state

The shared `SoundCloud` controller handles these states for desktop and Android.

## Seeking during stream loading

A user seek is retained while Qt reports the stream as loading or non-seekable.
The latest target is applied when the active player becomes ready. It takes
precedence over the saved session position, preserves the playback intent, and is
cleared when stopping or selecting another track. Position events from the old
position cannot overwrite a pending target. Audio effect buffers are cleared
when the seek is applied.

`seekDuringLoadingOverridesRestoredPosition` exercises this with a real WAV
source. `progressSliderClickAndDrag` exercises click and drag handlers extracted
from both production QML interfaces. `tests/run-hls-seek.sh` checks decoded audio
and advancing playback after forward, backward, paused and repeated HLS seeks.

## Seeking into an uncached HLS segment

The local stream proxy buffers HLS media segments and initialization objects
before sending a successful response to the decoder. Previously it forwarded
partial bytes immediately: an interrupted CDN response exposed a truncated
segment to FFmpeg, with no opportunity for the proxy to recover the request.
Foreground downloads now retry up to twice before returning an HTTP error.
Disconnected player requests are cancelled without retrying. Byte-range
responses preserve `Content-Range`; only complete, non-range responses enter
the cache. The existing 8 MiB object limit also bounds segment buffering.

`truncatedSegmentIsRetried` interrupts an upstream response and verifies that
the player receives one complete segment and that subsequent reads use the
cache. `hlsSeekThroughCache`, run with the generated HLS fixture, also seeks
into an uncached segment over a delayed local server and checks decoded PCM
and clock progress before testing fully cached playback.

## Favorite synchronization

After the authenticated profile loads, the controller reads every page of
`/me/likes/tracks`, independently of the visible library page. Known likes update
the current track, queue, visible results, cached lists and home shelves.
Later track selections use the same index. An absent track is considered unliked
only after the complete collection has loaded. Partial or failed responses do
not establish that a track is unliked.

Successful local like changes override older listing data and in-flight sync
responses. Toggle requests consult this shared state even when invoked with an
old queue entry. The index is kept in memory and cleared on sign-out or account
replacement; no account data is stored in the repository.

`likesSyncUpdatesEveryContextAndFutureTracks` covers paginated wrapped responses,
state propagation, future selections, toggling stale entries, late sync data and
sign-out. API reference: [SoundCloud Public API](https://developers.soundcloud.com/docs/api/explorer/open-api).

## Repeat, shuffle and listening history

The existing repeat button cycles through **Off → Track → Queue** on desktop
and Android. Its icon, tooltip/name and accessibility label identify the mode.
`repeatMode` is 0, 1 or 2 respectively. The legacy `repeat` state and
`setRepeat(bool)` command still mean track repeat; `false` selects Off, `true`
selects Track. Existing plugin commands require no changes. Legacy
`player/repeat=true` preferences and boolean session snapshots migrate to Track.
Modes persist in preferences and in account-scoped playback snapshots.

Track repeat applies only to natural end-of-media. Manual Next honors an explicitly
queued next row, otherwise follows forward history or the queue traversal. Queue repeat
wraps at the end; Off stops without discarding the selected row or history.
Sequential traversal starts at the selected row. Shuffle includes every other
row, including rows before that selection, once per cycle. Identical track URNs
in different rows have independent identities. Queue repeat starts another
shuffle cycle after exhaustion and avoids immediately repeating the last row
when another row exists. Selecting a new collection starts a fresh traversal.
Toggling shuffle starts a fresh traversal with the current row already visited.

Previous seeks to zero after 3 seconds, otherwise it follows actual listening
history. Next after Previous replays the retained forward history. Explicit row
selection or “Play next” creates a new history branch. Removed rows are pruned
from history; moving rows retains their identities. Added rows join the remaining
shuffle traversal. Removing the current row uses the planned successor when
available, otherwise retains the existing nearest-row fallback. Queue deduplication and trimming retain identities of surviving
rows. History is bounded to 10,000 transitions, matching the queue size limit.

A single cached next-row decision feeds both preload and actual advancement.
Peeking does not consume it. Queue edits invalidate obsolete preload requests;
late results are rejected by generation checks. Crossfade commits the same row
and history transition. Track repeat uses end-of-media restart instead of
crossfading the track into itself. Changing modes does not stop either audible
player during an existing crossfade; preparation resumes when its ramp finishes.
Android keeps its existing policy of disabling crossfade.

Playback snapshots retain row identities, history and cursor, visited rows,
the planned next row and explicit-next intent. Older snapshots without this
state start a fresh traversal at their restored selection. Invalid identities
fall back to a fresh traversal. Stream URLs are resolved again after restore;
playback is not started automatically.

Continuous Library/Discovery waves retain their existing generated order,
refill, bounded recommendation history and retry behavior. Repeat and shuffle
are ignored while a wave is active; natural end and manual Next continue the
wave. Previous retains the wave's existing seek/previous-row behavior. Starting
a fresh wave clears repeat and shuffle as before. Editing a wave into a fixed
queue retains the existing behavior of stopping recommendation generation.

Deterministic tests cover fixed shuffle plans (including duplicate rows), cycle
exhaustion, repeat boundaries, natural/manual advancement, back/forward history,
queue edits, session migration and restore, real local WAV preload handoff and
crossfade, and mode changes during a ramp. QML tests exercise the production
repeat controls at phone widths 320/412 and desktop widths 1100/1440 without
launching the application executable.

Manual Next at queue exhaustion also completes an in-progress crossfade: the
outgoing decoder is stopped and released, the selected decoder returns to full
volume, and the selected row and history are retained. Resuming cannot restart
the outgoing track. The WAV handoff regression covers both advancement to a
successor and exhaustion during the ramp, including resume after exhaustion.

Desktop system commands and streaming preferences are described in
[System media and streaming preferences](system-media-and-streaming.md).

Plugin `player.next` and `player.previous` use `canGoNext()` and
`canGoPrevious()` before invoking the controller. The command adapter preserves
its existing empty-string success and localized error contract. Queue repeat,
shuffle exhaustion, forward/backward history, wave refill and restarting the
current track therefore follow the same availability rules as system media.
