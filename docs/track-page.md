# Track page

The desktop track page puts high-resolution artwork and track actions beside a live
spectrum. The content uses a 66% track / 34% lyrics split at widths of 660 px
or more, with lyrics stacked below the track on smaller screens. The lyrics pane
provides search and synchronized playback. On medium-width panes, artwork is
centered above the title and actions; only spacious panes place them side by side.
Titles wrap long words inside their column. Neutral panels and a quiet empty lyrics
state keep the composition balanced. Lyrics search expands on request, and track
descriptions are collapsed under “About track” by default. The previous equalizer controls are removed from this page; the existing
audio routing and saved DSP settings remain supported internally.

When opened from the Now Playing artwork or title (sidebar or playback dock), the
page follows the current track across queue advances, previous/next commands and
new playback selections. Lyrics and track actions refresh with the track identity.
Pages opened from catalog cards stay attached to the selected track.

## Spectrum

AudioDsp analyzes PCM samples using a 4096-point Hann-windowed FFT and returns 96
logarithmic bands from 30 Hz to the lower of 18 kHz and the input Nyquist margin.
Stereo power is combined without cancelling opposite-phase channels. The existing
33 ms envelope drives either a continuous filled curve or thin rounded bars.
Spectrum attack/release uses gentler coefficients independent of the beat effect.
The filled wave adds 16 ms interpolation and a seven-band spatial smoothing kernel,
with a soft translucent fill and a light outline. Interpolation runs only while the
current track page is visible and honors the reduced-motion preference. The
selected style persists in UI preferences. Only the current track feeds the plot;
there is no synthetic animation in silence. Analysis stops when the page closes,
unless another audio visualization still needs it. PCM visualization requires
Qt 6.8 or newer and a backend with QAudioBufferOutput support.

## Lyrics

`Lyrics` uses six independent HTTPS sources: LRCLIB, NetEase Cloud Music,
QQ Music, KuGou, Genius and Lyrics.ovh. It first attempts an exact LRCLIB lookup,
then searches all sources concurrently if that record has no lyrics. Production
and official-upload labels are removed from titles; `Artist - Title` uploads are
split into metadata. Remix, live, translation and speed labels remain significant.

Search results are internal only. The track page never displays catalog candidates
or asks users to pick a record. A normalized exact title plus matching artist or
nearby duration is required before downloading lyrics. Large duration mismatches
are rejected even with the same artist/title. At most two matching records per
source are attempted; unrelated recommendations never trigger lyric downloads.
The first verified text is displayed; a verified timed version can replace plain
text. Existing timed lyrics are not overwritten by a slower provider. When no
matching text is available the page says “Lyrics not found”. A retry button repeats
the lookup; no query editor or result list is exposed.

LRCLIB and Lyrics.ovh have public documented APIs. NetEase, QQ Music and KuGou use
public website endpoints; Genius uses public search and extracts only marked lyric
containers from the matched song page. These website interfaces may change or be
unavailable by region. No authentication, embedded third-party credentials,
CAPTCHA bypass, account cookies or external proxy service is required. Failure of
one source does not stop the other sources. Commercial services requiring an
account contract/API key (for example Musixmatch and LyricFind) are not configured.

Lyrics use regular 15–16 px text with compact line spacing; section labels are
smaller and muted. Genius nodes marked `data-exclude-from-selection` are removed
with their nested markup before extracting text, so contributor counts, translation
menus and page titles do not appear as song lines.

Timed LRC supports multiple timestamps per line, one to three decimal places,
metadata tags and a global millisecond offset. Lines are sorted by time. Playback
position determines the active line, including after seeking. Clicking a timed line
starts playback from that timestamp, including when paused or when the page
shows a different track. The existing pending-seek mechanism retains the chosen
position while the stream is loading. The active row has an accent background
and text using the selected theme accent, with a soft GPU shadow glow. Color,
opacity and automatic centering animate according to the motion preference;
manual scrolling stops the centering animation. Hovering previews clickable rows. Automatic following can be toggled; manual scrolling turns
it off. Ordinary lyrics are displayed without inventing timing. Instrumental tracks are identified separately; provider failures are recorded
in backend state without exposing unrelated results to the user.
Lyrics are never substituted with the track description.

Requests use HTTPS, a descriptive User-Agent, an 8-second transfer timeout and
same-origin redirects. They send no SoundCloud token. Superseded requests are
cancelled and generation-guarded. Successful responses are cached in memory for up
to 64 request URLs; downloaded lyrics are not persisted to disk. Users can paste
plain text or timed LRC in the Add dialog. Custom lyrics take priority on subsequent
opens and are stored in platform QSettings under a hash of the SoundCloud track
identity, outside the repository. Removing custom lyrics restores provider lookup.
Pasting lyrics cancels outstanding requests to prevent late network replacement.
HTTP 429/503 responses impose a Retry-After cooldown with jitter. There are no
background retries or library-wide requests. Cooldowns are isolated per source
host. Credits identify and link to the source of the displayed text. The transport is platform-neutral; Android retains its separate player UI.

## Validation

`lyricsParsingAndTransport` covers LRC timestamps, title cleanup, cache reuse,
plain lyrics, instrumental records and malformed replies. `lyricsFallbackAndCustomText`
covers automatic fallback, custom text persistence and cancelled late replies.
`lyricsMultipleProvidersAndAutomaticMatching` exercises each added source with a
fake transport, HTML entities/nested lyric containers, unrelated recommendations,
version/duration mismatches, per-source throttling and cancellation. No candidates
are exposed. `qmlScreensRender` checks the empty state, absence of manual search
controls, both spectrum modes, theme accent updates and desktop/narrow layouts.
It also clicks a timed row against local WAV audio and checks seek/resume behavior.
