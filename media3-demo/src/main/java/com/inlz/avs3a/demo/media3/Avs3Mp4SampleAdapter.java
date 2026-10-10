package com.inlz.avs3a.demo.media3;

import androidx.media3.common.C;
import androidx.media3.common.Format;
import com.inlz.avs3a.*;

/** Strict, full-header single-frame MP4 sample adapter. No guessed dca3-to-header reconstruction. */
public final class Avs3Mp4SampleAdapter implements AutoCloseable {
    private final Avs3FrameParser parser;
    private final Format format;
    private final byte[] scratch = new byte[4096];
    public Avs3Mp4SampleAdapter(Format format, long epoch) throws Avs3Exception {
        this.format = format;
        parser = Avs3Sdk.newFrameParser(epoch);
    }
    public EncodedFrameInfo validate(byte[] sample, int length, long timeUs, long epoch)
            throws Avs3Exception, Avs3DecoderException {
        if (parser.queueInput(sample, 0, length, sdkTime(timeUs), epoch,
                Avs3Sdk.INPUT_COMPLETE_SINGLE_FRAME) != QueueResult.ACCEPTED)
            throw new Avs3DecoderException("Unexpected parser backpressure");
        FrameResult result = parser.receiveFrame(scratch, 0, scratch.length);
        if (result.kind() != FrameResult.Kind.READY)
            throw new Avs3DecoderException("MP4 sample must contain one supported complete AVS3 frame: " + result.kind());
        EncodedFrameInfo info = result.info();
        if ((format.channelCount != Format.NO_VALUE && format.channelCount != info.channels())
                || (format.sampleRate != Format.NO_VALUE && format.sampleRate != info.sampleRateHz()))
            throw new Avs3DecoderException("MP4 sample entry disagrees with AVS3 frame header");
        return info;
    }
    public void flush(long epoch) throws Avs3Exception { parser.flush(epoch); }
    public static long sdkTime(long value) { return value == C.TIME_UNSET ? Avs3Sdk.TIME_UNSET : value; }
    @Override public void close() { parser.close(); }
}
