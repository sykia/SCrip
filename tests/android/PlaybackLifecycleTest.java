package org.scrip.test;

import android.app.*;
import android.appwidget.AppWidgetManager;
import android.content.*;
import android.graphics.*;
import android.media.session.*;
import android.os.*;
import java.lang.reflect.*;

/** Проверяет настоящую службу на Android без SoundCloud и загрузки музыки. */
public final class PlaybackLifecycleTest extends Instrumentation {
    private Class<?> bridge, service;
    private Activity activity;
    private Bundle testArguments;
    private Object field(Class<?> type, Object instance, String name) throws Exception {
        Field f = type.getDeclaredField(name); f.setAccessible(true); return f.get(instance);
    }
    private void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private String argument(String key, String fallback) {
        String value = testArguments == null ? null : testArguments.getString(key);
        return value == null ? fallback : value;
    }
    private Bitmap previewArtwork() {
        Bitmap bitmap = Bitmap.createBitmap(128, 128, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        paint.setShader(new LinearGradient(0, 0, 128, 128, new int[]{0xff173b43, 0xff548c91, 0xffe58c9a}, null, Shader.TileMode.CLAMP));
        canvas.drawRect(0, 0, 128, 128, paint);
        paint.setShader(null); paint.setColor(0x88eaa4ae); paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(16);
        Path wave = new Path(); wave.moveTo(-12, 76); wave.cubicTo(24, 25, 50, 114, 84, 61); wave.cubicTo(104, 32, 123, 42, 145, 8);
        canvas.drawPath(wave, paint);
        return bitmap;
    }
    private void configureWidgetSize(Context context, Class<?> widget) throws Exception {
        String requested = argument("previewCompact", "false");
        boolean compact = Boolean.parseBoolean(requested);
        AppWidgetManager manager = AppWidgetManager.getInstance(context);
        int[] ids = manager.getAppWidgetIds(new ComponentName(context, widget));
        for (int id : ids) {
            Bundle options = manager.getAppWidgetOptions(id);
            int width = compact ? 250 : 360, height = compact ? 110 : 180;
            options.putInt(AppWidgetManager.OPTION_APPWIDGET_MIN_WIDTH, width);
            options.putInt(AppWidgetManager.OPTION_APPWIDGET_MAX_WIDTH, width);
            options.putInt(AppWidgetManager.OPTION_APPWIDGET_MIN_HEIGHT, height);
            options.putInt(AppWidgetManager.OPTION_APPWIDGET_MAX_HEIGHT, height);
            if (Build.VERSION.SDK_INT >= 31) {
                java.util.ArrayList<android.util.SizeF> sizes = new java.util.ArrayList<>();
                sizes.add(new android.util.SizeF(width, height));
                options.putParcelableArrayList(AppWidgetManager.OPTION_APPWIDGET_SIZES, sizes);
            }
            manager.updateAppWidgetOptions(id, options);
        }
    }
    private void snapshot(boolean active, boolean playing, boolean buffering, boolean hasTrack) throws Exception {
        String json = "{\"title\":\"Lifecycle test\",\"artist\":\"SCrip\",\"active\":" + active
            + ",\"playing\":" + playing + ",\"buffering\":" + buffering + ",\"hasTrack\":" + hasTrack + "}";
        bridge.getMethod("update", String.class).invoke(null, json);
        waitForIdleSync(); SystemClock.sleep(200); waitForIdleSync();
    }
    private void check(boolean foreground, boolean awake, int state) throws Exception {
        Object instance = field(service, null, "instance");
        require(instance != null, "Служба исчезла");
        require((Boolean) field(service, instance, "foreground") == foreground, "Неверный foreground");
        PowerManager.WakeLock lock = (PowerManager.WakeLock) field(service, instance, "wakeLock");
        require(lock.isHeld() == awake, "Неверное состояние wake lock");
        MediaSession session = (MediaSession) field(service, instance, "session");
        require(session.getController().getPlaybackState().getState() == state, "Неверное состояние медиасессии");
        require(!session.getController().getPlaybackState().getCustomActions().isEmpty(), "Нет действия полной остановки");
    }
    private void testDeferredPlay() throws Exception {
        Method record = bridge.getDeclaredMethod("recordDeferredAction", int.class); record.setAccessible(true);
        Method consume = bridge.getDeclaredMethod("consumePendingPlay", boolean.class); consume.setAccessible(true);
        Field ready = bridge.getDeclaredField("ready"); ready.setAccessible(true);
        Field pending = bridge.getDeclaredField("pendingPlay"); pending.setAccessible(true);
        pending.setBoolean(null, false);
        require(!(Boolean) consume.invoke(null, true), "Обычный запуск сам запросил Play");
        ready.setBoolean(null, false);
        record.invoke(null, 0); record.invoke(null, 0);
        require((Boolean) pending.get(null), "Холодный Play не сохранён");
        require(!(Boolean) consume.invoke(null, true), "Play выполнен до готовности Qt");
        ready.setBoolean(null, true);
        require(!(Boolean) consume.invoke(null, false), "Play выполнен без трека");
        require((Boolean) pending.get(null), "Намерение Play потеряно без трека");
        require((Boolean) consume.invoke(null, true), "Отложенный Play не выполнен после готовности и появления трека");
        require(!(Boolean) consume.invoke(null, true), "Play выполнен повторно");
        ready.setBoolean(null, false); record.invoke(null, 0); record.invoke(null, 5);
        ready.setBoolean(null, true);
        require(!(Boolean) consume.invoke(null, true), "Play сработал после явной отмены");
        ready.setBoolean(null, true);
        Method dispatch = bridge.getDeclaredMethod("dispatch", Context.class, int.class, long.class); dispatch.setAccessible(true);
        dispatch.invoke(null, getTargetContext(), 5, 0L);
        require(!(Boolean) pending.get(null), "Тёплая команда ошибочно стала отложенным Play");
    }
    private void testSavedTrackSnapshot() throws Exception {
        Context ctx = getTargetContext();
        android.content.SharedPreferences prefs = ctx.getSharedPreferences("music_widget", Context.MODE_PRIVATE);
        prefs.edit().clear().putString("restore_track", "{\"urn\":\"soundcloud:tracks:42\",\"title\":\"Сохранённый\",\"duration\":90000,\"artwork_url\":\"https://i1.sndcdn.com/test.jpg\",\"user\":{\"username\":\"Автор\"}}")
            .putLong("restore_position", 12345).apply();
        Method read = bridge.getDeclaredMethod("readSavedTrack", Context.class); read.setAccessible(true);
        org.json.JSONObject saved = (org.json.JSONObject) read.invoke(null, ctx);
        require(saved != null && saved.getJSONObject("track").getString("urn").equals("soundcloud:tracks:42"), "Холодный снимок трека не восстановлен");
        require(saved.getLong("position") == 12345, "Позиция холодного снимка потеряна");
        require(!saved.toString().contains("token") && !saved.toString().contains("secret"), "Снимок содержит секреты");
        Method clear = bridge.getDeclaredMethod("clearSavedTrack", Context.class); clear.setAccessible(true); clear.invoke(null, ctx);
        require(read.invoke(null, ctx) == null, "Явная остановка не удалила снимок трека");
    }
    private void testArtworkPublication() throws Exception {
        Object instance = field(service, null, "instance");
        require(instance != null, "Нет службы для теста обложки");
        Field artwork = bridge.getDeclaredField("artwork"); artwork.setAccessible(true);
        Field revision = bridge.getDeclaredField("artworkRevision"); revision.setAccessible(true);
        Field cover = bridge.getDeclaredField("cover"); cover.setAccessible(true);
        Field position = bridge.getDeclaredField("position"); position.setAccessible(true);
        Method key = service.getDeclaredMethod("metadataKey"); key.setAccessible(true);
        String originalArtwork = (String) artwork.get(null);
        int originalRevision = revision.getInt(null);
        long originalPosition = position.getLong(null);
        artwork.set(null, "same-track-art"); revision.setInt(null, originalRevision + 1);
        String loadingKey = (String) key.invoke(instance);
        position.setLong(null, originalPosition + 1000);
        require(loadingKey.equals(key.invoke(instance)), "Прогресс изменил ключ метаданных");
        position.setLong(null, originalPosition);
        Method currentArtwork = bridge.getDeclaredMethod("isCurrentArtwork", String.class, int.class); currentArtwork.setAccessible(true);
        require((Boolean) currentArtwork.invoke(null, "same-track-art", originalRevision + 1), "Текущая загрузка обложки ошибочно устарела");
        require(!(Boolean) currentArtwork.invoke(null, "same-track-art", originalRevision), "Устаревший результат принят");
        Bitmap image = previewArtwork(); cover.set(null, image); revision.setInt(null, originalRevision + 2);
        String loadedKey = (String) key.invoke(instance);
        require(!loadingKey.equals(loadedKey), "Готовность Bitmap не инвалидировала кэш метаданных");
        Method publish = service.getDeclaredMethod("publish"); publish.setAccessible(true); publish.invoke(instance);
        MediaSession session = (MediaSession) field(service, instance, "session");
        Bitmap published = session.getController().getMetadata().getBitmap(android.media.MediaMetadata.METADATA_KEY_ALBUM_ART);
        require(published != null && published.getWidth() == image.getWidth(), "Обложка не опубликована в MediaSession");
        artwork.set(null, originalArtwork); cover.set(null, null); revision.setInt(null, originalRevision + 3);
        publish.invoke(instance);
        require(session.getController().getMetadata().getBitmap(android.media.MediaMetadata.METADATA_KEY_ALBUM_ART) == null, "Отсутствие обложки не сбросило старые метаданные");
    }
    private void testNotificationProgressIsolation() throws Exception {
        Object instance = field(service, null, "instance");
        Field key = service.getDeclaredField("notificationKey"); key.setAccessible(true);
        Field position = bridge.getDeclaredField("position"); position.setAccessible(true);
        Field duration = bridge.getDeclaredField("duration"); duration.setAccessible(true);
        long oldPosition = position.getLong(null), oldDuration = duration.getLong(null);
        Method publish = service.getDeclaredMethod("publish"); publish.setAccessible(true);
        String notification = (String) key.get(instance);
        position.setLong(null, oldPosition + 1000); duration.setLong(null, oldDuration + 1000);
        publish.invoke(instance);
        require(notification.equals(key.get(instance)), "Прогресс пересоздал бы системное уведомление");
        MediaSession session = (MediaSession) field(service, instance, "session");
        require(session.getController().getPlaybackState().getPosition() == oldPosition + 1000, "Позиция не обновилась в MediaSession");
        position.setLong(null, oldPosition); duration.setLong(null, oldDuration); publish.invoke(instance);
        Field liked = bridge.getDeclaredField("liked"); liked.setAccessible(true);
        boolean oldLiked = liked.getBoolean(null); liked.setBoolean(null, !oldLiked); publish.invoke(instance);
        require(!notification.equals(key.get(instance)), "Смена лайка не обновила уведомление");
        liked.setBoolean(null, oldLiked); publish.invoke(instance);
    }
    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); testArguments = arguments; start(); }
    @Override public void onStart() {
        Bundle result = new Bundle();
        try {
            Context ctx = getTargetContext();
            bridge = ctx.getClassLoader().loadClass("org.scrip.app.MediaBridge");
            service = ctx.getClassLoader().loadClass("org.scrip.app.PlaybackService");
            Intent launch = ctx.getPackageManager().getLaunchIntentForPackage(ctx.getPackageName());
            activity = startActivitySync(launch);
            for (int i = 0; i < 100 && !(Boolean) field(bridge, null, "ready"); ++i) SystemClock.sleep(100);
            require((Boolean) field(bridge, null, "ready"), "Qt-мост не готов");
            testDeferredPlay();
            testSavedTrackSnapshot();
            int previewStyle = -1;
            if (testArguments != null && testArguments.containsKey("previewStyle"))
                previewStyle = Integer.parseInt(testArguments.getString("previewStyle"));
            if (previewStyle >= 0) {
                Class<?> widget = ctx.getClassLoader().loadClass("org.scrip.app.MusicWidget");
                String previewTitle = argument("previewTitle", "");
                Field title = bridge.getDeclaredField("title"); title.setAccessible(true);
                Field artist = bridge.getDeclaredField("artist"); artist.setAccessible(true);
                Field hasTrack = bridge.getDeclaredField("hasTrack"); hasTrack.setAccessible(true);
                Field playing = bridge.getDeclaredField("playing"); playing.setAccessible(true);
                Field liked = bridge.getDeclaredField("liked"); liked.setAccessible(true);
                title.set(null, previewTitle.isEmpty() ? "SCrip" : previewTitle);
                artist.set(null, argument("previewArtist", "Включи музыку в приложении"));
                hasTrack.setBoolean(null, !previewTitle.isEmpty());
                playing.setBoolean(null, !previewTitle.isEmpty());
                liked.setBoolean(null, !previewTitle.isEmpty());
                Field cover = bridge.getDeclaredField("cover"); cover.setAccessible(true);
                Field blurred = bridge.getDeclaredField("blurredCover"); blurred.setAccessible(true);
                Field rounded = bridge.getDeclaredField("roundedCover"); rounded.setAccessible(true);
                boolean withArtwork = Boolean.parseBoolean(argument("previewCover", "false"));
                Bitmap art = withArtwork ? previewArtwork() : null;
                cover.set(null, art); blurred.set(null, art); rounded.set(null, null);
                Field accent = bridge.getDeclaredField("widgetAccent"); accent.setAccessible(true); accent.setInt(null, 0xffeaa2ae);
                configureWidgetSize(ctx, widget);
                bridge.getMethod("setWidgetStyle", int.class).invoke(null, previewStyle);
                waitForIdleSync(); SystemClock.sleep(250); waitForIdleSync();
                result.putString("stream", "STYLE: " + previewStyle + "\n");
                finish(Activity.RESULT_OK, result);
                return;
            }
            Class<?> widget = ctx.getClassLoader().loadClass("org.scrip.app.MusicWidget");
            Method setStyle = bridge.getMethod("setWidgetStyle", int.class);
            Method getStyle = bridge.getMethod("getWidgetStyle");
            Field hasTrack = bridge.getDeclaredField("hasTrack"); hasTrack.setAccessible(true);
            Field title = bridge.getDeclaredField("title"); title.setAccessible(true);
            Field artist = bridge.getDeclaredField("artist"); artist.setAccessible(true);
            Method render = widget.getDeclaredMethod("views", Context.class, int.class, boolean.class); render.setAccessible(true);
            title.set(null, "Очень длинное название композиции для проверки обрезания многоточием в границах карточки");
            artist.set(null, "Исполнитель с длинным именем для проверки адаптивной ширины");
            hasTrack.setBoolean(null, true);
            for (int style = 0; style < 3; ++style) {
                setStyle.invoke(null, style);
                require((Integer) getStyle.invoke(null) == style, "Стиль виджета не сохранился: " + style);
                require(render.invoke(null, getTargetContext(), style, false) != null, "Не создан полный вариант виджета: " + style);
                require(render.invoke(null, getTargetContext(), style, true) != null, "Не создан компактный вариант виджета: " + style);
            }
            hasTrack.setBoolean(null, false);
            require(render.invoke(null, getTargetContext(), 0, true) != null, "Не создан виджет без активного трека");
            SystemClock.sleep(2000);
            final boolean[] darkStatusIcons = {false};
            runOnMainSync(() -> {
                if (Build.VERSION.SDK_INT >= 30) {
                    android.view.WindowInsetsController controller = activity.getWindow().getInsetsController();
                    darkStatusIcons[0] = controller != null && (controller.getSystemBarsAppearance()
                            & android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS) != 0;
                } else {
                    darkStatusIcons[0] = (activity.getWindow().getDecorView().getSystemUiVisibility()
                            & android.view.View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR) != 0;
                }
            });
            require(darkStatusIcons[0], "Значки строки состояния должны быть тёмными");
            bridge.getMethod("preparePlayback").invoke(null);
            snapshot(true, true, false, true);
            check(true, true, PlaybackState.STATE_PLAYING);
            testArtworkPublication();
            testNotificationProgressIsolation();
            Object playbackService = field(service, null, "instance");
            Method focus = service.getDeclaredMethod("handleAudioFocusChange", int.class); focus.setAccessible(true);
            focus.invoke(playbackService, android.media.AudioManager.AUDIOFOCUS_LOSS_TRANSIENT);
            require((Boolean) field(service, playbackService, "resumeAfterFocus"), "Не сохранён намеренный resume");
            focus.invoke(playbackService, android.media.AudioManager.AUDIOFOCUS_GAIN);
            require(!(Boolean) field(service, playbackService, "resumeAfterFocus"), "Resume не погашен после возврата фокуса");
            focus.invoke(playbackService, android.media.AudioManager.AUDIOFOCUS_GAIN);
            require(!(Boolean) field(service, playbackService, "resumeAfterFocus"), "Повторный gain запланировал второй resume");
            focus.invoke(playbackService, android.media.AudioManager.AUDIOFOCUS_LOSS_TRANSIENT);
            service.getDeclaredMethod("cancelFocusResume").setAccessible(true);
            Method cancel = service.getDeclaredMethod("cancelFocusResume"); cancel.setAccessible(true); cancel.invoke(playbackService);
            focus.invoke(playbackService, android.media.AudioManager.AUDIOFOCUS_GAIN);
            require(!(Boolean) field(service, playbackService, "resumeAfterFocus"), "Ручная остановка не отменила resume");
            focus.invoke(playbackService, android.media.AudioManager.AUDIOFOCUS_LOSS);
            require(!(Boolean) field(service, playbackService, "resumeAfterFocus"), "Постоянная потеря оставила resume");
            // Уводим настоящее Qt-приложение с экрана, не закрывая его.
            runOnMainSync(() -> activity.moveTaskToBack(true));
            snapshot(true, false, false, true);
            check(true, true, PlaybackState.STATE_BUFFERING);
            bridge.getMethod("preparePlayback").invoke(null);
            snapshot(true, false, true, true);
            check(true, true, PlaybackState.STATE_BUFFERING);
            SystemClock.sleep(32000); // Старый таймер останавливал службу через 30 секунд.
            check(true, true, PlaybackState.STATE_BUFFERING);
            snapshot(true, true, false, true);
            check(true, true, PlaybackState.STATE_PLAYING);
            snapshot(false, false, false, true);
            check(false, false, PlaybackState.STATE_PAUSED);
            snapshot(true, true, false, true);
            check(true, true, PlaybackState.STATE_PLAYING);
            snapshot(false, false, false, false);
            for (int i = 0; i < 50 && field(service, null, "instance") != null; ++i) SystemClock.sleep(100);
            require(field(service, null, "instance") == null, "Stop не завершил службу");
            // Отмена/ошибка может опередить создание запрошенной foreground-службы.
            bridge.getMethod("preparePlayback").invoke(null);
            snapshot(false, false, false, true);
            SystemClock.sleep(6000);
            check(false, false, PlaybackState.STATE_PAUSED);
            snapshot(false, false, false, false);
            result.putString("stream", "PASS: фон, переход, буферизация >30 с, пауза, продолжение, остановка\n");
            finish(Activity.RESULT_OK, result);
        } catch (Throwable error) {
            result.putString("stream", "FAIL: " + error + "\n");
            finish(Activity.RESULT_CANCELED, result);
        }
    }
}
