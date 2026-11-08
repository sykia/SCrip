package org.scrip.app;

import android.app.*;
import android.appwidget.*;
import android.content.*;
import android.content.res.ColorStateList;
import android.os.Bundle;
import android.util.SizeF;
import android.widget.RemoteViews;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

public class MusicWidget extends AppWidgetProvider {
    @Override public void onUpdate(Context context, AppWidgetManager manager, int[] ids) { MediaBridge.restore(context); refresh(context); }
    @Override public void onAppWidgetOptionsChanged(Context context, AppWidgetManager manager, int id, Bundle options) { refresh(context); }
    @Override public void onReceive(Context context, Intent intent) {
        super.onReceive(context, intent);
        if ("org.scrip.app.MEDIA_CONTROL".equals(intent.getAction()))
            MediaBridge.dispatch(context, intent.getIntExtra("control", 0), 0);
    }
    static String time(long ms) {
        long seconds = Math.max(0, ms) / 1000;
        return String.format(Locale.ROOT, "%d:%02d", seconds / 60, seconds % 60);
    }
    static RemoteViews views(Context context, int style, boolean compact) {
        RemoteViews view = new RemoteViews(context.getPackageName(), R.layout.music_widget);
        final boolean blur = style == 0, light = style == 2;
        final boolean lightWide = light && !compact;
        final int text = light ? 0xff222126 : 0xfffaf8fa;
        final int secondary = light ? 0xff77737a : 0xffc8c3c8;
        final int icon = light ? 0xff27252a : 0xfff7f4f6;
        final int accent = MediaBridge.widgetAccent;
        String title = MediaBridge.hasTrack ? MediaBridge.title : "SCrip";
        String artist = MediaBridge.hasTrack ? MediaBridge.artist : "Включи музыку в приложении";
        view.setTextViewText(R.id.widget_title, title);
        view.setTextViewText(R.id.widget_artist, artist);
        view.setTextViewText(R.id.widget_title_light, title);
        view.setTextViewText(R.id.widget_artist_light, artist);
        view.setTextViewText(R.id.widget_elapsed, time(MediaBridge.position));
        view.setTextViewText(R.id.widget_remaining, "−" + time(Math.max(0, MediaBridge.duration - MediaBridge.position)));
        view.setTextViewText(R.id.widget_elapsed_card, time(MediaBridge.position));
        view.setTextViewText(R.id.widget_remaining_card, "−" + time(Math.max(0, MediaBridge.duration - MediaBridge.position)));
        view.setTextViewText(R.id.widget_elapsed_light, time(MediaBridge.position));
        view.setTextViewText(R.id.widget_remaining_light, "−" + time(Math.max(0, MediaBridge.duration - MediaBridge.position)));
        view.setProgressBar(R.id.widget_progress, 1000, MediaBridge.duration > 0 ? (int)Math.min(1000, MediaBridge.position * 1000 / MediaBridge.duration) : 0, false);
        view.setProgressBar(R.id.widget_progress_card, 1000, MediaBridge.duration > 0 ? (int)Math.min(1000, MediaBridge.position * 1000 / MediaBridge.duration) : 0, false);
        view.setProgressBar(R.id.widget_progress_light, 1000, MediaBridge.duration > 0 ? (int)Math.min(1000, MediaBridge.position * 1000 / MediaBridge.duration) : 0, false);
        view.setImageViewBitmap(R.id.widget_cover, MediaBridge.roundedCover(context));
        view.setImageViewBitmap(R.id.widget_cover_light, MediaBridge.roundedCover(context));
        if (blur) view.setImageViewBitmap(R.id.widget_blur_cover, MediaBridge.blurCover(context));
        view.setViewVisibility(R.id.widget_blur_cover, blur ? android.view.View.VISIBLE : android.view.View.GONE);
        view.setViewVisibility(R.id.widget_scrim, blur ? android.view.View.VISIBLE : android.view.View.GONE);
        view.setInt(R.id.widget_background, "setBackgroundResource", blur ? R.drawable.widget_bg_blur : light ? R.drawable.widget_bg_light : R.drawable.widget_bg_dark);
        view.setViewVisibility(R.id.widget_cover, blur ? android.view.View.GONE : android.view.View.VISIBLE);
        view.setViewVisibility(R.id.widget_info, lightWide ? android.view.View.GONE : android.view.View.VISIBLE);
        view.setViewVisibility(R.id.widget_light_info, lightWide ? android.view.View.VISIBLE : android.view.View.GONE);
        view.setViewVisibility(R.id.widget_play_blur, blur ? android.view.View.VISIBLE : android.view.View.GONE);
        view.setViewVisibility(R.id.widget_card_controls, blur || lightWide ? android.view.View.GONE : android.view.View.VISIBLE);
        view.setViewVisibility(R.id.widget_blur_controls, blur ? android.view.View.VISIBLE : android.view.View.GONE);
        view.setViewVisibility(R.id.widget_progress_row, compact || !blur ? android.view.View.GONE : android.view.View.VISIBLE);
        view.setViewVisibility(R.id.widget_card_progress_row, compact || blur || lightWide ? android.view.View.GONE : android.view.View.VISIBLE);
        view.setViewVisibility(R.id.widget_like, compact ? android.view.View.GONE : android.view.View.VISIBLE);
        view.setViewVisibility(R.id.widget_like_card, compact ? android.view.View.GONE : android.view.View.VISIBLE);
        view.setViewVisibility(R.id.widget_play_card, blur || lightWide ? android.view.View.GONE : android.view.View.VISIBLE);
        view.setTextColor(R.id.widget_title, text); view.setTextColor(R.id.widget_artist, secondary);
        view.setTextColor(R.id.widget_title_light, text); view.setTextColor(R.id.widget_artist_light, secondary);
        view.setTextColor(R.id.widget_elapsed, secondary); view.setTextColor(R.id.widget_remaining, secondary);
        view.setTextColor(R.id.widget_elapsed_card, secondary); view.setTextColor(R.id.widget_remaining_card, secondary);
        view.setTextColor(R.id.widget_elapsed_light, secondary); view.setTextColor(R.id.widget_remaining_light, secondary);
        view.setTextViewTextSize(R.id.widget_title, android.util.TypedValue.COMPLEX_UNIT_SP, blur ? 18 : 16);
        view.setInt(R.id.widget_title, "setMaxLines", blur ? 2 : 1);
        boolean isPlaying = MediaBridge.playing || MediaBridge.buffering;
        int play = isPlaying ? R.drawable.widget_pause : R.drawable.widget_play;
        view.setImageViewResource(R.id.widget_play_blur, play);
        view.setImageViewResource(R.id.widget_play_card, play);
        view.setImageViewResource(R.id.widget_play_light, play);
        int heart = MediaBridge.liked ? R.drawable.widget_heart_filled : R.drawable.widget_heart;
        view.setImageViewResource(R.id.widget_like, heart);
        view.setImageViewResource(R.id.widget_like_card, heart);
        view.setImageViewResource(R.id.widget_like_light, heart);
        int[] icons = {R.id.widget_play_blur, R.id.widget_play_card, R.id.widget_previous, R.id.widget_next,
            R.id.widget_previous_card, R.id.widget_next_card, R.id.widget_like, R.id.widget_like_card,
            R.id.widget_previous_light, R.id.widget_next_light, R.id.widget_like_light};
        for (int id : icons) view.setInt(id, "setColorFilter", icon);
        int mainIcon = light ? 0xffffffff : 0xff302a2d;
        view.setInt(R.id.widget_play_blur, "setColorFilter", 0xff302a2d);
        view.setInt(R.id.widget_play_card, "setColorFilter", mainIcon);
        view.setInt(R.id.widget_play_light, "setColorFilter", 0xffffffff);
        view.setInt(R.id.widget_play_card, "setBackgroundResource", light ? R.drawable.widget_play_button_light : R.drawable.widget_play_button);
        if (MediaBridge.liked) {
            view.setInt(R.id.widget_like, "setColorFilter", accent);
            view.setInt(R.id.widget_like_card, "setColorFilter", accent);
            view.setInt(R.id.widget_like_light, "setColorFilter", accent);
        }
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            int progressTrack = light ? 0xffd4cccf : blur ? 0x99ffffff : 0xff55565c;
            view.setColorStateList(R.id.widget_progress, "setProgressTintList", ColorStateList.valueOf(accent));
            view.setColorStateList(R.id.widget_progress, "setProgressBackgroundTintList", ColorStateList.valueOf(progressTrack));
            view.setColorStateList(R.id.widget_progress_card, "setProgressTintList", ColorStateList.valueOf(accent));
            view.setColorStateList(R.id.widget_progress_card, "setProgressBackgroundTintList", ColorStateList.valueOf(progressTrack));
            view.setColorStateList(R.id.widget_progress_light, "setProgressTintList", ColorStateList.valueOf(accent));
            view.setColorStateList(R.id.widget_progress_light, "setProgressBackgroundTintList", ColorStateList.valueOf(progressTrack));
            view.setColorStateList(R.id.widget_play_blur, "setBackgroundTintList", ColorStateList.valueOf(accent));
            if (!light) view.setColorStateList(R.id.widget_play_card, "setBackgroundTintList", ColorStateList.valueOf(accent));
        }
        view.setContentDescription(R.id.widget_play_blur, isPlaying ? "Пауза" : "Воспроизвести");
        view.setContentDescription(R.id.widget_play_card, isPlaying ? "Пауза" : "Воспроизвести");
        view.setContentDescription(R.id.widget_play_light, isPlaying ? "Пауза" : "Воспроизвести");
        view.setContentDescription(R.id.widget_like, MediaBridge.liked ? "Убрать из понравившихся" : "Добавить в понравившиеся");
        view.setContentDescription(R.id.widget_like_card, MediaBridge.liked ? "Убрать из понравившихся" : "Добавить в понравившиеся");
        view.setContentDescription(R.id.widget_like_light, MediaBridge.liked ? "Убрать из понравившихся" : "Добавить в понравившиеся");
        view.setOnClickPendingIntent(R.id.widget_info, MediaBridge.openApp(context));
        view.setOnClickPendingIntent(R.id.widget_light_info, MediaBridge.openApp(context));
        view.setOnClickPendingIntent(R.id.widget_cover_light, MediaBridge.openApp(context));
        view.setOnClickPendingIntent(R.id.widget_previous, MediaBridge.control(context, 1));
        view.setOnClickPendingIntent(R.id.widget_previous_card, MediaBridge.control(context, 1));
        view.setOnClickPendingIntent(R.id.widget_previous_light, MediaBridge.control(context, 1));
        view.setOnClickPendingIntent(R.id.widget_play_blur, MediaBridge.control(context, 0));
        view.setOnClickPendingIntent(R.id.widget_play_card, MediaBridge.control(context, 0));
        view.setOnClickPendingIntent(R.id.widget_play_light, MediaBridge.control(context, 0));
        view.setOnClickPendingIntent(R.id.widget_next, MediaBridge.control(context, 2));
        view.setOnClickPendingIntent(R.id.widget_next_card, MediaBridge.control(context, 2));
        view.setOnClickPendingIntent(R.id.widget_next_light, MediaBridge.control(context, 2));
        view.setOnClickPendingIntent(R.id.widget_like, MediaBridge.control(context, 7));
        view.setOnClickPendingIntent(R.id.widget_like_card, MediaBridge.control(context, 7));
        view.setOnClickPendingIntent(R.id.widget_like_light, MediaBridge.control(context, 7));
        return view;
    }
    static void refresh(Context context) {
        if (context == null) return;
        AppWidgetManager manager = AppWidgetManager.getInstance(context);
        int[] ids = manager.getAppWidgetIds(new ComponentName(context, MusicWidget.class));
        int style = MediaBridge.getWidgetStyle();
        for (int id : ids) {
            Bundle options = manager.getAppWidgetOptions(id);
            int width = options.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_WIDTH, 250);
            int height = options.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_HEIGHT, 110);
            boolean compact = compact(style, width, height);
            if (android.os.Build.VERSION.SDK_INT >= 31) {
                @SuppressWarnings("unchecked") ArrayList<SizeF> sizes = options.getParcelableArrayList(AppWidgetManager.OPTION_APPWIDGET_SIZES);
                if (sizes != null && !sizes.isEmpty()) {
                    Map<SizeF, RemoteViews> responsive = new HashMap<>();
                    for (SizeF size : sizes) {
                        boolean small = compact(style, size.getWidth(), size.getHeight());
                        responsive.put(size, views(context, style, small));
                    }
                    manager.updateAppWidget(id, new RemoteViews(responsive));
                    continue;
                }
            }
            manager.updateAppWidget(id, views(context, style, compact));
        }
    }
    static void refreshProgress(Context context) {
        if (context == null) return;
        AppWidgetManager manager = AppWidgetManager.getInstance(context);
        int[] ids = manager.getAppWidgetIds(new ComponentName(context, MusicWidget.class));
        if (ids.length == 0) return;
        RemoteViews progress = new RemoteViews(context.getPackageName(), R.layout.music_widget);
        progress.setTextViewText(R.id.widget_elapsed, time(MediaBridge.position));
        progress.setTextViewText(R.id.widget_remaining, "−" + time(Math.max(0, MediaBridge.duration - MediaBridge.position)));
        progress.setTextViewText(R.id.widget_elapsed_card, time(MediaBridge.position));
        progress.setTextViewText(R.id.widget_remaining_card, "−" + time(Math.max(0, MediaBridge.duration - MediaBridge.position)));
        progress.setTextViewText(R.id.widget_elapsed_light, time(MediaBridge.position));
        progress.setTextViewText(R.id.widget_remaining_light, "−" + time(Math.max(0, MediaBridge.duration - MediaBridge.position)));
        progress.setProgressBar(R.id.widget_progress, 1000, MediaBridge.duration > 0 ? (int)Math.min(1000, MediaBridge.position * 1000 / MediaBridge.duration) : 0, false);
        progress.setProgressBar(R.id.widget_progress_card, 1000, MediaBridge.duration > 0 ? (int)Math.min(1000, MediaBridge.position * 1000 / MediaBridge.duration) : 0, false);
        progress.setProgressBar(R.id.widget_progress_light, 1000, MediaBridge.duration > 0 ? (int)Math.min(1000, MediaBridge.position * 1000 / MediaBridge.duration) : 0, false);
        manager.partiallyUpdateAppWidget(ids, progress);
    }
    private static boolean compact(int style, float width, float height) {
        return style == 2 ? width < 320 || height < 150 : width < 270 || height < 140;
    }
}
