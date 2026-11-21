# SoundCloud connection assistance

Sange offers an opt-in Cloudflare DNS-over-HTTPS route from connection errors
and Settings on desktop and Android. It is off by default. The consent dialog
names Cloudflare, discloses IP/domain visibility and limits the claim to DNS
blocking. Details expand in place; the dialog scrolls on short screens. Both
consent and revocation are explicit. The selection is stored in the existing
user settings outside the repository.

Do not label an HTTP 403 or a connection timeout as proof of Russian location
or censorship. No country/IP lookup is performed. Transport failures and HTTP
403 responses suggest connection assistance; successful API responses clear
the hint. Authentication errors and rate limits retain their existing handling.

## Transport

`DnsRoute` runs a loopback HTTP CONNECT listener and its resolver on a dedicated
Qt thread. The application proxy factory selects it only for HTTPS on port 443
for `soundcloud.com`, its subdomains, `sndcdn.com`, its subdomains and the exact
Sange OAuth worker host. Loopback media-cache requests remain direct. Other
hosts retain the system proxy selection. Artwork keeps its original hostname
when a proxy is selected, rather than applying the legacy direct CDN rewrite.

The listener resolves A records at `https://1.1.1.1/dns-query` using Cloudflare's
JSON API. Its own resolver and upstream sockets explicitly use NoProxy to avoid
recursion. It forwards encrypted bytes to the resolved IP without terminating
TLS: Qt still checks SoundCloud's certificate against the original hostname.
No certificates are installed, TLS checks disabled or account headers parsed.
No public third-party traffic proxy is used. DNS replies, CONNECT headers,
connection counts and transfer buffers are bounded. Non-allowlisted hosts,
non-443 ports and private/reserved IPv4 destinations are rejected. Disabling the
mode closes its open tunnels. IPv6-only access, address racing and alternative
resolvers are not implemented in this version.

This changes DNS resolution, not the public IP, TLS SNI or OS routing. It cannot
guarantee service under IP/TLS blocking or service-side restrictions. It does
not exempt Sange from a TUN VPN. Browser OAuth uses the browser's own network
configuration. The user may need an existing working VPN or a change to their
VPN's app-routing settings when DNS alone is insufficient.

The existing artwork preparation resolver predates this feature and remains
independent of the new opt-in route.

## Validation

- Build: `cmake --build --preset debug`.
- Offline routing/security tests: `ctest --test-dir build/debug -R '^dnsroute$'
  --output-on-failure`.
- QML consent/revocation and narrow-screen tests are included in `artwork_ui`.
- Explicit external TLS check: `SANGE_DNS_LIVE=1 build/debug/dnsroute_tests
  liveTlsTunnel`. A signed-out HTTP response proves TLS connectivity, not music
  playback or an authenticated API call. The ordinary suite skips this check.

For the November 20 investigation, generated network-isolation helpers and
screenshots live under `build/dns-review/`. The Linux helper binds only the
validation process's non-loopback IPv4 TCP connections to `enp12s0` using
SO_BINDTODEVICE. It does not modify Happ, routes, user settings or the installed
application. Direct probes returned an HTTP 403 from the unsigned SoundCloud
API root, while Cloudflare DoH was reachable. These observations do not establish
that authenticated playback works throughout Russia.

