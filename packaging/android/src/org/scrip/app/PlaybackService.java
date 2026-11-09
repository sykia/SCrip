package org.scrip.app;

import android.app.*;
import android.content.*;
import android.content.pm.ServiceInfo;
import android.media.*;
import android.media.session.*;
import android.os.*;

/** Foreground-служба удерживает Qt-процесс, а не воспроизводит вторую копию аудио. */
public class PlaybackService extends Service {
    static PlaybackService instance;
    private MediaSession session;
    private PowerManager.WakeLock wakeLock;
    private boolean foreground;
    int foregroundStarts, foregroundStops;
    private static final String CHANNEL = "scrip_playback";
    private static final int NOTIFICATION = 44;
    private static final String LIKE = "org.scrip.app.LIKE";
    private AudioManager audioManager;
    private AudioFocusRequest focusRequest;
    private boolean hasFocus, resumeAfterFocus, focusPaused, destroyed;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable focusTimeout = () -> {
        if (!resumeAfterFocus) return;
        resumeAfterFocus = false; focusPaused = false;
        MediaBridge.dispatch(this, 5, 0);
        publish();
    };
    private String metadataKey = "";
    private String notificationKey = "";
    private final BroadcastReceiver noisy = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) { MediaBridge.dispatch(context, 5, 0); }
    };
    @Override public void onCreate() {
        super.onCreate(); instance = this;
        getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel(CHANNEL, "Воспроизведение музыки", NotificationManager.IMPORTANCE_LOW));
        wakeLock = getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "SCrip:Playback"); wakeLock.setReferenceCounted(false);
        audioManager = getSystemService(AudioManager.class);
        focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setOnAudioFocusChangeListener(this::handleAudioFocusChange, MediaBridge.MAIN).build();
        IntentFilter filter = new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY);
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(noisy, filter, Context.RECEIVER_NOT_EXPORTED);
        else registerReceiver(noisy, filter);
        session = new MediaSession(this, "SCrip");
        session.setSessionActivity(MediaBridge.openApp(this));
        session.setCallback(new MediaSession.Callback() {
            @Override public void onPlay() { MediaBridge.dispatch(PlaybackService.this, 4, 0); }
            @Override public void onPause() { cancelFocusResume(); MediaBridge.dispatch(PlaybackService.this, 5, 0); }
            @Override public void onStop() { cancelFocusResume(); MediaBridge.dispatch(PlaybackService.this, 6, 0); }
            @Override public void onCustomAction(String action, Bundle extras) {
                if (LIKE.equals(action)) MediaBridge.dispatch(PlaybackService.this, 7, 0);
            }
            @Override public void onSkipToNext() { MediaBridge.dispatch(PlaybackService.this, 2, 0); }
            @Override public void onSkipToPrevious() { MediaBridge.dispatch(PlaybackService.this, 1, 0); }
            @Override public void onSeekTo(long position) { MediaBridge.dispatch(PlaybackService.this, 3, position); }
        }, MediaBridge.MAIN);
        session.setActive(true);
    }
    void handleAudioFocusChange(int change) {
                if (destroyed) return;
                if (change == AudioManager.AUDIOFOCUS_LOSS) {
                    handler.removeCallbacks(focusTimeout); resumeAfterFocus = false; focusPaused = false; hasFocus = false;
                    MediaBridge.dispatch(this, 5, 0); publish();
                } else if (change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT || change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK) {
                    if (!focusPaused) resumeAfterFocus = MediaBridge.playing && MediaBridge.active;
                    focusPaused = true; hasFocus = false;
                    if (resumeAfterFocus) { handler.removeCallbacks(focusTimeout); handler.postDelayed(focusTimeout, 120000); }
                    MediaBridge.dispatch(this, 8, 0); publish();
                } else if (change == AudioManager.AUDIOFOCUS_GAIN) {
                    hasFocus = true;
                    if (resumeAfterFocus && focusPaused && MediaBridge.ready && MediaBridge.hasTrack) {
                        handler.removeCallbacks(focusTimeout); resumeAfterFocus = false; focusPaused = false;
                        MediaBridge.dispatch(this, 4, 0);
                    }
                }
    }
    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        if (!MediaBridge.ready) { stopSelf(); return START_NOT_STICKY; }
        // Даже если загрузка успела завершиться ошибкой до onStartCommand,
        // сначала выполняем обязательство startForegroundService перед Android.
        Notification pending = new Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.widget_notification).setContentTitle("SCrip")
            .setContentText("Подготовка воспроизведения…").setOngoing(true)
            .setContentIntent(MediaBridge.openApp(this)).build();
        enterForeground(pending);
        publish(); return START_NOT_STICKY;
    }
    void publish() {
        String key = metadataKey();
        if (!metadataKey.equals(key)) {
            metadataKey = key;
            MediaMetadata.Builder metadata = new MediaMetadata.Builder()
                .putString(MediaMetadata.METADATA_KEY_TITLE, MediaBridge.title)
                .putString(MediaMetadata.METADATA_KEY_ARTIST, MediaBridge.artist)
                .putLong(MediaMetadata.METADATA_KEY_DURATION, MediaBridge.duration);
            if (MediaBridge.cover != null) metadata.putBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART, MediaBridge.cover);
            session.setMetadata(metadata.build());
        }
        long actions = PlaybackState.ACTION_PLAY | PlaybackState.ACTION_PAUSE | PlaybackState.ACTION_PLAY_PAUSE | PlaybackState.ACTION_SKIP_TO_NEXT | PlaybackState.ACTION_SKIP_TO_PREVIOUS | PlaybackState.ACTION_SEEK_TO | PlaybackState.ACTION_STOP;
        final boolean active = MediaBridge.active || MediaBridge.preparing;
        final int state = !MediaBridge.hasTrack ? PlaybackState.STATE_STOPPED
            : active && (!MediaBridge.playing || MediaBridge.buffering) ? PlaybackState.STATE_BUFFERING
            : MediaBridge.playing ? PlaybackState.STATE_PLAYING : PlaybackState.STATE_PAUSED;
        final int likeIcon = MediaBridge.liked ? R.drawable.widget_heart_filled : R.drawable.widget_heart;
        final String likeLabel = MediaBridge.liked ? "Снять лайк" : "Поставить лайк";
        session.setPlaybackState(new PlaybackState.Builder().setActions(actions)
            .addCustomAction(LIKE, likeLabel, likeIcon)
            .setState(state, MediaBridge.position, state == PlaybackState.STATE_PLAYING ? 1f : 0f, SystemClock.elapsedRealtime()).build());
        String nextNotificationKey = MediaBridge.title + '\n' + MediaBridge.artist + '\n' + MediaBridge.artworkRevision
            + '\n' + active + '\n' + MediaBridge.playing + '\n' + MediaBridge.buffering + '\n' + MediaBridge.liked;
        if (notificationKey.equals(nextNotificationKey) && !(active && !foreground)) {
            updateForegroundState(active, null);
            if (!MediaBridge.hasTrack && !active) stopSelf();
            return;
        }
        notificationKey = nextNotificationKey;
        Notification notification = new Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.widget_notification).setContentTitle(MediaBridge.title).setContentText(MediaBridge.artist)
            .setLargeIcon(MediaBridge.cover).setContentIntent(MediaBridge.openApp(this))
            .setVisibility(Notification.VISIBILITY_PUBLIC).setOnlyAlertOnce(true).setShowWhen(false)
            .setCategory(Notification.CATEGORY_TRANSPORT).setOngoing(active)
            .addAction(new Notification.Action.Builder(R.drawable.widget_previous, "Предыдущий", MediaBridge.control(this, 1)).build())
            .addAction(new Notification.Action.Builder(active ? R.drawable.widget_pause : R.drawable.widget_play, active ? "Пауза" : "Воспроизвести", MediaBridge.control(this, 0)).build())
            .addAction(new Notification.Action.Builder(R.drawable.widget_next, "Следующий", MediaBridge.control(this, 2)).build())
            .addAction(new Notification.Action.Builder(likeIcon, likeLabel, MediaBridge.control(this, 7)).build())
            .setStyle(new Notification.MediaStyle().setMediaSession(session.getSessionToken()).setShowActionsInCompactView(0, 1, 2)).build();
        updateForegroundState(active, notification);
        if (!MediaBridge.hasTrack && !active) stopSelf();
    }
    private void updateForegroundState(boolean active, Notification notification) {
        if (active) {
            if (!foreground) {
                enterForeground(notification);
            } else if (notification != null) getSystemService(NotificationManager.class).notify(NOTIFICATION, notification);
            if (!wakeLock.isHeld() && MediaBridge.playing && !MediaBridge.buffering) wakeLock.acquire();
            if (!hasFocus) {
                hasFocus = audioManager.requestAudioFocus(focusRequest) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
                if (!hasFocus) { resumeAfterFocus = false; focusPaused = false; MediaBridge.dispatch(this, 5, 0); }
            }
        } else {
            if (notification != null) getSystemService(NotificationManager.class).notify(NOTIFICATION, notification);
            if (wakeLock.isHeld()) wakeLock.release();
            if (resumeAfterFocus) {
                // Keep the existing foreground service alive while a transient focus loss is resumable.
            } else {
                if (foreground) leaveForeground();
                if (hasFocus) { audioManager.abandonAudioFocusRequest(focusRequest); hasFocus = false; }
            }
        }
    }
    private void enterForeground(Notification notification) {
        if (Build.VERSION.SDK_INT >= 29) startForeground(NOTIFICATION, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        else startForeground(NOTIFICATION, notification);
        foreground = true; ++foregroundStarts;
    }
    private void leaveForeground() {
        stopForeground(STOP_FOREGROUND_DETACH);
        foreground = false; ++foregroundStops;
    }
    String metadataKey() {
        return MediaBridge.title + '\n' + MediaBridge.artist + '\n' + MediaBridge.artwork + '\n'
            + MediaBridge.duration + '\n' + MediaBridge.artworkRevision;
    }
    @Override public void onDestroy() {
        destroyed = true; handler.removeCallbacksAndMessages(null); resumeAfterFocus = focusPaused = false;
        foreground = false;
        if (instance == this) instance = null;
        try { unregisterReceiver(noisy); } catch (IllegalArgumentException ignored) {}
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        if (audioManager != null && focusRequest != null) audioManager.abandonAudioFocusRequest(focusRequest);
        if (session != null) { session.setActive(false); session.release(); }
        getSystemService(NotificationManager.class).cancel(NOTIFICATION);
        if (MediaBridge.ready) {
            MediaBridge.playing = MediaBridge.active = MediaBridge.buffering = MediaBridge.preparing = false;
            MediaBridge.dispatch(this, 6, 0);
        } else {
            MediaBridge.playing = MediaBridge.active = MediaBridge.buffering = MediaBridge.preparing = MediaBridge.hasTrack = false;
        }
        super.onDestroy();
    }
    private void cancelFocusResume() {
        handler.removeCallbacks(focusTimeout); resumeAfterFocus = focusPaused = false;
    }
    @Override public IBinder onBind(Intent intent) { return null; }
}
