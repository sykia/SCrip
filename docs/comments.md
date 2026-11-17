# Track comments

Sendzh reads and publishes track comments through the official SoundCloud API:
`GET /tracks/{track_urn}/comments` and `POST /tracks/{track_urn}/comments`.
Timestamps are in milliseconds. Untimed comments retain a separate `-1` sentinel.

On desktop, track details place comments beside the track and lyrics below it.
The player comment button toggles a floating panel. Its arrow moves the panel
into the space below desktop navigation when the window has room; smaller
windows retain the right placement. Drag the panel title to move it anywhere
inside the window, and drag any edge or corner to resize it (minimum 240 × 280
pixels, limited by the window size). Custom geometry lasts for the current
session; the placement arrow restores the automatic layout. The narrow left panel opens track details
for writing a comment.

On Android, the expanded player displays a timed comment below the artwork.
Tapping that preview opens a full comments sheet. Track actions also open
comments for a selected track. Android Back closes comments before the player.

Both interfaces offer all comments and a chronological timed view. The latest
comment at or before playback is highlighted for eight seconds. Seeking updates
the selection, including clearing it before the first comment. Manual scrolling
pauses centering until Follow is selected. Timed views load additional pages
while visible, up to 5,000 loaded comments; further pages can be loaded manually.

The composer accepts up to 1,000 characters. Enable Timestamp to attach the
current playback position or enter `minutes:seconds`; disable it for an untimed
comment. A failed submission preserves the draft and displays an error. Comment
publishing is never retried automatically after a response failure. Track changes
and sign-out invalidate pending responses; pagination is restricted to the
selected track's official comments endpoint.

Available actions are a timestamp seek, profile navigation, copying comment text,
and a reply using an `@permalink` mention. The public API does not expose comment
likes, deletion, or threaded replies, so Sendzh does not present those actions.
No comment cache or draft is persisted to disk.

Validation uses mocked API responses and rendered desktop/mobile QML fixtures.
Live account publishing and the Android system keyboard require device checks.

`commentable: false` disables publishing independently of comment visibility.
`reveal_comments: false` hides loaded comments and prevents reading further pages;
it is shown as an author visibility choice, not as an empty discussion. Missing
or null fields impose no inferred restriction. Metadata changes invalidate hidden
read responses, while track/account changes invalidate the entire comment context.
A hidden discussion may still permit writing if `commentable` allows it; a posted
comment is not added to the visible list while visibility is disabled.