Cloudflare documents its [JSON DoH API](https://developers.cloudflare.com/1.1.1.1/encryption/dns-over-https/make-api-requests/dns-json/).
SoundCloud's [API terms](https://developers.soundcloud.com/docs/api/terms-of-use.html)
do not provide a blanket public approval for bypass tools; do not describe this
feature as approved by SoundCloud without a specific applicable agreement.

Final validation on the November 20 working tree:

- Debug build passed (`build/dns-build-complete.log`).
- `dnsroute` and the full `artwork_ui` suite passed; the separate opt-in live
  check passed through the DNS tunnel with physical-interface binding and
  returned HTTP 403 with verified TLS.
- All five connection-hint classification cases passed, including recovery.
- The broader `qmlScreensRender` test failed on an existing mini-player context
  dependency: `windowEffects` is not provided by that test harness. The new
  DNS component's consent/revocation, expanded details and mobile layout tests
  passed. Do not report the entire UI suite as clean.

## Regional API and media relay

Direct testing subsequently identified CloudFront's explicit country-block page,
not merely a generic 403. DNS cannot fix this response. The client recognizes
that specific page and offers a separate, consent-based Sange server route.
The existing authentication Worker now provides `/api/` and `/media` routes.
API traffic, the access token, IP address and audio requests pass through the
project server; the dialog discloses this change of trust. The server streams
bytes without shared caching or application-level logging of credentials.

API requests remain fixed to `api.soundcloud.com`, with bounded JSON writes,
manual redirects and per-IP limits. Canonical pagination/stream API URLs remain
canonical in the client, then use the selected transport. Native clients send
an explicit Sange User-Agent. API JSON stream resolvers and direct CDN redirects
produce signed, 30-minute media capabilities. Only HTTPS `sndcdn.com` hosts are
accepted; HLS child, init and key URIs are re-signed. Tampered/expired capabilities,
arbitrary hosts, unsupported ranges and external redirects are rejected.
Media never forwards OAuth or browser cookies. Playlist buffers are bounded;
audio is streamed with range headers intact. API, media and OAuth rate limits
are independent. Free hosting has finite request quotas; this is not a promise
of unlimited capacity or access to uploader-restricted content.

Live verification through `enp12s0`, with proxy environment disabled, returned:
direct authenticated API 403 (explicit country block), relay profile/library/
stream resolution 200, HLS playlist 200 and media segment 200. FFmpeg decoded two
seconds of the relayed MP3 audio successfully. The probe holds the existing
session token and audio only in process memory; no token, signed URL, profile,
track identity or audio file is written to the repository.

The executable is now `sange` (`sange.exe` on Windows), while Qt storage and
Android package identities remain unchanged. The user's Happ configuration has
an explicit direct-routing entry for the new debug executable path. Private
Happ backups are outside the repository. Renaming/replacing an executable means
process-based VPN rules must refer to its current path, not a stale `(deleted)`
process target.

Native playback verification completed after adapting to SoundCloud's current
`media-streaming.soundcloud.cloud` AAC origin and typed stream resolver paths.
Both AAC 160 and MP3 128 passed decoding plus a seek to 30 seconds using the
production `StreamCache` and Qt Multimedia/FFmpeg, with IPv4/IPv6 external TCP
bound to the physical interface. Media capability paths retain safe filename
extensions; opaque HLS segment paths receive an appropriate extension hint.
Old `/media?...` capabilities remain accepted until expiry. The deployed Worker
version for this verification is `d457296d-ddb8-42ae-9960-010ea7c3e493`.

The current client build passed and the renamed `sange` process was launched.
Its ordinary unbound requests received the explicit regional 403 with the new
Happ rule active, confirming that the app itself was routed directly. The
server route remains opt-in: use **Use server → Connect** in the application.
The earlier `windowEffects` fixture failure was fixed by supplying the native
service interface in offscreen tests; the complete screen-rendering check now
passes.

Final client checks also passed with the generated HLS seek fixture enabled:
`dnsroute`, `streamcache` and `artwork_ui` (90 seconds total), plus the targeted
API relay/error-classification and complete screen-rendering tests. Worker
coverage has 15 passing cases. NSIS compiled the renamed-executable fixture;
its four Python checks passed. Native Windows installer execution and Android
builds were not run for this Linux investigation.

## Country-block notice

An explicit CloudFront country-block response now produces a shared compact
notice on desktop and Android, including track details and the Android player.
The notice offers one connection action; that action opens consent and never
enables the relay automatically. Ordinary 403, 401 and 429 responses do not
claim a country restriction. OAuth and stream API responses participate in the
same detection. A country-specific error is not duplicated as a red error box.
A successful API response clears the country hint; clearing an error message
alone does not erase the actionable notice.

Notice surfaces use the host theme's panel/border colors. Only the connection
button uses the selected accent; there is no fixed purple color. The same
component is tested in light/dark desktop and narrow Android layouts, including
live theme changes, cancellation and consent.

## Artwork transport with the optional relay

When `network/apiRelay` is enabled, the QML artwork network factory and native
media artwork clients send public `i1`–`i4.sndcdn.com` image requests through
`/artwork/<cdn-host>/<image-path>` on the existing Worker. This also covers
artwork URLs restored from a saved catalogue; API response rewriting is not
required. Image size transformations still happen before transport routing.
The route does not forward account authorization, cookies, or URL query strings.
The Worker accepts only GET/HEAD and JPEG, PNG or WebP paths on those four fixed
CDN hosts, refuses redirects and non-image responses, and uses the existing
media rate limit. Successful public images may be cached for one day. Disabling
the relay restores the existing direct artwork transport for new requests.

Verified with 16 Worker tests, Linux debug and universal Android builds, and a
real JPEG fetched through the deployed Worker with the client socket bound to
the physical interface (no VPN). Worker version:
`da220c54-738a-419b-a9f2-995415bf1bbb`. The phone-test APK uses the existing
release signing key and package identity.
