package org.lineage.p11.fbeprobe;

import android.content.Context;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.security.MessageDigest;

/** Small file helpers shared by the seed activity and the boot receiver. */
final class Probe {
    static final String TAG = "FBE_PROBE";
    static final String DE_NAME = "de_probe.txt";
    static final String CE_NAME = "ce_probe.txt";

    private Probe() {
    }

    static File deFilesDir(Context context) {
        // Public API. createCredentialProtectedStorageContext() is not in the
        // current SDK stubs, so CE uses getDataDir()/files instead.
        return context.createDeviceProtectedStorageContext().getFilesDir();
    }

    static File deFile(Context context) {
        return new File(deFilesDir(context), DE_NAME);
    }

    static File ceFile(Context context) {
        // getDataDir() is the credential-protected /data/user/<user> directory.
        return new File(context.getDataDir(), "files/" + CE_NAME);
    }

    static void write(File file, String value) throws Exception {
        File parent = file.getParentFile();
        if (parent != null && !parent.exists()) {
            parent.mkdirs();
        }
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(value.getBytes("UTF-8"));
        }
    }

    static String read(File file) throws Exception {
        try (FileInputStream in = new FileInputStream(file)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            return out.toString("UTF-8");
        }
    }

    static String sha256(File file) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0) {
                md.update(buf, 0, n);
            }
        }
        StringBuilder sb = new StringBuilder();
        for (byte b : md.digest()) {
            sb.append(String.format("%02x", b));
        }
        return sb.toString();
    }

    /** Returns a log-safe status; never throws across the locked-user boundary. */
    static String describe(File file) {
        try {
            return "OK sha=" + sha256(file) + " data=" + read(file);
        } catch (Exception e) {
            return "BLOCKED " + e.getClass().getSimpleName();
        }
    }

    static void logStatus(Context context, String action) {
        String line = "action=" + action
                + " de=" + describe(deFile(context))
                + " ce=" + describe(ceFile(context));
        Log.i(TAG, line);
        try {
            write(new File(deFilesDir(context), "last_probe.txt"), line);
        } catch (Exception e) {
            Log.i(TAG, "last_probe write failed: " + e.getClass().getSimpleName());
        }
    }
}
