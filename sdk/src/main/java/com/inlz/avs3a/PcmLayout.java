package com.inlz.avs3a;

public enum PcmLayout {
    UNKNOWN(0),
    MONO(1),
    STEREO(2);

    private final int id;

    PcmLayout(int id) {
        this.id = id;
    }

    public int id() {
        return id;
    }

    static PcmLayout fromId(int id) {
        for (PcmLayout l : values()) {
            if (l.id == id) return l;
        }
        return UNKNOWN;
    }
}
