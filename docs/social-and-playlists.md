# Profiles, collections and playlist editing

## Navigation

| Feature | Desktop | Android |
| --- | --- | --- |
| Followers and following | Click the counts in any profile | Tap the counts in any profile |
| Related artists | Profile → Similar artists | Profile → Similar artists |
| Artist websites and social accounts | Link chips beside the profile description | Wrapping link chips in the profile |
| Track reposts | Track actions → Repost / Undo repost | Track actions → Repost / Undo repost |
| Playlist reposts | Playlist → More | Playlist → More |
| Saved playlists and stations | Playlists → Liked playlists / Stations | Library → Favorites → Playlists / Stations |
| Own reposts | Own profile → Reposts → Tracks / Playlists | Own profile → Reposts → Tracks / Playlists |
| Edit a playlist | Own playlist → More → Edit playlist | Own playlist → More → Edit playlist |

The editor uses a local draft. Title, description, privacy, track order and track removals are committed together with **Save**. Drag the six-dot handle with a mouse or one finger to reorder tracks. Adjacent rows move aside and the list scrolls when the dragged track approaches an edge. The pointer handler belongs to the viewport, so delegate recycling cannot cancel a long drag. Reordering uses model move notifications rather than replacing the list. Keyboard users can focus a handle and use Alt+Up / Alt+Down. Row menus contain track removal. Playlist deletion is in the editor's menu and requires confirmation. Closing a changed draft offers to discard it.

The client loads the complete playlist and checks ownership before exposing editing. Before a write, it reads the latest metadata and complete track sequence again. A changed or incomplete remote playlist blocks the operation and retains the draft. Repeated track occurrences are preserved. Adding a track uses the same paginated ownership and completeness checks and validates a second snapshot before updating. Add, edit and delete writes are serialized within the application; logout and account changes invalidate pending operations. The API does not provide an atomic compare-and-swap operation; a simultaneous remote write between the last validation and the update remains possible.

## SoundCloud API

