# Code audit — 2026-11-19

The review covers the C++ backend and platform integrations, QML views and
controls, OAuth worker, browser bridge, build configuration, packaging helpers,
and existing regression suites. Changes already present in the working tree
were retained. Historical release archives were not rewritten.

## Corrected behavior

- Lyrics searches now retain the selected track's original duration and storage
  identity. Removing custom lyrics repeats the original lookup, including its
  duration, and subsequent edits use the same persistent key.
- Unparseable synchronized lyrics fall back to readable plain text. The raw text
  exposed to the interface matches the displayed version.
- LRC timestamp offsets saturate before addition, avoiding signed overflow on
  extreme provider or custom input.
- Direct `soundcloud://` resource links now use the same operation-generation and
  busy/error handling as web links. Late responses cannot navigate away from the
  most recently requested resource, and response identity is checked.
- A response that finishes after its stream-cache resource was evicted cannot
  cache the removed object or recreate orphan HLS child mappings. Buffered
  requests for removed resources receive HTTP 404 without further retries.
- Switching discovery preferences after wave history pruning uses the adjusted
  index of newly appended recommendations, preserving explicit queue additions.
- Automatic recommendation selection excludes preview-only tracks, consistent
  with the full-track playback policy.
- Desktop QML integration fixtures provide the real language controller when
  opening settings, matching application startup instead of emitting a missing
  context-property error.

Regression tests cover the corrected paths. Five new regression cases were run
against the old implementation and failed before their corresponding fixes.
The history-pruning regression additionally covers a long wave queue.

## Validation scope

Desktop build and automated UI/backend checks run on Linux. Windows and Android
platform code is reviewed, but native builds, device playback, system media
controls, live SoundCloud authentication, and external lyrics providers require
those platforms or live integration environments. Mock transports keep normal
tests independent of user credentials and live service availability.

Build output, analyzer output, test logs, and screenshots belong under `build/`.

Static analysis used `clang-tidy`'s `clang-analyzer-*` checks for all 18 desktop
C++ translation units. A separate compilation database under `build/audit/`
omits GCC's `-mno-direct-extern-access` option, which Clang does not accept;
application compiler flags were not changed. The analyzer reports one potential
leak at the `std::function` capture in `SoundCloud::deferRequest`. Ownership was
reviewed: the capture is a value owned by the timer callback or injected test
scheduler, has normal RAII destruction, and creates no ownership cycle. This
warning is retained in the analyzer log rather than suppressed.

## Completed checks

- `cmake --build --preset debug`: successful after the final source changes.
- `ctest --test-dir build/debug --output-on-failure -j2`: all 8 suites passed,
  including 263 successful cases in the SoundCloud/UI executable.
- `bash tests/run-hls-seek.sh --generate-only`, followed by the HLS and stream
  cache suites with `SCRIP_HLS_TEST_PLAYLIST` pointing to the generated fixture:
  both suites passed, including decoded audio after seeks and cache-only seeks
  after the upstream server is closed.
- `bash tests/run-secretstore-linux.sh`: unavailable-service behavior and isolated
  Secret Service round-trip passed.
- Worker tests: 6 passed. Python regression tests: 28 passed.
- Repository license check, tracked-file secret scan, and `git diff --check`:
  successful.

The local Ninja state emitted `premature end of file; recovering`; Ninja
recovered and completed the builds. No application compiler errors or warnings
were reported. Generated build state is not part of the source changes.
