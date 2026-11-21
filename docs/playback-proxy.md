# Media proxy threading

The local HTTP media proxy runs on `SangeStreamCache`, a dedicated Qt thread.
`StreamCache` is the GUI-facing facade; `StreamCacheWorker` owns all sockets,
network replies, timers, resource mappings, cache entries and temporary files.
Commands execute in the worker using blocking queued invocations. The worker
must never invoke or wait for the GUI thread. Only URLs cross the boundary.

This separation prevents a playback recovery deadlock observed on Linux:
Qt deferred destruction of an FFmpeg demuxer waits for its thread on the GUI
thread, while that demuxer is waiting for an HTTP response from our proxy.
Running the proxy on the GUI thread prevents both operations from finishing.
Interrupting requests before explicit player stop/source changes alone does
not cover deferred Qt destruction or subsequent decoder requests.

Keep `SoundCloud::m_streamCache` declared before both QMediaPlayer members:
reverse destruction order keeps the proxy alive during decoder teardown.
The facade stops and joins its worker on destruction; Qt's finished signal
then deletes the worker context and its child in their owning thread.

`StreamCacheTest::proxyRespondsWhileGuiThreadWaits` verifies uncached and cached
responses while the main event loop is blocked. Worker tests separately cover
ranges, interrupted requests, download retries, eviction and decoded HLS seeks.
Linux uses the public `Sange` desktop/application ID; the `scrip` executable,
package identity and `SCrip` settings identity remain compatible with upgrades.

## Opaque HLS resources and interrupted downloads

Playlist membership, rather than a URL suffix or the CDN Content-Type, determines
which responses must be buffered atomically. An extensionless child served as
`application/octet-stream` previously escaped that buffering: the proxy forwarded
HTTP 200 and partial bytes, then closed the response on an upstream interruption.
It could no longer retry without exposing a truncated segment to the decoder.
`extensionlessHlsSegmentIsRetriedBeforeDelivery` reproduces this failure and now
requires a complete response after an upstream retry.

For fMP4 playlists with EXT-X-MAP, extensionless local media/init URLs receive
`.m4s`/`.mp4` suffixes while their upstream URLs remain unchanged. FFmpeg 9 rejects
extensionless segment URLs through its HLS extension validation before reading
any audio. This is a separate failure from interrupted transport. The decoded
`hlsSeekThroughCache:opaque-interrupted-segment` case combines opaque upstream
URLs, generic MIME types and an interrupted destination segment. It checks PCM
and position advance after uncached and cached seeks.

Slow transfers (at least two seconds) and failed transfers log `SangeMedia` with
a random local object prefix, HTTP/network status, byte count, length validity,
atomic/forwarded state, retry number and elapsed time. No upstream URLs, headers,
OAuth tokens or track identifiers are included. `SangePlayback` adds numeric
backend status and position/duration at errors or watchdog stalls. These records
can distinguish future CDN/proxy failures without leaking signed addresses.

The November 20 user report contains FFmpeg I/O errors followed by failed
restored seeks, but does not contain upstream segment response details. The
regressions above prove client defects; they do not establish that every stall
in that historical report used an opaque URL or the same interrupted response.

Reproduction controls on CachyOS / Qt 6.12.0 / FFmpeg n9.0.2:

- The pre-change proxy fails the extensionless HTTP retry regression with
  `RemoteHostClosedError`.
- The pre-change proxy fails decoded playback before first PCM because FFmpeg
  rejects the extensionless local segment URLs.
- Applying only the local suffix fix allows initial playback, but an interrupted
  uncached seek to 75 seconds emits AAC decoding errors and fails the
  position-plus-PCM progress assertion. This isolates premature forwarding as a
  separate defect from URL validation.

The isolated baseline harness and captured control logs are generated under
`build/playback-investigation/`; they contain only synthetic localhost media.

After both fixes, the debug build succeeds. The stream-cache suite with
`SCRIP_HLS_TEST_PLAYLIST` supplied passes all 18 checks with no skips, including
both decoded HLS cases. The six recovery scenarios also pass (8 QtTest checks
including setup/cleanup). Results are in `fixed-streamcache.log` and
`fixed-recovery.log` under the same investigation directory. The installed
application was not replaced as part of this source investigation.
