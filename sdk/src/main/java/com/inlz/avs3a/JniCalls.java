package com.inlz.avs3a;

final class JniCalls implements NativeCalls {
    private static final JniCalls INSTANCE = new JniCalls();

    static JniCalls instance() {
        return INSTANCE;
    }

    private JniCalls() {}

    @Override public int nContractVersion() { return NativeBridge.nContractVersion(); }
    @Override public int nProcessAbi() { return NativeBridge.nProcessAbi(); }
    @Override public long nCapabilities() { return NativeBridge.nCapabilities(); }
    @Override public String nBuildInfo() { return NativeBridge.nBuildInfo(); }

    @Override
    public long nCreate(String modelPath, String verifiedVendorPath, long epoch) {
        return NativeBridge.nCreate(modelPath, verifiedVendorPath, epoch);
    }

    @Override
    public int nQueue(long h, byte[] in, int off, int len, long ptsUs, long epoch, int flags) {
        return NativeBridge.nQueue(h, in, off, len, ptsUs, epoch, flags);
    }

    @Override
    public int nReceive(long h, byte[] out, int off, int cap, long[] info) {
        return NativeBridge.nReceive(h, out, off, cap, info);
    }

    @Override public int nEnd(long h) { return NativeBridge.nEnd(h); }
    @Override public int nFlush(long h, long epoch) { return NativeBridge.nFlush(h, epoch); }
    @Override public void nRelease(long h) { NativeBridge.nRelease(h); }

    @Override public long nParserCreate(long epoch) { return NativeBridge.nParserCreate(epoch); }

    @Override
    public int nParserQueue(long h, byte[] in, int off, int len, long ptsUs, long epoch, int flags) {
        return NativeBridge.nParserQueue(h, in, off, len, ptsUs, epoch, flags);
    }

    @Override
    public int nParserReceive(long h, byte[] out, int off, int cap, long[] info) {
        return NativeBridge.nParserReceive(h, out, off, cap, info);
    }

    @Override public int nParserEnd(long h) { return NativeBridge.nParserEnd(h); }
    @Override public int nParserFlush(long h, long epoch) { return NativeBridge.nParserFlush(h, epoch); }
    @Override public void nParserRelease(long h) { NativeBridge.nParserRelease(h); }
}
