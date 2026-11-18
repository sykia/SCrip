[Home](Home) › Features

Sange uses SoundCloud's catalogue and the access available to the signed-in account. Individual tracks and metadata may be unavailable through the API.

## Discovery

- Home mixes, recommendations, releases and listening history.
- A continuous personal wave with Familiar, Balance and Discovery preferences.
- Track and uploading-account exclusions for recommendations, with restoration in settings.
- Artist stations when SoundCloud supplies a station identity; related-track selections when a station is unavailable.

Recommendations combine SoundCloud related-track results with local ordering. Sange does not train a recommendation model or analyze audio to identify similar music. Exclusions affect recommendations, while explicitly opened collections and manual playback remain available.

## Search and library

- Search for tracks, playlists and people; paste a SoundCloud resource URL into search.
- Track filters: genres, tags, duration, BPM, UTC upload dates and full-track access.
- Browse likes, feed, history, playlists, profiles and saved stations.
- Filter loaded collection rows, sort them and save search configurations.
- Explicitly search a whole collection through paginated loading, with progress and cancellation.
- Select multiple tracks and add them to the queue or a playlist.

Local filtering searches only loaded rows. Whole-collection search is a separate action and can pause for explicit continuation. Sorting an incomplete collection covers only its loaded rows.

## Playback and queue

- Play, pause, seek, previous/next, volume, shuffle, repeat track and repeat queue.
- Reorder the queue and use **Play next**.
- Save the queue as a playlist, remove duplicates, or clear upcoming tracks; undo queue cleanup from the same menu.
- **Standard / Data saver** stream preferences and a **Preload** switch.
- Desktop crossfade, requiring preloading; Android keeps crossfade disabled.
- System media controls and Android background playback.
- Seek using the supplied track waveform in the desktop track page or expanded Android player. If the image is missing or unsupported, the normal progress line remains available.

Stream preferences select among formats actually returned by SoundCloud. They do not guarantee a bitrate. Preview-only tracks are identified; full playback does not silently substitute a preview. Preloading and temporary HLS caching are not a persistent offline library.

## Profiles, playlists and comments

- Artist profiles, followers/following, related artists and public profile links.
- Follow or unfollow artists from profiles and player controls. Subscriptions belong to the signed-in SoundCloud account; actions wait for initial synchronization.
- Track likes and reposts; supported playlist likes and reposts.
- Create playlists and edit owned playlists: title, description, privacy, order and track removals.
- Read track comments, publish timed or untimed comments, seek to timestamps and open commenter profiles.

Playlist editing requires a complete playlist and ownership checks. The client enforces a 500-track resulting playlist limit. Concurrent changes can block a save; the draft remains available. Comment visibility and publishing respect the track's supplied metadata.

## Track information

Open **About track** on desktop or the track information sheet on Android to inspect supplied metadata: description, artist credits, uploader, genre, tags, BPM, release date, license and access/visibility information. Missing fields are omitted; upload dates are not substituted for release dates. Preview or blocked access does not identify the underlying restriction by itself.

The waveform uses SoundCloud's supplied PNG when supported, rather than an invented waveform or local audio analysis. Click or finish a horizontal drag to seek; keyboard Left/Right moves five seconds and Home/End selects the endpoints. Seeking the current track preserves its paused state.

## Links and updates

- Paste SoundCloud resource URLs into search. Desktop packages register the `scrip://` handler; Android also accepts shared text and supported web links.
- An optional desktop browser extension forwards ordinary SoundCloud link clicks to Sange; it is not installed automatically.
- Check for updates, download the platform package and pass it to the system installer from Settings. Installation remains a separate user action.

See [link setup](Installation#opening-soundcloud-links) and [in-app updates](Installation#updating-from-settings) for platform requirements.

## Appearance and extensions

- Theme and accent preferences, artwork styling and motion controls.
- Declarative panels, themes and commands through the [Plugin System](Plugin-System).
- Trusted QML extensions for custom UI and behavior, with explicit full-access permission.
- Optional desktop Discord Rich Presence, disabled by default.

## Platform differences

| Capability | Linux / Windows | Android |
| --- | --- | --- |
| Interface | Desktop layout | Dedicated touch layout and expanded player |
| System media integration | MPRIS / Windows SMTC | Media session and playback service |
| Tray controls | When a system tray is available | Not applicable |
| Crossfade | Available with preload | Disabled |
| Discord Rich Presence | Local Discord desktop client | Unavailable |
| Lyrics | Track page, timed LRC and custom text | Lyrics sheet when lyrics are available |
| Track visualization | Desktop spectrum; wave animation | Wave animation |
| Plugins | JSON installer and QML runtime | Same formats, system file picker |

Lyrics and metadata depend on their providers. Audio-reactive visuals depend on Qt version and multimedia backend; software rendering uses a static wave fallback.

## Detailed guides

[Recommendations](https://github.com/sykia/sange/blob/main/docs/recommendations.md) · [Library tools](https://github.com/sykia/sange/blob/main/docs/library-tools.md) · [Track search](https://github.com/sykia/sange/blob/main/docs/track-search.md) · [Queue actions](https://github.com/sykia/sange/blob/main/docs/queue-actions.md) · [Playlists and profiles](https://github.com/sykia/sange/blob/main/docs/social-and-playlists.md) · [Comments](https://github.com/sykia/sange/blob/main/docs/comments.md) · [Track page and lyrics](https://github.com/sykia/sange/blob/main/docs/track-page.md) · [Streaming preferences](https://github.com/sykia/sange/blob/main/docs/system-media-and-streaming.md) · [Discord](https://github.com/sykia/sange/blob/main/docs/discord-presence.md)

---
[Plugin System →](Plugin-System) · [FAQ](FAQ)