The integration uses the official [SoundCloud API specification](https://developers.soundcloud.com/docs/api/explorer/open-api):

- `/users/{user_urn}/followers`, `/followings`, `/related`, `/web-profiles` for profile content.
- `/me/likes/playlists` and `/me/likes/system-playlists` for saved collections; `POST` / `DELETE /likes/playlists/{urn}` accepts both kinds.
- `/me/reposts/tracks`, `/me/reposts/playlists`, and `POST` / `DELETE /reposts/{tracks|playlists}/{urn}` for reposts.
- `GET` / `PUT` / `DELETE /playlists/{urn}` and the paginated tracks endpoint for editing.
- `/system-playlists/{urn}` for visible station tracks.
- `/resolve?url=...` for profiles, tracks, playlists, stations and short links.

Collection likes and reposts load their account state lazily. Until synchronization finishes, the action cannot be toggled. A failed initial load exposes retry; a failed write retains the previous value. Pending operations are keyed by action and URN. Responses from older profile requests are ignored. Account-scoped state is cleared after logout and successful account switching.

Profile links accept HTTP(S) URLs only. OAuth credentials are attached only to the fixed HTTPS API host; resolver redirects to other hosts are rejected. Auth, purchase and settings links remain browser flows rather than music resources.

## Link opening

Paste a SoundCloud URL into search to open its resource. An installed desktop package also registers `scrip://open?url=<percent-encoded-SoundCloud-URL>`. Desktop command-line arguments accept ordinary SoundCloud URLs. A per-user local socket forwards new link launches to the running window instead of starting another player.

Linux desktop packages advertise the `scrip` and `soundcloud` schemes. A source checkout does not register system handlers: install its desktop file/package first. Windows setup registers the `scrip` scheme for the current user. Neither package takes over the global HTTP(S) handler.

Android handles VIEW intents for SoundCloud's main, `www`, `m` and `on` domains, custom protocol links, and text shared to SCrip. A native bridge delivers both cold-start and subsequent intents. On Android 12+, enable the domains under **Settings → Apps → SCrip → Open by default → Supported web addresses**. Labels vary by device. SCrip cannot verify a domain owned by SoundCloud; this requires the user's domain selection. See [Android's domain approval rules](https://developer.android.com/training/app-links/verify-applinks).

### Optional desktop browser bridge

`packaging/browser-link-bridge/` contains a dependency-free extension for normal SoundCloud link clicks in web pages. It converts an explicit click to the `scrip` protocol. Install the desktop package before enabling it.

For Chromium browsers, open the extensions page, enable developer mode and use **Load unpacked** to select this directory. For a temporary Firefox development install, use **about:debugging → This Firefox → Load Temporary Add-on** and select `manifest.json`. Permanent Firefox distribution requires signing.

The bridge runs on HTTP(S) pages to recognize clicked links. It does not fetch, transmit or store browsing data. Modified clicks, downloads, account/purchase flows and browser-internal pages retain their normal behavior. Typing a URL into the browser address bar is not intercepted; the extension button provides **Open in SCrip** for the current SoundCloud page. Browser external-application prompts still apply. This bridge is optional and is not installed automatically.

## Verification

Transport tests cover paginated lists, stale responses, safe profile links, playlist and station likes, reposts, serialized mutations, draft ordering with duplicates, ownership, incomplete track lists, remote conflicts, deletion, resolver redirects and station tracks. Isolated QML fixtures instantiate the shared surfaces at desktop and phone widths and capture editor/people layouts under `build/`. These fixtures do not launch the SCrip application or contact live accounts.

## Engagement counters and people

Existing like/repost buttons share `EngagementAction`: a heart/repeat icon and a compact count (K/M/B for large values). The full count, action and own state remain in the tooltip and accessible name. Zero is explicit; missing/null/negative counts use an em dash. Track `reveal_stats: false` hides counts even when a numeric field is present. No totals are inferred by walking user lists.

A short click, tap or Space toggles the existing action. Holding opens people without toggling; dragging lets the enclosing Flickable take over. Right click or Menu / Shift+F10 opens the same list command. Pending writes are serialized per action and URN; failed writes retain confirmed state. Successful writes update a session-wide counter/state overlay so stale item copies and late detail responses cannot apply the delta twice or undo it. Unknown and hidden counts remain unknown/hidden. Overlays reset on account changes; external users' concurrent changes are not live streamed.

The official API exposes `GET /tracks/{urn}/favoriters`, `/tracks/{urn}/reposters` and `/playlists/{urn}/reposters`. It does **not** document playlist/station liker lists or station reposter lists. These types have no unsupported list command; station reposts are hidden. The playlist schema does not guarantee a repost count, so it remains unknown when absent.

`PeoplePopup` reuses profile navigation, loading/empty/error states and explicit next-page/retry controls. Requests begin on opening, fetch one page, deduplicate users and follow only returned `next_href` values. Repost endpoints document `limit` but do not promise linked partitioning; no cursor or total is fabricated. Closing, selecting another resource or changing accounts invalidates pending responses. Reading people does not modify engagement or playback. Requests use the existing quota scheduler and authenticated API-host validation.

Tests: `tst_engagement.qml` covers short/held clicks, keyboard context menus, mouse/touch scroll cancellation, counters, supported types and 320/412/desktop widths. SoundCloud transport fixtures cover confirmed/failed/duplicate writes, synchronization, hidden/missing counts, pagination retry and stale/closed/account responses.

## Engagement count freshness

Confirmed likes and reposts keep a shared count override by URN, including repeated
tracks in lists and the queue. Each engagement write changes a request revision
both when it starts and when it finishes (including failure). A GET can refresh
confirmed counts only if it began outside a write and its revision is still
current. Account changes invalidate that revision too. This deliberately treats
concurrent engagement writes conservatively; the next ordinary eligible GET
refreshes counts without adding polling.

Count ingestion happens once before coalesced GET callbacks. Requests across a
write boundary do not coalesce. Cached objects are rendered through the current
overrides and never treated as new server observations. Valid visible numbers can
increase or decrease; missing, null, negative and hidden numbers do not replace a
confirmed count. Server counts never replace the confirmed own like/repost state.

Station navigation uses only `station_urn` returned on the API track/user object.
It never constructs station identifiers from a track/user ID. Missing stations
and failed station loads can open a clearly named SCrip selection using the
seed track's documented related endpoint. Opening that selection does not start
playback; its displayed rows can be played or added as usual. This fallback is
not represented as a SoundCloud artist station.

Contract checked on 2026-11-17 against the official
[OpenAPI JSON](https://developers.soundcloud.com/docs/api/explorer/api.json) and
[API guide](https://developers.soundcloud.com/docs/api/guide).
