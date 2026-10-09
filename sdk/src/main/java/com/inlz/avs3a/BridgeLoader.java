package com.inlz.avs3a;

final class BridgeLoader {
    private static boolean loaded;

    static synchronized void ensureLoaded() throws Avs3Exception {
        if (loaded) return;
        try {
            System.loadLibrary("avs3a_jni");
            if (NativeBridge.nContractVersion() != 1)
                throw new Avs3Exception(Avs3Error.NATIVE_CONTRACT_MISMATCH);
            loaded = true;
        } catch (UnsatisfiedLinkError e) {
            throw Avs3Exception.fromSanitizedFailure(
                Avs3Error.BRIDGE_UNAVAILABLE, e.getClass().getSimpleName());
        }
    }

    static boolean isLoaded() {
        return loaded;
    }
}
