package com.inlz.avs3a;

public final class PcmResult {
    public enum Kind { NEED_INPUT, READY, END_OF_STREAM, OUTPUT_TOO_SMALL }

    private final Kind kind;
    private final PcmInfo info;
    private final int requiredBytes;

    private PcmResult(Kind kind, PcmInfo info, int requiredBytes) {
        this.kind = kind;
        this.info = info;
        this.requiredBytes = requiredBytes;
    }

    static PcmResult needInput() { return new PcmResult(Kind.NEED_INPUT, null, 0); }
    static PcmResult ready(PcmInfo info) { return new PcmResult(Kind.READY, info, 0); }
    static PcmResult endOfStream() { return new PcmResult(Kind.END_OF_STREAM, null, 0); }
    static PcmResult outputTooSmall(int requiredBytes) {
        return new PcmResult(Kind.OUTPUT_TOO_SMALL, null, requiredBytes);
    }

    public Kind kind() { return kind; }
    public PcmInfo info() {
        if (kind != Kind.READY) throw new IllegalStateException("no info for kind " + kind);
        return info;
    }
    public int requiredBytes() {
        if (kind != Kind.OUTPUT_TOO_SMALL) throw new IllegalStateException("no requiredBytes for kind " + kind);
        return requiredBytes;
    }
}
