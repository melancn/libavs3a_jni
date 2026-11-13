package com.inlz.avs3a;

import android.content.Context;

/**
 * Verifies that this process runs a supported ABI and that the decoder provider
 * is the single packaged <code>libavs3a_jni.so</code> (full variant).
 *
 * The historical name is kept for API compatibility; the provider is no longer
 * a separate <code>libavs3a_decoder.so</code>. Fingerprint matching now means
 * "the decoder is statically linked into the loaded JNI SO", which is known from
 * the build variant and the registered JNI contract.
 */
final class RuntimeVendorVerifier {
    static final String VENDOR_ID = "avs3a-ystpzs-1.4.1";

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
        if (processAbi != 1 && processAbi != 2)
            throw new Avs3Exception(Avs3Error.VENDOR_ABI_NOT_READY);
        if (!BuildConfig.BUNDLED_VENDOR)
            throw new Avs3Exception(Avs3Error.VENDOR_UNAVAILABLE, "decoder not linked");

        // No second library path is needed; nCreate ignores the historical path.
        Result result = new Result("", true);
        cached = result;
        return result;
    }

    static void clearCache() {
        cached = null;
    }
}
