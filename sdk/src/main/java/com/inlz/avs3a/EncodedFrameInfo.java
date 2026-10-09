package com.inlz.avs3a;

public final class EncodedFrameInfo {
    private final long ptsUs;
    private final int sampleRateHz;
    private final int channels;
    private final int samplesPerChannel;
    private final int frameBytes;
    private final int payloadOffset;
    private final int payloadBytes;
    private final int bitrateBps;
    private final int channelMode;
    private final long epoch;

    EncodedFrameInfo(long[] info) {
        this.ptsUs = info[0];
        this.sampleRateHz = (int) info[1];
        this.channels = (int) info[2];
        this.samplesPerChannel = (int) info[3];
        this.frameBytes = (int) info[4];
        this.payloadOffset = (int) info[5];
        this.payloadBytes = (int) info[6];
        this.bitrateBps = (int) info[7];
        this.channelMode = (int) info[8];
        this.epoch = info[9];
    }

    public long ptsUs() { return ptsUs; }
    public int sampleRateHz() { return sampleRateHz; }
    public int channels() { return channels; }
    public int samplesPerChannel() { return samplesPerChannel; }
    public int frameBytes() { return frameBytes; }
    public int payloadOffset() { return payloadOffset; }
    public int payloadBytes() { return payloadBytes; }
    public int bitrateBps() { return bitrateBps; }
    public int channelMode() { return channelMode; }
    public long epoch() { return epoch; }

    @Override
    public String toString() {
        return "EncodedFrameInfo{rate=" + sampleRateHz + ", ch=" + channels
            + ", frameBytes=" + frameBytes + ", payloadBytes=" + payloadBytes + "}";
    }
}
