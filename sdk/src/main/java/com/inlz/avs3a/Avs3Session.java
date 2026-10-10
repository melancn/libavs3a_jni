package com.inlz.avs3a;

import java.util.Arrays;

public final class Avs3Session implements AutoCloseable {
    private final long handle;
    private long epoch;
    private final NativeCalls calls;
    private final long[] infoScratch = new long[8];
    private volatile boolean closed;

    Avs3Session(long handle, long epoch, NativeCalls calls) {
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

        int r = calls.nQueue(handle, input, offset, length, ptsUs, epoch, flags);
        if (r < 0) throw Avs3Exception.fromNative(r);
        return QueueResult.fromNative(r);
    }

    public synchronized PcmResult receivePcm(byte[] output, int offset, int capacity)
            throws Avs3Exception {
        requireOpen();
        if (output == null)
            throw new Avs3Exception(Avs3Error.INVALID_ARGUMENT);
        if (offset < 0 || capacity < 0 || offset + capacity > output.length)
            throw new Avs3Exception(Avs3Error.INVALID_ARGUMENT);

        Arrays.fill(infoScratch, 0L);
        int r = calls.nReceive(handle, output, offset, capacity, infoScratch);
        if (r < 0) throw Avs3Exception.fromNative(r);

        switch (r) {
            case 0: return PcmResult.needInput();
            case 1:
                PcmInfo info = validateAndCopyInfo(infoScratch);
                return PcmResult.ready(info);
            case 2: return PcmResult.endOfStream();
            case 3:
                int need = checkedPcmCapacity(infoScratch[4]);
                return PcmResult.outputTooSmall(need);
            default: throw new Avs3Exception(Avs3Error.INTERNAL);
        }
    }

    public synchronized void signalEndOfInput() throws Avs3Exception {
        requireOpen();
        int r = calls.nEnd(handle);
        if (r < 0) throw Avs3Exception.fromNative(r);
    }

    public synchronized void flush(long newEpoch) throws Avs3Exception {
        requireOpen();
        if (newEpoch <= epoch)
            throw new Avs3Exception(Avs3Error.STALE_EPOCH);
        int r = calls.nFlush(handle, newEpoch);
        if (r < 0) throw Avs3Exception.fromNative(r);
        epoch = newEpoch;
    }

    @Override
    public synchronized void close() {
        if (closed) return;
        closed = true;
        calls.nRelease(handle);
    }

    private void requireOpen() throws Avs3Exception {
        if (closed) throw new Avs3Exception(Avs3Error.CLOSED_OR_INVALID_HANDLE);
    }

    private PcmInfo validateAndCopyInfo(long[] info) throws Avs3Exception {
        if (info[1] <= 0)
            throw new Avs3Exception(Avs3Error.INTERNAL, "invalid sample rate");
        int channels = (int) info[2];
        if (channels <= 0)
            throw new Avs3Exception(Avs3Error.INTERNAL, "invalid channel count");
        PcmLayout layout = PcmLayout.fromId((int) info[5]);
        if (layout == PcmLayout.UNKNOWN)
            throw new Avs3Exception(Avs3Error.INTERNAL, "unknown pcm layout");
        if (layout.channels() != channels)
            throw new Avs3Exception(Avs3Error.INTERNAL, "layout channel mismatch");
        int samples = (int) info[3];
        if (samples <= 0)
            throw new Avs3Exception(Avs3Error.INTERNAL, "invalid samples");
        int bytes = (int) info[4];
        int expected = samples * channels * 2;
        if (bytes != expected)
            throw new Avs3Exception(Avs3Error.INTERNAL, "byte count mismatch");
        return new PcmInfo(info);
    }

    private int checkedPcmCapacity(long rawBytes) {
        if (rawBytes < 0 || rawBytes > Integer.MAX_VALUE)
            return 0;
        return (int) rawBytes;
    }
}
