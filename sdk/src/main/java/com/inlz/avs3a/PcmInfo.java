package com.inlz.avs3a;

public final class PcmInfo {
    private final long ptsUs;
    private final int sampleRateHz;
    private final int channels;
    private final int samplesPerChannel;
    private final int byteCount;
    private final PcmLayout layout;
    private final int flags;
    private final long epoch;

    PcmInfo(long[] info) {
        this.ptsUs = info[0];
        this.sampleRateHz = (int) info[1];
        this.channels = (int) info[2];
        this.samplesPerChannel = (int) info[3];
        this.byteCount = (int) info[4];
        this.layout = PcmLayout.fromId((int) info[5]);
        this.flags = (int) info[6];
        this.epoch = info[7];
    }

    public long ptsUs() { return ptsUs; }
    public int sampleRateHz() { return sampleRateHz; }
    public int channels() { return channels; }
    public int samplesPerChannel() { return samplesPerChannel; }
    public int byteCount() { return byteCount; }
    public PcmLayout layout() { return layout; }
    public int flags() { return flags; }
    public long epoch() { return epoch; }

    @Override
    public String toString() {
        return "PcmInfo{rate=" + sampleRateHz + ", ch=" + channels
            + ", samples=" + samplesPerChannel + ", bytes=" + byteCount + "}";
    }
}
