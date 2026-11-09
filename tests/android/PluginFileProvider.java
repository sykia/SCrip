package org.scrip.test;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;

/** Test-only provider that exposes deterministic bounded-read fixtures. */
public final class PluginFileProvider extends ContentProvider {
    @Override public boolean onCreate() { return true; }
    @Override public Cursor query(Uri uri, String[] projection, String selection,
            String[] selectionArgs, String sortOrder) { return null; }
    @Override public String getType(Uri uri) { return "application/json"; }
    @Override public Uri insert(Uri uri, ContentValues values) { return null; }
    @Override public int delete(Uri uri, String selection, String[] selectionArgs) { return 0; }
    @Override public int update(Uri uri, ContentValues values, String selection,
            String[] selectionArgs) { return 0; }

    @Override public ParcelFileDescriptor openFile(Uri uri, String mode)
            throws FileNotFoundException {
        if (!"r".equals(mode)) throw new FileNotFoundException("read-only test provider");
        String fixture = uri.getLastPathSegment();
        int size = "exact".equals(fixture) ? 131072
                : "oversize".equals(fixture) ? 131073 : 11;
        final byte[] bytes = new byte[size];
        for (int i = 0; i < size; ++i) bytes[i] = (byte) (i & 0x7f);
        try {
            ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createPipe();
            Thread writer = new Thread(() -> {
                try (FileOutputStream output = new FileOutputStream(pipe[1].getFileDescriptor())) {
                    output.write(bytes);
                } catch (IOException ignored) {
                } finally {
                    try { pipe[1].close(); } catch (IOException ignored) {}
                }
            }, "plugin-file-fixture");
            writer.start();
            return pipe[0];
        } catch (IOException error) {
            throw new FileNotFoundException(error.getMessage());
        }
    }
}
