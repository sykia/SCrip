# SoundCloud request quotas

The transport follows the official [SoundCloud rate-limit documentation](https://developers.soundcloud.com/docs/api/rate-limits).
Quota values are never hard-coded. General API, streams, search, likes, repost mutations, downloads and OAuth requests have separate cooldowns. A known general API cooldown also delays streams and every other API group, including stream resolution and preloading. Search cooldowns do not delay playlist creation; likes cooldowns do not delay profiles or playback. Response header names are case-insensitive: `Retry-After` accepts seconds or an HTTP date; exhausted `X-Ratelimit-Remaining` uses the Unix-seconds `X-Ratelimit-Reset`. If both supply deadlines, the later one wins. The advertised limit is not used to infer another window or a local quota. A 429 without a usable deadline uses a short fallback backoff.

Timers keep the UI responsive. Session epochs invalidate deferred requests and callbacks on logout or account replacement; destruction also invalidates timers. App-wide cooldowns survive logout within the process because SoundCloud quotas apply to the client application.

Foreground GETs may retry twice after 429, 502/503/504 or selected temporary network failures. Other errors fail immediately. Writes never retry these ambiguous failures. The existing single token refresh and one 401 replay remain separate from this retry budget; token POSTs are never automatically replayed after failure.

Overlapping identical GETs share their result within the same session and request policy. Parameters remain part of the key. Likes synchronization, home shelves and repost decoration share the first likes page and accumulated like IDs. Only the first page is cached in memory; failed or cyclic pagination never marks the library complete. Explicit home refresh and successful like changes invalidate the first-page cache. No library is persisted.

Current-track stream requests use high network priority. Preloading waits until the current stream source is resolved and skips exhausted budgets. Background recommendations do not use transport retries; discovery has at most three delayed refill attempts and observes the API cooldown. Manual retry resets this budget.

The server does not identify the exhausted quota group in current headers or JSON error bodies. Cooldowns are attributed to the documented endpoint group and HTTP method; a response on a route with a stricter quota cannot reliably distinguish that quota from the general budget. The client does not guess from quota sizes or reset durations. OAuth is isolated. Already dispatched requests cannot be reordered by network priority. This is process-local coordination, not coordination with other clients using the same client ID.

Tests use a fake network transport, clock and deferred-operation queue. Long cooldowns are advanced virtually, including session and object-lifetime cancellation.
