package com.inlz.avs3a;

import android.content.Context;
import java.io.InputStream;
import java.io.IOException;
import java.io.File;

final class Avs3ModelStore {
    private static final String VENDOR_ID = RuntimeVendorVerifier.VENDOR_ID;

    // SHA-256 of the model embedded in libavs3a_jni.so (vendor model.bin).
    static final String EMBEDDED_MODEL_SHA256 =
            "55d56a17dbfa22da21f2fa945ebe13824d246fe8b2669a63e0440316282ae068";

    static VerifiedModel prepareBundledModel(Context context) throws Avs3Exception {
        Context appContext = context.getApplicationContext();
        if (appContext == null) throw new Avs3Exception(Avs3Error.INVALID_ARGUMENT);
        if (!BuildConfig.BUNDLED_VENDOR) {
            // Bridge has no decoder and therefore no embedded model.
            throw new Avs3Exception(Avs3Error.MODEL_MISSING, "no embedded model in bridge variant");
        }
        // Empty internal path tells nCreate to use the model compiled into the SO.
        return new VerifiedModel(appContext, "", VENDOR_ID, EMBEDDED_MODEL_SHA256);
    }

    static VerifiedModel prepareModel(Context context, ModelSource source) throws Avs3Exception {
        Context appContext = context.getApplicationContext();
        if (appContext == null) throw new Avs3Exception(Avs3Error.INVALID_ARGUMENT);
        File root = appContext.getNoBackupFilesDir();
        try {
            return ModelIo.stageModel(appContext, root, VENDOR_ID, source);
        } catch (IOException e) {
            throw Avs3Exception.fromSanitizedFailure(Avs3Error.MODEL_IO_FAILED, e.getClass().getSimpleName());
        }
    }
}
