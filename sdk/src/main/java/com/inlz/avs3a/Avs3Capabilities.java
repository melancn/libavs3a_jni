package com.inlz.avs3a;

public final class Avs3Capabilities {
    private final boolean bridgeLoaded;
    private final int nativeContractVersion;
    private final int processAbi;
    private final boolean parserReady;
    private final boolean vendorPresent;
    private final boolean vendorFingerprintMatched;
    private final boolean vendorAbiVerified;
    private final boolean decodeImplementationReady;

    Avs3Capabilities(boolean bridgeLoaded, int nativeContractVersion, int processAbi,
                     boolean parserReady, boolean vendorPresent,
                     boolean vendorFingerprintMatched, boolean vendorAbiVerified,
                     boolean decodeImplementationReady) {
        this.bridgeLoaded = bridgeLoaded;
        this.nativeContractVersion = nativeContractVersion;
        this.processAbi = processAbi;
        this.parserReady = parserReady;
        this.vendorPresent = vendorPresent;
        this.vendorFingerprintMatched = vendorFingerprintMatched;
        this.vendorAbiVerified = vendorAbiVerified;
        this.decodeImplementationReady = decodeImplementationReady;
    }

    public boolean bridgeLoaded() { return bridgeLoaded; }
    public int nativeContractVersion() { return nativeContractVersion; }
    public int processAbi() { return processAbi; }
    public boolean parserReady() { return parserReady; }
    public boolean vendorPresent() { return vendorPresent; }
    public boolean vendorFingerprintMatched() { return vendorFingerprintMatched; }
    public boolean vendorAbiVerified() { return vendorAbiVerified; }
    public boolean decodeImplementationReady() { return decodeImplementationReady; }

    @Override
    public String toString() {
        return "Avs3Capabilities{bridge=" + bridgeLoaded
            + ", abi=" + processAbi
            + ", parserReady=" + parserReady
            + ", decodeReady=" + decodeImplementationReady + "}";
    }
}
