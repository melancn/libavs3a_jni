package com.inlz.avs3a;

import android.content.Context;

public final class Avs3Sdk {
    public static final int API_CONTRACT_VERSION = 1;
    public static final long TIME_UNSET = Long.MIN_VALUE;
    public static final int INPUT_COMPLETE_SINGLE_FRAME = 1;

    private Avs3Sdk() {}

    public static Avs3Capabilities capabilities(Context context) throws Avs3Exception {
        BridgeLoader.ensureLoaded();
        int contract = NativeBridge.nContractVersion();
        if (contract != API_CONTRACT_VERSION) {
            throw new Avs3Exception(Avs3Error.NATIVE_CONTRACT_MISMATCH);
        }
        int abi = NativeBridge.nProcessAbi();
        boolean bridgeLoaded = BridgeLoader.isLoaded();

        boolean vendorPresent = false;
        boolean vendorFingerprintMatched = false;
        boolean vendorAbiVerified = (abi == 1 || abi == 2);
        boolean decodeReady = false;

        try {
            RuntimeVendorVerifier.verifyApplicationVendor(context, abi);
            vendorPresent = true;
            vendorFingerprintMatched = true;
            decodeReady = vendorAbiVerified;
        } catch (Avs3Exception e) {
            // vendor not present or not verified
        }

        return new Avs3Capabilities(
            bridgeLoaded, contract, abi, true,
            vendorPresent, vendorFingerprintMatched,
            vendorAbiVerified, decodeReady);
    }

    public static VerifiedModel prepareBundledModel(Context context) throws Avs3Exception {
        return Avs3ModelStore.prepareBundledModel(context);
    }

    public static VerifiedModel prepareModel(Context context, ModelSource source) throws Avs3Exception {
        return Avs3ModelStore.prepareModel(context, source);
    }

    public static Avs3Session open(VerifiedModel model, long epoch) throws Avs3Exception {
        if (model == null) throw new Avs3Exception(Avs3Error.INVALID_ARGUMENT);
        if (epoch < 0) throw new Avs3Exception(Avs3Error.INVALID_ARGUMENT);

        BridgeLoader.ensureLoaded();
        int abi = NativeBridge.nProcessAbi();
        String vendorPath = RuntimeVendorVerifier.verifyApplicationVendor(model.appContext(), abi);

        NativeCalls calls = JniCalls.instance();
        long result = calls.nCreate(model.internalPath(), vendorPath, epoch);
        if (result <= 0) throw Avs3Exception.fromNative((int) result);

        return new Avs3Session(result, epoch, calls);
    }

    public static Avs3FrameParser newFrameParser(long epoch) throws Avs3Exception {
        if (epoch < 0) throw new Avs3Exception(Avs3Error.INVALID_ARGUMENT);
        BridgeLoader.ensureLoaded();
        NativeCalls calls = JniCalls.instance();
        long result = calls.nParserCreate(epoch);
        if (result <= 0) throw Avs3Exception.fromNative((int) result);
        return new Avs3FrameParser(result, epoch, calls);
    }
}
