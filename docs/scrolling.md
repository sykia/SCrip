# Scrolling and seeking

Desktop library, track lists and cover grids smooth discrete mouse-wheel steps.
Touchpad pixel deltas and touch gestures retain Qt's native handling. Modified
wheel events are passed through. Animation stops when direct manipulation starts,
the viewport changes, or content is replaced. Reduced motion applies wheel steps
immediately. Scroll limits include the Flickable origin and margins.

The Android track list reuses delegates and keeps a bounded offscreen buffer.
Pagination checks are coalesced at 100 ms intervals during scrolling and retried
when pagination becomes available or viewport/content dimensions change. Hidden
pages and failed requests do not automatically request more data.

Both players preview the selected time while dragging the seek handle; playback
seeks on release, avoiding repeated decoder and network work during a drag.

Validation: desktop debug build, Android ARM64 `scrip` target build, CTest suite,
and QML wheel tests (event dispatch, margins, reversal, and content resize).
Physical-device frame pacing and Windows runtime behavior require device testing.
