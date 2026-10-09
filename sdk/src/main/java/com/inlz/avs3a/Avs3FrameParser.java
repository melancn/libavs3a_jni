package com.inlz.avs3a;

import java.util.Arrays;

public final class Avs3FrameParser implements AutoCloseable {
    private final long handle;
    private long epoch;
    private final NativeCalls calls;
    private final long[] infoScratch = new long[10];
    private volatile boolean closed;

    Avs3FrameParser(long handle, long epoch, NativeCalls calls) {
        this.handle = handle;
        this.epoch = epoch;
        this.calls = calls;
    }

    public synchronized QueueResult queueInput(
            byte[] input, int offset, int length, long ptsUs, long epoch, int flags)
            throws Avs3Exception {
        requireOpen();
        if (input == null && length > 0)
            throw new Avs3Exception(Avs3Error.INVALID_ARGUMENT);
        if (input != null) {
            if (offset < 0 || length < 0 || offset + length > input.length)
                throw new Avs3Exception(Avs3Error.INVALID_ARGUMENT);
        }
        if (epoch != this.epoch)
            throw new Avs3Exception(Avs3Error.STALE_EPOCH);
        if (flags != 0 && flags != Avs3Sdk.INPUT_COMPLETE_SINGLE_FRAME)
            throw new Avs3Exception(Avs3Error.INVALID_ARGUMENT);

        int r = calls.nParserQueue(handle, input, offset, length, ptsUs, epoch, flags);
        if (r < 0) throw Avs3Exception.fromNative(r);
        return QueueResult.fromNative(r);
    }

    public synchronized FrameResult receiveFrame(byte[] output, int offset, int capacity)
            throws Avs3Exception {
        requireOpen();
        if (output == null)
            throw new Avs3Exception(Avs3Error.INVALID_ARGUMENT);
        if (offset < 0 || capacity < 0 || offset + capacity > output.length)
            throw new Avs3Exception(Avs3Error.INVALID_ARGUMENT);

        Arrays.fill(infoScratch, 0L);
        int r = calls.nParserReceive(handle, output, offset, capacity, infoScratch);
        if (r < 0) throw Avs3Exception.fromNative(r);

        switch (r) {
            case 0: return FrameResult.needInput();
            case 1:
                EncodedFrameInfo info = new EncodedFrameInfo(infoScratch);
                return FrameResult.ready(info);
            case 2: return FrameResult.endOfStream();
            case 3:
                int need = checkedInt(infoScratch[4]);
                return FrameResult.outputTooSmall(need);
            default: throw new Avs3Exception(Avs3Error.INTERNAL);
        }
    }

    public synchronized void signalEndOfInput() throws Avs3Exception {
        requireOpen();
        int r = calls.nParserEnd(handle);
        if (r < 0) throw Avs3Exception.fromNative(r);
    }

    public synchronized void flush(long newEpoch) throws Avs3Exception {
        requireOpen();
        if (newEpoch <= epoch)
            throw new Avs3Exception(Avs3Error.STALE_EPOCH);
        int r = calls.nParserFlush(handle, newEpoch);
        if (r < 0) throw Avs3Exception.fromNative(r);
        epoch = newEpoch;
    }

    @Override
    public synchronized void close() {
        if (closed) return;
        closed = true;
        calls.nParserRelease(handle);
    }

    private void requireOpen() throws Avs3Exception {
        if (closed) throw new Avs3Exception(Avs3Error.CLOSED_OR_INVALID_HANDLE);
    }

    private int checkedInt(long v) {
        if (v < 0 || v > Integer.MAX_VALUE) return 0;
        return (int) v;
    }
}
