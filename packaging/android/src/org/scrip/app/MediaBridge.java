package org.scrip.app;

import android.app.*;
import android.content.*;
import android.graphics.*;
import android.os.*;
import android.util.Log;
import org.json.JSONObject;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Общий снимок настоящего Qt-плеера: без второго источника музыки или очереди. */
public final class MediaBridge {
    static final Handler MAIN = new Handler(Looper.getMainLooper());
    static Context context;
    static volatile boolean ready;
    private static volatile boolean shuttingDown;
    private static volatile int lifecycle;
    private static boolean pendingPlay;
    private static boolean restorePending;
    static volatile boolean preparing;
    static volatile long prepareStartedAt;
    static String title = "SCrip", artist = "Включи музыку в приложении", artwork = "";
    static boolean playing, active, buffering, hasTrack, liked;
    static long position, duration;
    static Bitmap cover;
    static volatile int artworkRevision;
    private static final ExecutorService IMAGE_WORKER = Executors.newSingleThreadExecutor();
    private static String savedKey = "";
    private static long savedPosition = -15000;
    private MediaBridge() {}
    private static native void nativeAction(int action, long position);
    private static native void nativeRestore(String track, long position);

    public static void initialize(Context ctx) {
        context = ctx.getApplicationContext(); shuttingDown = false; ++lifecycle; ready = true;
        JSONObject saved = readSavedTrack(context);
        restorePending = saved != null;
        if (saved != null) {
            try { nativeRestore(saved.getJSONObject("track").toString(), saved.optLong("position")); }
            catch (org.json.JSONException error) { clearSavedTrack(context); restorePending = false; }
        } else currentUrn = currentArtworkUrl = "";
    }
    /** Start the foreground service while the activity is still visible, before Qt starts decoding audio. */
    public static void preparePlayback() {
        if (!ready || context == null) return;
        prepareStartedAt = SystemClock.elapsedRealtime();
        MAIN.post(() -> {
            if (!ready) return;
            Log.i("SCripStartup", "android_prepare_dispatch_ms=" + (SystemClock.elapsedRealtime() - prepareStartedAt));
            preparing = true;
            // Существующая служба остаётся foreground на всём переходе.
            if (PlaybackService.instance != null) return;
            try { context.startForegroundService(new Intent(context, PlaybackService.class)); }
            catch (IllegalStateException | SecurityException error) { Log.w("SCripMedia", "Не удалось подготовить фоновое воспроизведение", error); }
        });
    }
    public static void shutdown() {
        ready = false; shuttingDown = true; ++lifecycle;
        synchronized (MediaBridge.class) { pendingPlay = false; }
        MAIN.post(() -> {
            playing = active = buffering = hasTrack = preparing = false;
            if (context != null) {
                context.stopService(new Intent(context, PlaybackService.class));
                save(true);
            }
        });
    }
    public static void update(String json) {
        MAIN.post(() -> {
            if (!ready || context == null) return;
            try {
                JSONObject data = new JSONObject(json);
                String nextTitle = data.optString("title");
                String nextArtist = nextTitle.isEmpty() ? "Включи музыку в приложении" : data.optString("artist");
                boolean nextPlaying = data.optBoolean("playing"), nextBuffering = data.optBoolean("buffering");
                boolean nextActive = data.optBoolean("active"), nextHasTrack = data.optBoolean("hasTrack");
                boolean nextLiked = data.optBoolean("liked");
                long nextPosition = Math.max(0, data.optLong("position")), nextDuration = Math.max(0, data.optLong("duration"));
                String nextArtwork = data.optString("artwork");
                boolean restoreCleared = data.optBoolean("restoreCleared");
                String nextUrn = data.optString("urn");
                boolean artworkChanged = !nextArtwork.equals(artwork);
                boolean playbackEnded = hasTrack && playing && (!nextHasTrack || !nextPlaying);
                if (playbackEnded) save(true);
                title = nextTitle.isEmpty() ? "SCrip" : nextTitle; artist = nextArtist;
                playing = nextPlaying; buffering = nextBuffering; active = nextActive; hasTrack = nextHasTrack; liked = nextLiked;
                position = nextPosition; duration = nextDuration;
                if (nextHasTrack && !nextUrn.isEmpty()) currentUrn = nextUrn;
                if (nextHasTrack) currentArtworkUrl = data.optString("artworkUrl");
                if (restoreCleared) { clearSavedTrack(context); restorePending = false; }
                if (nextHasTrack && !data.optString("urn").isEmpty()) restorePending = false;
                if (consumePendingPlay(nextHasTrack && !restorePending)) nativeAction(4, 0);
                if (artworkChanged) {
                    artwork = nextArtwork;
                    final int requestRevision = ++artworkRevision;
                    cover = null;
                    if (!artwork.isEmpty()) {
                        final String key = artwork;
                        IMAGE_WORKER.execute(() -> {
                            BitmapFactory.Options options = new BitmapFactory.Options();
                            options.inSampleSize = 2;
                            Bitmap decoded = BitmapFactory.decodeFile(key, options);
                            if (decoded == null) return;
                            Bitmap square = centerCrop(decoded, 128);
                            MAIN.post(() -> {
                                if (!isCurrentArtwork(key, requestRevision)) return;
                                cover = square;
                                ++artworkRevision;
                                if (PlaybackService.instance != null) PlaybackService.instance.publish();
                            });
                        });
                    }
                }
                save(false);
                preparing = false;
                if (active && !shuttingDown && PlaybackService.instance == null) {
                    try { context.startForegroundService(new Intent(context, PlaybackService.class)); }
                    catch (IllegalStateException | SecurityException error) { Log.w("SCripMedia", "Не удалось запустить фоновый плеер", error); }
                } else if (PlaybackService.instance != null) PlaybackService.instance.publish();
            } catch (org.json.JSONException error) { Log.w("SCripMedia", "Некорректное состояние плеера", error); }
        });
    }
    private static void save(boolean force) {
        // Диск обновляется при смене трека/паузы, а не каждую секунду.
        String key = currentUrn + '\n' + title + '\n' + artist + '\n' + artwork + '\n' + playing;
        if (!force && key.equals(savedKey) && Math.abs(position - savedPosition) < 15000) return;
        savedKey = key;
        android.content.SharedPreferences.Editor edit = context.getSharedPreferences("music_widget", Context.MODE_PRIVATE).edit()
            .putString("title", title).putString("artist", artist).putString("artwork", artwork)
            .putLong("position", position).putLong("duration", duration);
        // Сохраняем только публичные сведения о выбранном треке, никогда токены/секреты.
        if (hasTrack && !currentUrn.isEmpty()) {
            try {
                JSONObject track = new JSONObject().put("urn", currentUrn).put("title", title).put("duration", duration)
                    .put("artwork_url", currentArtworkUrl).put("user", new JSONObject().put("username", artist));
                edit.putString("restore_track", track.toString()).putLong("restore_position", position);
            } catch (org.json.JSONException ignored) {}
        }
        edit.apply();
        savedPosition = position;
    }
    private static String currentUrn = "";
    private static String currentArtworkUrl = "";
    static JSONObject readSavedTrack(Context ctx) {
        String json = ctx.getSharedPreferences("music_widget", Context.MODE_PRIVATE).getString("restore_track", "");
        if (json.isEmpty()) return null;
        try { return new JSONObject().put("track", new JSONObject(json)).put("position", ctx.getSharedPreferences("music_widget", Context.MODE_PRIVATE).getLong("restore_position", 0)); }
        catch (org.json.JSONException error) { clearSavedTrack(ctx); return null; }
    }
    static void clearSavedTrack(Context ctx) {
        ctx.getSharedPreferences("music_widget", Context.MODE_PRIVATE).edit().remove("restore_track").remove("restore_position")
            .remove("title").remove("artist").remove("artwork").remove("position").remove("duration").apply();
        currentUrn = currentArtworkUrl = ""; hasTrack = playing = active = buffering = false;
        title = "SCrip"; artist = "Включи музыку в приложении"; artwork = ""; position = duration = 0;
        savedKey = ""; savedPosition = -15000;
    }
    static void restore(Context ctx) {
        if (context == null) context = ctx.getApplicationContext();
        if (ready) return;
        android.content.SharedPreferences p = context.getSharedPreferences("music_widget", Context.MODE_PRIVATE);
        title = p.getString("title", "SCrip"); artist = p.getString("artist", "Включи музыку в приложении");
        currentUrn = "";
        JSONObject savedTrack = readSavedTrack(context);
        if (savedTrack != null) {
            JSONObject track = savedTrack.optJSONObject("track");
            if (track != null) { currentUrn = track.optString("urn"); currentArtworkUrl = track.optString("artwork_url"); }
        }
        hasTrack = !title.isEmpty() && !title.equals("SCrip");
        artwork = p.getString("artwork", "");
        if (!artwork.isEmpty()) {
            final String key = artwork;
            final int requestRevision = ++artworkRevision;
            IMAGE_WORKER.execute(() -> {
                Bitmap decoded = BitmapFactory.decodeFile(key);
                if (decoded == null) return;
                Bitmap square = centerCrop(decoded, 128);
                MAIN.post(() -> {
                    if (!isCurrentArtwork(key, requestRevision)) return;
                    cover = square;
                    ++artworkRevision;
                    if (PlaybackService.instance != null) PlaybackService.instance.publish();
                });
            });
        } else { ++artworkRevision; cover = null; }
        position = p.getLong("position", 0); duration = p.getLong("duration", 0); playing = false; active = buffering = preparing = false;
    }
    static void dispatch(Context ctx, int action, long value) {
        if (action == 6) { clearSavedTrack(ctx); restorePending = false; }
        if (ready) {
            if (action == 5 || action == 6) recordDeferredAction(action);
            nativeAction(action, value);
        }
        else {
            recordDeferredAction(action);
            Intent launch = ctx.getPackageManager().getLaunchIntentForPackage(ctx.getPackageName());
            if (launch != null) { launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); ctx.startActivity(launch); }
        }
    }
    static synchronized void recordDeferredAction(int action) {
        if (action == 0 || action == 4) pendingPlay = true;
        else if (action == 5 || action == 6) pendingPlay = false;
    }
    static synchronized boolean consumePendingPlay(boolean trackAvailable) {
        if (!trackAvailable || !pendingPlay || !ready) return false;
        pendingPlay = false;
        return true;
    }
    static boolean isCurrentArtwork(String key, int revision) {
        return key.equals(artwork) && revision == artworkRevision;
    }
    static int generation() { return lifecycle; }
    static PendingIntent openApp(Context ctx) {
        Intent launch = ctx.getPackageManager().getLaunchIntentForPackage(ctx.getPackageName());
        return PendingIntent.getActivity(ctx, 20, launch, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }
    static PendingIntent control(Context ctx, int action) {
        Intent intent = new Intent(ctx, MediaControlReceiver.class).setAction("org.scrip.app.MEDIA_CONTROL").putExtra("control", action);
        return PendingIntent.getBroadcast(ctx, action, intent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }
    private static Bitmap centerCrop(Bitmap source, int size) {
        Bitmap result = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(result);
        float scale = Math.max(size / (float) source.getWidth(), size / (float) source.getHeight());
        float w = source.getWidth() * scale, h = source.getHeight() * scale;
        canvas.drawBitmap(source, null, new RectF((size - w) / 2, (size - h) / 2, (size + w) / 2, (size + h) / 2), new Paint(Paint.FILTER_BITMAP_FLAG));
        return result;
    }
}
