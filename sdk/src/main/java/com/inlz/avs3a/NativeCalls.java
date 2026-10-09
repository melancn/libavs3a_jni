package com.inlz.avs3a;

interface NativeCalls {
    int nContractVersion();
    int nProcessAbi();
    long nCapabilities();
    String nBuildInfo();
    long nCreate(String modelPath, String verifiedVendorPath, long epoch);
    int nQueue(long h, byte[] in, int off, int len, long ptsUs, long epoch, int flags);
    int nReceive(long h, byte[] out, int off, int cap, long[] info);
    int nEnd(long h);
    int nFlush(long h, long epoch);
    void nRelease(long h);

    long nParserCreate(long epoch);
    int nParserQueue(long h, byte[] in, int off, int len, long ptsUs, long epoch, int flags);
    int nParserReceive(long h, byte[] out, int off, int cap, long[] info);
    int nParserEnd(long h);
    int nParserFlush(long h, long epoch);
    void nParserRelease(long h);
}
