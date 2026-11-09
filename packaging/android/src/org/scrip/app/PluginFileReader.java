package org.scrip.app;

import android.content.ContentResolver;
import android.content.Context;
import android.net.Uri;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;

/** Bounded reader for documents returned by Android's system picker. */
public final class PluginFileReader {
    private PluginFileReader() {}

    public static byte[] read(Context context, String uriText, int maximumBytes) {
        if (context == null || uriText == null || maximumBytes <= 0 || maximumBytes > 1_048_576) return null;
        Uri uri = Uri.parse(uriText);
        if (!"content".equals(uri.getScheme()) || uri.getAuthority() == null) return null;
        ContentResolver resolver = context.getContentResolver();
        byte[] buffer = new byte[8192];
        try (InputStream input = resolver.openInputStream(uri);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            if (input == null) return null;
            int total = 0;
            while (true) {
                int remaining = (int) Math.min(buffer.length, (long) maximumBytes + 1 - total);
                if (remaining <= 0) break;
                int count = input.read(buffer, 0, remaining);
                if (count < 0) break;
                if (count == 0) {
                    int value = input.read();
                    if (value < 0) break;
                    output.write(value);
                    ++total;
                    continue;
                }
                output.write(buffer, 0, count);
                total += count;
            }
            return output.toByteArray();
        } catch (Exception ignored) {
            return null;
        }
    }
}
