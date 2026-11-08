package org.scrip.vaulttest;

import android.app.Instrumentation;
import android.app.Activity;
import android.os.Bundle;
import android.content.Context;
import java.io.File;
import java.nio.file.Files;
import java.security.KeyStore;
import java.util.Arrays;
import org.scrip.app.SecretVault;

/** Выполняется только в отдельном тестовом приложении/UID, без данных SCrip. */
public final class SecretVaultCheck extends Instrumentation {
    private static void check(boolean condition) {
        if (!condition) throw new AssertionError("Проверка SecretVault не пройдена");
    }
    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }
    @Override public void onStart() {
        Bundle result = new Bundle();
        try {
            Context context = getTargetContext();
            check(SecretVault.remove(context, "session"));
            check("".equals(SecretVault.read(context, "session")));
            String fixture = "fixture-secret";
            check(SecretVault.write(context, "session", fixture));
            check(fixture.equals(SecretVault.read(context, "session")));
            File target = new File(context.getNoBackupFilesDir(), "oauth/session.gcm");
            byte[] first = Files.readAllBytes(target.toPath());
            check(!new String(first, java.nio.charset.StandardCharsets.ISO_8859_1).contains(fixture));
            check(SecretVault.write(context, "session", fixture));
            byte[] second = Files.readAllBytes(target.toPath());
            check(!Arrays.equals(first, second));
            second[second.length - 1] ^= 1;
            Files.write(target.toPath(), second);
            check(SecretVault.read(context, "session") == null);
            check(SecretVault.write(context, "session", fixture));
            KeyStore keys = KeyStore.getInstance("AndroidKeyStore"); keys.load(null);
            keys.deleteEntry("org.scrip.oauth.v1");
            check(SecretVault.read(context, "session") == null);
            check(SecretVault.remove(context, "session"));
            check("".equals(SecretVault.read(context, "session")));
            check(!SecretVault.write(context, "../escape", fixture));
            result.putString("result", "PASS: roundtrip, unique IV, ciphertext, tamper, missing key, deletion, path restriction");
            finish(Activity.RESULT_OK, result);
        } catch (Throwable failure) {
            result.putString("result", "FAIL: " + failure.getClass().getSimpleName());
            finish(Activity.RESULT_CANCELED, result);
        }
    }
}
