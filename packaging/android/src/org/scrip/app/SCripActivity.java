package org.scrip.app;

import android.view.View;
import androidx.core.view.WindowCompat;
import org.qtproject.qt.android.bindings.QtActivity;

/** Тёмные значки строки состояния поверх светлого интерфейса SCrip. */
public final class SCripActivity extends QtActivity {
    private void updateStatusBarIcons() {
        final View decor = getWindow().getDecorView();
        // После обработки Qt: иначе флаги окна могут перекрыть XML-тему.
        decor.post(() -> {
            if (!isFinishing() && !isDestroyed()) {
                WindowCompat.getInsetsController(getWindow(), decor)
                        .setAppearanceLightStatusBars(true);
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
