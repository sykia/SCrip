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
