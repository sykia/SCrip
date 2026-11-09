package org.scrip.app;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/** Команды системного уведомления плеера. */
public final class MediaControlReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        if ("org.scrip.app.MEDIA_CONTROL".equals(intent.getAction()))
            MediaBridge.dispatch(context, intent.getIntExtra("control", 0), 0);
    }
}
