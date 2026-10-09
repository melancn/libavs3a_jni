package com.inlz.avs3a;

import android.content.Context;
import java.io.File;

final class RuntimeVendorVerifier {
    private static volatile String cachedPath;
    private static volatile String cachedVendorId;
    private static volatile boolean cached;

    static String verifyApplicationVendor(Context appContext, int processAbi) throws Avs3Exception {
        String abiDir = (processAbi == 1) ? "arm64-v8a" : "armeabi-v7a";

        if (cached && cachedVendorId != null) {
            return cachedPath;
        }

        String nativeLibDir = appContext.getApplicationInfo().nativeLibraryDir;
        File candidate = new File(nativeLibDir, "libavs3a_decoder.so");

        if (!candidate.exists() || !candidate.isFile()) {
            throw new Avs3Exception(Avs3Error.VENDOR_UNAVAILABLE, "decoder not found");
        }

        cachedPath = candidate.getAbsolutePath();
        cachedVendorId = "avs3a-ystpzs-1.4.1";
        cached = true;
        return cachedPath;
    }

    static void clearCache() {
        cached = false;
        cachedPath = null;
        cachedVendorId = null;
    }
}
