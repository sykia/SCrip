# Track selection

The catalog's track rows (including the desktop grid) and playlist, mix and station
previews support selection. Queue controls retain their existing behavior.

- Use **Select tracks** on desktop, or **Track selection** in the Android menu.
- Ctrl-click toggles a row and enters selection mode. Shift-click selects the
  inclusive range from the last toggled row; non-track entries are skipped.
- On Android, hold a track row to enter selection mode. Native button hold handling
  yields to scrolling; an ordinary tap outside selection retains its previous action.
- Tab focuses row selection controls; Space toggles them, Shift-Space extends the
  range. Escape and Android Back leave selection before navigating away. Back first
  closes the destination picker. Pending writes cannot be dismissed with Back.
- **All loaded** selects only track rows already loaded in the current list.
  Pagination retains the selection without automatically selecting new rows.

Selection uses row positions, not track URNs. Repeated tracks in a playlist can be
selected independently. Actions preserve visible list order, including duplicates.
Changing the list, search/filter context or account clears selection. Replacing or
reordering the loaded row prefix also clears it; metadata-only updates retain it.

The compact selection bar offers **Queue**, **Playlist**, a loaded-only selection
control and an exit control. At phone widths its controls wrap rather than overflow.
It uses the existing surface buttons, typography and theme colors. Confirmation
is brief; errors retain the selection and destination title for retry.

## Writes and limits

Queue insertion is one ordered operation after the current track (or at the end
when no track is current), matching the existing single-track queue action. The
operation rejects invalid entries or a resulting queue above 10,000 entries before
changing anything. Existing playback restoration rules remain unchanged.

Playlist additions use the same path as single-track additions: owner validation,
complete paginated snapshots, a second snapshot for conflict detection, URN-only
serialization and the shared request/quota handler. Each batch performs one PUT
for an existing playlist, or one POST for a new private playlist. Batch additions
preserve repetitions; the existing single-track action still avoids adding a track
already present. The complete resulting playlist must fit the 500-track limit;
there is no silent truncation. Playlist writes share the existing serialization
lock. Selection controls are locked while their write is pending.

Operation tokens keep late completions from changing a different selection context.
Account generations discard old playlist and destination responses. A request
already submitted to SoundCloud cannot be recalled by changing the list. Conflict
checks detect changes before the PUT; the API does not provide an atomic conditional
write, so another client can still change the playlist between validation and PUT.

## Verification

Qt Quick tests cover row order, duplicates, ranges, pagination, context resets,
keyboard and pointer input, Android hold versus scrolling, failed writes, stale
completions and selection bar geometry at phone and desktop widths. C++ tests cover
ordered atomic queue insertion, playlist batch serialization, size limits, shared
write exclusion, error responses and old-account completions. Existing playlist
snapshot tests exercise owner, completeness and conflict rejection through the
shared write path. Offscreen tests exercise UI components without launching Sendzh.
