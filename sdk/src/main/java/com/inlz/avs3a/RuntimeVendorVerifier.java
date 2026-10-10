package com.inlz.avs3a;

import android.content.Context;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/**
 * Locates the application-packaged vendor decoder and verifies its runtime
 * fingerprint (size + SHA-256) against the build-time verified vendor lock
 * (frozen vendor.lock.json). File existence alone never counts as a
 * fingerprint match.
 */
final class RuntimeVendorVerifier {
    static final String VENDOR_ID = "avs3a-ystpzs-1.4.1";

    // From libavs3a_jni-execution-plan/frozen/vendor.lock.json
    private static final String SHA256_ARM64 =
            "4368cd990adcbd841e618bc2de81a96019b24c70b5487912e7ef603067a0eb23";
    private static final String SHA256_ARMV7 =
            "0b32d35280486c7b36033ceb6c9e82ac4d4d55a7a7749a06eb29ab0e9495dccc";
    private static final long SIZE_ARM64 = 259992L;
    private static final long SIZE_ARMV7 = 194176L;

    static final class Result {
        final String path;
        final boolean fingerprintMatched;

        Result(String path, boolean fingerprintMatched) {
            this.path = path;
            this.fingerprintMatched = fingerprintMatched;
        }

        String path() {
            return path;
        }

        boolean fingerprintMatched() {
            return fingerprintMatched;
        }
    }

    private static volatile Result cached;

    static synchronized Result verifyApplicationVendor(Context appContext, int processAbi)
            throws Avs3Exception {
        if (cached != null) return cached;

        if (appContext == null)
            throw new Avs3Exception(Avs3Error.INVALID_ARGUMENT);

        final String abiDir;
        final String expectedSha;
        final long expectedSize;
        switch (processAbi) {
            case 1:
                abiDir = "arm64-v8a";
                expectedSha = SHA256_ARM64;
                expectedSize = SIZE_ARM64;
                break;
            case 2:
                abiDir = "armeabi-v7a";
                expectedSha = SHA256_ARMV7;
                expectedSize = SIZE_ARMV7;
                break;
            default:
                throw new Avs3Exception(Avs3Error.VENDOR_ABI_NOT_READY);
        }

        String nativeLibDir = appContext.getApplicationInfo().nativeLibraryDir;
        File candidate = new File(nativeLibDir, "libavs3a_decoder.so");

        if (!candidate.exists() || !candidate.isFile()) {
            throw new Avs3Exception(Avs3Error.VENDOR_UNAVAILABLE, "decoder not found");
        }

        boolean matched = candidate.length() == expectedSize
                && expectedSha.equals(sha256(candidate));

        Result result = new Result(candidate.getAbsolutePath(), matched);
        cached = result;
        return result;
    }

    static void clearCache() {
        cached = null;
    }

    private static String sha256(File file) throws Avs3Exception {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            try (FileInputStream fis = new FileInputStream(file)) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = fis.read(buf)) > 0) {
                    md.update(buf, 0, n);
                }
            }
            byte[] digest = md.digest();
            StringBuilder sb = new StringBuilder(64);
            for (byte b : digest) {
                sb.append(String.format("%02x", b & 0xff));
            }
            return sb.toString();
        } catch (NoSuchAlgorithmException | IOException e) {
            throw Avs3Exception.fromSanitizedFailure(
                    Avs3Error.VENDOR_UNAVAILABLE, e.getClass().getSimpleName());
        }
    }
}
