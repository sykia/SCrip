# Integration plan and results

## Initial state

The project contained a QML shell with empty sections and transparency settings. Networking, authentication and playback were absent. The user supplied issued keys in a screenshot; they were stored outside the checkout. A case-recognition error was corrected. After configuring the Redirect URI, the user signed in successfully.

## Completed stages

1. Added a SoundCloud QObject for OAuth, API calls, collection models and audio control.
2. Connected Qt Network and Qt Multimedia without third-party networking dependencies.
3. Connected search, collection, profile, playlists, queue and player.
4. Replaced the QML design with a light Apple-style interface based on a new reference: narrow sidebar, artwork grid, list, floating player and compact dialogs. Old shaders and native blur are excluded from the build.
5. Before sign-in, show only the welcome screen, sign-in button and connection settings.
6. Added QtTest checks with a substituted transport and interface snapshots in `build/`.

## Validation

Live API: successful Client Credentials exchange, search, `/me` profile, likes, playlists and an HLS stream response with HTTP 200. The user completed OAuth and a valid user session was saved. Actual playback through Qt Multimedia was checked: position advances without player errors.

Automated tests cover request headers and encoding, pagination, rejection of third-party next_href, search races, single token refresh, 401/429 errors, guest-session restrictions, unavailable tracks, local callback, PKCE, incorrect state, code exchange, logout with pending requests and QML screen creation.

Test data exists only in `tests/`; the production UI receives API data. Checks do not alter user likes or playlists. Run `ctest --test-dir build/debug --output-on-failure`.

## Version boundaries

- Full offline copying of streams is not provided.
- Audio uploads, editing other users' data, messages and comments were not added: this is a listening and personal-collection client.
- Unavailable and region-restricted tracks depend on SoundCloud's response.
- Linux builds are checked locally; Windows requires a separate build and multimedia-plugin validation on that system.
- The user deferred a public server and its testing.

## Design evolution: three columns and motion

The light Apple-style design was combined with flat surfaces and a three-column layout. Sidebar navigation is 196 px; content and right-panel padding is 24 px; the artwork grid uses consistent 24 px gaps. Below 1100 px, the right column becomes a popup panel accessible from the player.

Profiles have a dedicated page with avatar, name, location, real counters, description and collection tabs. The reposts API section is available. The main SoundCloud website did not respond during validation; structure was checked against the official Help Center: https://help.soundcloud.com/hc/en-us/articles/115003570748-Anatomy-of-SoundCloud .

Several screens on https://antigravity.google/ were viewed while scrolling. Soft entrances, eased motion and spacious composition were adapted: 240 ms section transitions, 320 ms artwork entrances and short button feedback. This is an independent QML implementation, rather than copied website code. Mouse scrolling is smoothed; touchpad scrolling remains native. Settings include a motion toggle.

Checks cover wide and narrow screens, profiles, scrolling with real QWheelEvent input, and the connected account's live collection, artwork and playback.
