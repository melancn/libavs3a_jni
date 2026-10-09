package com.inlz.avs3a;

import android.content.Context;
import android.content.res.AssetManager;
import java.io.InputStream;
import java.io.IOException;
import java.io.File;

final class Avs3ModelStore {
    private static final String VENDOR_ID = "avs3a-ystpzs-1.4.1";

    static VerifiedModel prepareBundledModel(Context context) throws Avs3Exception {
        return prepareModel(context, new ModelSource() {
            @Override
            public InputStream open() throws IOException {
                AssetManager am = context.getApplicationContext().getAssets();
                return am.open("avs3a/model.bin");
            }
        });
    }

    static VerifiedModel prepareModel(Context context, ModelSource source) throws Avs3Exception {
        Context appContext = context.getApplicationContext();
        File root = appContext.getNoBackupFilesDir();
        try {
            return ModelIo.stageModel(root, VENDOR_ID, source);
        } catch (IOException e) {
            throw Avs3Exception.fromSanitizedFailure(Avs3Error.MODEL_IO_FAILED, e.getClass().getSimpleName());
        }
    }
}
