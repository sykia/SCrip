# Continuous Wave

The home screen offers a continuous discovery wave on desktop and Android.
Three inline segments — **More familiar**, **Balance**, and **More discoveries** —
control the account-scoped recommendation preference directly. There is no tuning
button or separate selection popup. All three choices use the same discovery
engine and the bounded mixing rules in [Recommendations](recommendations.md#local-preferences-and-exclusions).
Selecting a segment immediately rebuilds the unplayed automatic recommendations
and starts a different eligible track. Manual additions retain their order after
that selected track, and playback history is preserved. The most recent bounded
related-track pool is cached with the session so switching can reuse candidates
without another recommendation request. If local candidates are exhausted, the
current audio continues until a normal bounded refill supplies a replacement.
Pausing or explicitly selecting a queued track cancels that pending switch.

The backend retains the legacy library-wave implementation and saved sessions
for compatibility. Its former library/discovery tabs are no longer shown.
The wave play control starts or resumes discovery.

Both discovery and legacy library sessions use the regular player, media controls and next-track preloading. The queue retains a short history; duplicate recommendations and explicitly blocked or unstreamable tracks are filtered. Discovery retains the identifiers of every queued track for the lifetime of the saved wave, including across restarts. Queue history can be trimmed without clearing this duplicate protection. Starting a fresh wave after signing out clears that history. Playback is continuous while the service returns playable tracks; this does not guarantee sample-accurate gapless transitions. Discovery follows single-source related-track pagination and keeps a rotating frontier of up to 100 recommendation seeds. When a refill exhausts its six-related-request budget without new tracks, it expands the seeds from further liked-track pages when available and automatically retries in the background. Network failures also retry automatically, with up to three automatic retries at 3, 6 and 12 seconds (or later when required by quota headers), followed by an explicit retry action. Successful additions reset the backoff. Playback prefetches when fewer than six queued tracks remain including the current track. If the queue runs dry during an outage or catalogue exhaustion, the player waits and starts the next unique track once it arrives; unavailable service data cannot guarantee uninterrupted audio. Pausing cancels pending retry timers and prevents in-flight refills from restarting playback; stopping or switching modes invalidates old replies. The library mode retains its manual retry action.

Each mode saves its own queue, position, pagination cursor, recommendation seeds and recent identifiers in the platform QSettings location, outside the repository and scoped to the signed-in account. Position updates every second during playback, and snapshots are saved on queue changes, pause, mode switches and normal shutdown. Restart restores the last session paused; the wave play control selects the saved discovery session. Signing out clears both modes.

`WavePanel.qml` is shared by both interfaces. Its GLSL fragment shader is baked into Qt shader packages with ShaderTools for the Qt RHI graphics backends (OpenGL, Vulkan, Direct3D). The wave contours, particles and beat pulses follow the current theme accent, including custom and plugin accents, and update immediately when the theme changes. The software-rendered rings use the same accent. The panel and playback controls retain the app panel and foreground colors for readability in both light and dark themes. The three recommendation preferences appear directly above the visual; their labels wrap on narrow screens and retain full hit areas. Tapping/clicking the shader toggles playback; the central play/pause, loading and retry symbols also have accessible names. Choosing a preference starts a different recommendation; it does not clear playback history or manual additions. Errors are exposed through the retry symbol, desktop tooltip and accessibility description.

Seven interwoven contours react to level, bass and beat envelopes; kick transients emit an expanding pressure ring. The shader renders directly at the native device pixel ratio, without a reduced-resolution intermediate texture. Its draw area is bounded to 1.7 times the panel height, independent of fullscreen window width. Derivative-based coverage keeps the fine contours smooth.

Desktop animation continues while the window is visible, including when another app has focus on a second monitor. Minimizing, hiding, or closing the window unloads the shader and stops wave audio analysis; Android also stops the visual when backgrounded. Qt Quick does not retain the hidden scene graph or graphics resources. Playback and its required memory remain available. Disabling motion stops animation and analysis. Qt 6.8+ decoded audio supplies level, bass and onset envelopes; older Qt versions retain the visual animation without audio response. The software renderer uses a static ring.

Platform verification: build and automated desktop/mobile-layout tests on Linux. Android devices and Windows GPUs require native runtime checks; cross-platform shader packaging is not a substitute for those checks.

See [Recommendations](recommendations.md) for multi-source selection, deterministic artist diversity, request budgets, provenance and home-shelf partitioning.

Discovery tuning and account-scoped exclusions are described in [Recommendations](recommendations.md#local-preferences-and-exclusions).
