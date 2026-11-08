package org.scrip.app;

import android.content.Context;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.AtomicFile;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/** Только JNI хоста. Ключ не экспортируется; файлы исключены из backup. */
public final class SecretVault {
    private static final String ALIAS = "org.scrip.oauth.v1";
    private SecretVault() {}

    private static AtomicFile file(Context context, String record) throws Exception {
        if (!record.equals("connection") && !record.equals("session")) throw new Exception();
        File dir = new File(context.getNoBackupFilesDir(), "oauth");
        if (!dir.isDirectory() && !dir.mkdirs()) throw new Exception();
        return new AtomicFile(new File(dir, record + ".gcm"));
    }
    private static boolean exists(AtomicFile target) {
        return target.getBaseFile().exists() || new File(target.getBaseFile().getPath() + ".bak").exists();
    }
    private static SecretKey key(boolean create) throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        if (!store.containsAlias(ALIAS)) {
            if (!create) throw new Exception();
            KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
            generator.init(new KeyGenParameterSpec.Builder(ALIAS,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256).build());
            generator.generateKey();
        }
        return (SecretKey) store.getKey(ALIAS, null);
    }
    public static synchronized String read(Context context, String record) {
        try {
            AtomicFile target = file(context, record);
            if (!exists(target)) return "";
            byte[] bytes = target.readFully();
            if (bytes.length < 29 || bytes.length > 1024 * 1024 || bytes[0] != 1) return null;
            byte[] iv = java.util.Arrays.copyOfRange(bytes, 1, 13);
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, key(false), new GCMParameterSpec(128, iv));
            cipher.updateAAD(record.getBytes(StandardCharsets.UTF_8));
            return new String(cipher.doFinal(bytes, 13, bytes.length - 13), StandardCharsets.UTF_8);
        } catch (Exception ignored) { return null; }
    }
    public static synchronized boolean write(Context context, String record, String value) {
        AtomicFile target = null;
        FileOutputStream output = null;
        try {
            target = file(context, record);
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, key(true));
            cipher.updateAAD(record.getBytes(StandardCharsets.UTF_8));
            byte[] encrypted = cipher.doFinal(value.getBytes(StandardCharsets.UTF_8));
            byte[] bytes = ByteBuffer.allocate(1 + 12 + encrypted.length).put((byte) 1).put(cipher.getIV()).put(encrypted).array();
            output = target.startWrite(); output.write(bytes); target.finishWrite(output);
            return true;
        } catch (Exception ignored) {
            if (target != null && output != null) target.failWrite(output);
            return false;
        }
    }
    public static synchronized boolean remove(Context context, String record) {
        try {
            AtomicFile target = file(context, record); target.delete(); return !exists(target);
        } catch (Exception ignored) { return false; }
    }
}
