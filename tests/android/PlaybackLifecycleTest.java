package org.scrip.test;

import android.app.*;
import android.content.*;
import android.media.session.*;
import android.os.*;
import java.lang.reflect.*;

/** Проверяет настоящую службу на Android без SoundCloud и загрузки музыки. */
public final class PlaybackLifecycleTest extends Instrumentation {
    private Class<?> bridge, service;
    private Activity activity;
    private Object field(Class<?> type, Object instance, String name) throws Exception {
        Field f = type.getDeclaredField(name); f.setAccessible(true); return f.get(instance);
    }
    private void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
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
    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }
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
