# Queue actions

The desktop queue inspector and Android queue sheet expose the same actions menu.
Menus use the existing desktop `TrackAction` rows and Android `MenuRow` rows,
including their icons and spacing. The shared `QueueActions.qml` title dialog
uses existing theme colors, transitions and buttons with a 48 px title field.

- **Save as a new playlist** asks for a title and creates a private ordinary
  playlist (`set_type: playlist`) through the existing authenticated request path.
  The request snapshots the entire queue, preserving order and duplicate tracks.
  Empty queues and signed-out sessions cannot save. Titles are limited to 255
  characters. Invalid entries and queues exceeding 500 tracks fail as a whole;
  no tracks are silently dropped. Errors retain the title for retry.
- **Remove duplicates** keeps first occurrences except for the current track:
  its playing occurrence takes precedence over earlier and later duplicates.
  The current index is recalculated without restarting or seeking the player.
- **Clear after current** retains the complete prefix through the current entry.
  It is disabled without a valid current entry or a remaining tail.

Bulk edits stop wave continuation, invalidate outstanding recommendation and
preload generations, settle any active crossfade onto the current incoming
player, and persist the edited queue. Starting a wave again is an explicit user
action. Playback position is preserved. Playlist writes share the existing write
lock; repeated submissions cannot issue concurrent writes. Account resets clear
the operation state and invalidate callbacks from the previous account.

SoundCloud's official [Playlist Limits](https://help.soundcloud.com/hc/en-us/articles/360005673974-Playlist-Limits)
sets a hard limit of 500 tracks. Its [API guide](https://developers.soundcloud.com/docs/api/guide)
documents `POST /playlists`; the
[official OpenAPI specification](https://github.com/soundcloud/api/blob/master/openapi/api.yaml)
describes the playlist payload. Live service acceptance, including duplicate
handling and unavailable tracks, remains subject to SoundCloud validation.

Regression coverage lives in `tests/tst_soundcloud.cpp`: stable ordering, current
occurrence priority, unchanged position/player, queue boundaries, write errors,
parallel-submit protection, account-reset callbacks and isolated phone/desktop
layouts. Layout tests render test windows, not the application executable.

## Undo

The queue menu offers a single undo for row removal, duplicate removal and clearing
the future tail. Snapshots include distinct occurrence IDs and shuffle traversal
state. Undo restores rows and their order without invoking play, seek or resume;
the currently active occurrence stays selected, even after removing another row.
Undoing removal of the playing row retains its already-started successor. Undoing
an empty queue restores entries without starting playback. Recommendation
continuation stays stopped; restoring the queue does not restart a wave.

Subsequent manual queue mutations, queue replacement and account changes invalidate
undo. Preloaded streams are invalidated and prepared against the restored order.
The settings for shuffle and repeat are not changed. No undo snapshot is persisted.
