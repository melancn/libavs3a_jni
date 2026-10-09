package com.inlz.avs3a;

public final class FrameResult {
    public enum Kind { NEED_INPUT, READY, END_OF_STREAM, OUTPUT_TOO_SMALL }

    private final Kind kind;
    private final EncodedFrameInfo info;
    private final int requiredBytes;

    private FrameResult(Kind kind, EncodedFrameInfo info, int requiredBytes) {
        this.kind = kind;
        this.info = info;
        this.requiredBytes = requiredBytes;
    }

    static FrameResult needInput() { return new FrameResult(Kind.NEED_INPUT, null, 0); }
    static FrameResult ready(EncodedFrameInfo info) { return new FrameResult(Kind.READY, info, 0); }
    static FrameResult endOfStream() { return new FrameResult(Kind.END_OF_STREAM, null, 0); }
    static FrameResult outputTooSmall(int requiredBytes) {
        return new FrameResult(Kind.OUTPUT_TOO_SMALL, null, requiredBytes);
    }

    public Kind kind() { return kind; }
    public EncodedFrameInfo info() {
        if (kind != Kind.READY) throw new IllegalStateException("no info for kind " + kind);
        return info;
    }
    public int requiredBytes() {
        if (kind != Kind.OUTPUT_TOO_SMALL) throw new IllegalStateException("no requiredBytes for kind " + kind);
        return requiredBytes;
    }
}
