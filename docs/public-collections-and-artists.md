# Public profile collections and track artist credits

The public profile uses the existing catalog loader, cache, quota scheduler and
`next_href` pagination. Only the selected section is requested. Desktop and Android share one horizontal row of four profile tabs with a smoothly
sliding underline. Likes and reposts open short type menus, keeping secondary types out of the tab row. Phones retain desktop labels,
font sizes and spacing, with horizontal scrolling when needed. Both interfaces expose the same seven destinations:

| Destination | Public API route beneath `/users/{user_urn}` |
| --- | --- |
| Uploaded tracks | `/tracks` |
| Created playlists | `/playlists` |
| Liked tracks | `/likes/tracks` |
| Liked playlists | `/likes/playlists` |
| Liked stations | `/likes/system-playlists` |
| Reposted tracks | `/reposts/tracks` |
| Reposted playlists | `/reposts/playlists` |

User URNs are encoded as path components. The existing personal `/me` routes are
retained. Public collection responses never populate personal like or repost
registries. Rows continue to use the viewer's known like state and existing
selection actions. Listing generations reject old pages after switching profiles,
sections or accounts; a separate generation guards profile-detail responses.

`collectionStatus` distinguishes loading, ready, empty, unavailable (403/404) and
error. Malformed success responses are errors, not empty collections. A failed
next page preserves loaded rows and supports retry. Repeated page URLs stop
pagination. Quota failures use the existing shared scheduler; unavailable sections
are never synthesized from another endpoint.

## Artist identity

`TrackMetadata::displayArtist` returns a trimmed, nonempty string from
`metadata_artist`, excluding control-only/invalid values, then falls back to the
uploader's trimmed `user.username`. Old saved queues without the field need no
migration. The raw track and its `user` object retain their identities. The
plugin snapshot contract is unchanged.

Lists, playlist contents, queue, player, lyric lookup, Discord and Android media
publication use this helper. The detail view and action menu identify the uploader
separately when the credit differs; an artist credit does not navigate to the
uploader's profile. Follow actions always target the uploader's URN. Receiving
updated credits in catalog/playlist tracks refreshes the selected and queued
track without restarting audio, and persists updated queue metadata. Open lyric
views refresh when the current track's credit changes.

## Sources and validation

Checked on 2026-11-17 against the official
[OpenAPI specification](https://github.com/soundcloud/api/blob/master/openapi/api.yaml)
and [API guide](https://developers.soundcloud.com/docs/api/guide).

Tests cover all routes, pagination/retry, quota scheduling, stale responses,
account changes, error/empty/unavailable states, personal-state isolation, artist
fallback/validation, legacy queue objects, live credit updates, lyric parameters
and Discord payloads. Offscreen desktop and Android QML tests save screenshots
under `build/debug/`; these test harnesses do not launch the SCrip executable.
Native Android media/JNI behavior and Windows integration require their respective
platforms.
