package org.scrip.app;

import android.view.View;
import androidx.core.view.WindowCompat;
import org.qtproject.qt.android.bindings.QtActivity;

/** Keep system bar icons readable in both SCrip themes. */
public final class SCripActivity extends QtActivity {
    private volatile boolean darkTheme = true;

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
