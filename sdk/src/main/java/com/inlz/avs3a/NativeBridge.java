package com.inlz.avs3a;

final class NativeBridge {
    static native int nContractVersion();
    static native int nProcessAbi();
    static native long nCapabilities();
    static native String nBuildInfo();
    static native long nCreate(String modelPath, String verifiedVendorPath, long epoch);
    static native int nQueue(long h, byte[] in, int off, int len, long ptsUs, long epoch, int flags);
    static native int nReceive(long h, byte[] out, int off, int cap, long[] info);
    static native int nEnd(long h);
    static native int nFlush(long h, long epoch);
    static native void nRelease(long h);

    static native long nParserCreate(long epoch);
    static native int nParserQueue(long h, byte[] in, int off, int len, long ptsUs, long epoch, int flags);
    static native int nParserReceive(long h, byte[] out, int off, int cap, long[] info);
    static native int nParserEnd(long h);
    static native int nParserFlush(long h, long epoch);
    static native void nParserRelease(long h);
}
