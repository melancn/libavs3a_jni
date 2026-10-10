package com.inlz.avs3a;

/**
 * PCM speaker layouts reported by the SDK. Ids are contractual
 * (protocol/jni-contract.json "pcmLayouts") and equal channelConfig + 1.
 *
 * MC_10_2 and MC_22_2 are representable vendor layouts, but this vendor build
 * has no bitrate table for them (NULL codecBitrateConfigTable slot), so the
 * SDK never produces PCM in those layouts; they are rejected as
 * UNSUPPORTED_MODE before decoding.
 */
public enum PcmLayout {
    UNKNOWN(0, 0),
    MONO(1, 1),
    STEREO(2, 2),
    MC_5_1_0(3, 6),
    MC_7_1_0(4, 8),
    MC_10_2(5, 12),
    MC_22_2(6, 24),
    MC_4_0(7, 4),
    MC_5_1_2(8, 8),
    MC_5_1_4(9, 10),
    MC_7_1_2(10, 10),
    MC_7_1_4(11, 12);

    private final int id;
    private final int channels;

    PcmLayout(int id, int channels) {
        this.id = id;
        this.channels = channels;
    }

    public int id() {
        return id;
    }

    /** Speaker count this layout carries; 0 for UNKNOWN. */
    public int channels() {
        return channels;
    }

    static PcmLayout fromId(int id) {
        for (PcmLayout l : values()) {
            if (l.id == id) return l;
        }
        return UNKNOWN;
    }
}
