# Collection search, sorting and saved searches

Desktop and Android use the same `CollectionTools.qml` controls in collections
and the playlist preview. Typing filters the loaded rows without requests. The
status explicitly identifies an incomplete view, including empty local results.
The collection actions menu starts a whole-collection search using the collection's
existing authenticated pagination. It does not search the SoundCloud catalogue.
Loaded rows become partial results immediately; progress reports the loaded count.
The same menu stops further pages. An already issued page can finish, but stopping,
changing context/query, signing out or changing accounts prevents continuation.

Requests use the existing coordinator, token handling, GET coalescing and rate
limits. Pages are sequential, separated by 350 ms, and constrained to the same
HTTPS API collection. Repeated cursors and malformed pagination stop with an
incomplete status instead of reporting a completed empty search. Network failures
retain loaded rows. A run pauses after 100 pages to bound quota consumption; the
user can explicitly continue from the remaining cursor. There is no permanent
library mirror. Normal lazy loading remains available separately, and is suspended while a local
query is present so typing cannot exhaustively load the library.

Sorting offers source order, title, credited artist, duration and upload date
when a parseable date is actually present. Reverse order is optional; missing
values stay last and equal values retain source occurrence order. Upload date is
never presented as the date of liking a track. An incomplete collection is sorted
only over loaded rows. Filtering and sorting change presentation only. Playback
from a row explicitly snapshots the displayed order, preserving duplicate
occurrences. Selection resets when the sorting/filter context changes or when new pages can
shift filtered/sorted row positions, including identical duplicate tracks.

Saved searches store a name, query, supported catalogue filters and sort settings
in platform QSettings outside the repository. Catalogue kinds and collection
contexts have separate scopes. Apply, rename and delete are available through the
collection actions menu. Renaming preserves the original query and filters. A saved search
never triggers an exhaustive collection download on application or on typing.
At most 100 configurations are retained; track metadata and credentials are not
stored by this feature.

Validation: mocked backend regressions cover pagination progress, cancellation,
loops, account reset and stale responses. Qt Quick tests render real controls at
280, 372 and 960 logical pixels in both themes and exercise typing, stable duplicate
sorting and saved-search scopes/renaming/deletion. The existing Android fixture
loads the actual mobile Main.qml. These are Linux-hosted fixtures, not Windows
or Android device validation.

Controls follow the existing toolbar and sheet styling: desktop search is inline
with collection actions, mobile actions use a compact menu button, and saved-search
management uses the same themed fields and surfaces. On short mobile windows the
playlist header scrolls independently so track rows remain reachable.
