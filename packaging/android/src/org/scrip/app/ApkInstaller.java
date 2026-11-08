package org.scrip.app;

import android.content.Context;
import android.content.Intent;
import android.content.ClipData;
import android.net.Uri;
import android.util.Log;
import android.os.Build;
import android.provider.Settings;
import androidx.core.content.FileProvider;
import java.io.File;

public final class ApkInstaller {
    private ApkInstaller() {}

    public static boolean install(Context context, String path) {
        try {
            if (Build.VERSION.SDK_INT >= 26 && !context.getPackageManager().canRequestPackageInstalls()) {
                Intent permission = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                    Uri.parse("package:" + context.getPackageName()));
                permission.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                context.startActivity(permission);
                return false;
            }
            File apk = new File(path);
            if (!apk.isFile() || apk.length() == 0) return false;
            Uri uri = FileProvider.getUriForFile(context, context.getPackageName() + ".qtprovider", apk);
            Intent intent = new Intent(Intent.ACTION_VIEW);
            intent.setDataAndType(uri, "application/vnd.android.package-archive");
            intent.setClipData(ClipData.newRawUri("SCrip update", uri));
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(intent);
            return true;
        } catch (RuntimeException error) {
            Log.e("SCrip", "Не удалось открыть установщик APK", error);
            return false;
        }
    }
}
