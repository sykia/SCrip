package org.scrip.app;

import android.view.View;
import android.content.Intent;
import android.os.Bundle;
import androidx.core.view.WindowCompat;
import org.qtproject.qt.android.bindings.QtActivity;

/** Keep system bar icons readable in both SCrip themes. */
public final class SCripActivity extends QtActivity {
    private volatile boolean darkTheme = true;
    private static String pendingLink = "";
    private static boolean linksReady = false;
    private static native void nativeOpenLink(String link);

    public static synchronized void initializeLinks() {
        linksReady = true;
        if (!pendingLink.isEmpty()) { nativeOpenLink(pendingLink); pendingLink = ""; }
    }

    private static synchronized void receiveLink(Intent intent) {
        if (intent == null) return;
        String link = Intent.ACTION_VIEW.equals(intent.getAction()) ? intent.getDataString()
                : Intent.ACTION_SEND.equals(intent.getAction()) ? intent.getStringExtra(Intent.EXTRA_TEXT) : null;
        if (link != null && link.length() <= 8192) {
            if (linksReady) nativeOpenLink(link);
            else pendingLink = link;
        }
    }

    @Override public void onCreate(Bundle state) {
        receiveLink(getIntent());
        super.onCreate(state);
    }

    @Override public void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        receiveLink(intent);
    }


    public void setDarkTheme(boolean dark) {
        darkTheme = dark;
        runOnUiThread(this::updateStatusBarIcons);
    }

    private void updateStatusBarIcons() {
        final View decor = getWindow().getDecorView();
        // После обработки Qt: иначе флаги окна могут перекрыть XML-тему.
        decor.post(() -> {
            if (!isFinishing() && !isDestroyed()) {
                WindowCompat.getInsetsController(getWindow(), decor)
                        .setAppearanceLightStatusBars(!darkTheme);
                WindowCompat.getInsetsController(getWindow(), decor)
                        .setAppearanceLightNavigationBars(!darkTheme);
            }
        });
    }

    @Override public void onResume() {
        super.onResume();
        updateStatusBarIcons();
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) updateStatusBarIcons();
    }
}
