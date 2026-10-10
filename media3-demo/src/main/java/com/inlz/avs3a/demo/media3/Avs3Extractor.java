package com.inlz.avs3a.demo.media3;

import androidx.media3.common.*;
import androidx.media3.common.util.ParsableByteArray;
import androidx.media3.extractor.*;
import com.inlz.avs3a.*;
import java.io.IOException;

/** Streaming elementary AVS3 input. No invented duration or byte-to-time seek approximation. */
public final class Avs3Extractor implements Extractor {
    private final byte[] inputBytes = new byte[16384];
    private final byte[] frameBytes = new byte[4096];
    private Avs3FrameParser parser;
    private TrackOutput track;
    private ExtractorOutput output;
    private boolean ended, formatSent, firstInput = true;
    private long epoch = 1, startTimeUs;
    @Override public boolean sniff(ExtractorInput input) throws IOException {
        byte[] prefix = new byte[7];
        try {
            return input.peekFully(prefix, 0, 7, true) && (prefix[0] & 255) == 255 && (prefix[1] & 255) == 242;
        } catch (java.io.EOFException e) { return false; }
        finally { input.resetPeekPosition(); }
    }
    @Override public void init(ExtractorOutput output) {
        this.output = output; track = output.track(0, C.TRACK_TYPE_AUDIO);
        output.seekMap(new SeekMap.Unseekable(C.TIME_UNSET));
    }
    @Override public int read(ExtractorInput input, PositionHolder seek) throws IOException {
        try {
            if (parser == null) parser = Avs3Sdk.newFrameParser(epoch);
            FrameResult result = parser.receiveFrame(frameBytes, 0, frameBytes.length);
            if (result.kind() == FrameResult.Kind.READY) {
                EncodedFrameInfo info = result.info();
                if (!formatSent) {
                    track.format(new Format.Builder().setSampleMimeType(Avs3Format.MIME_TYPE).setCodecs("av3a")
                            .setSampleRate(info.sampleRateHz()).setChannelCount(info.channels())
                            .setAverageBitrate(info.bitrateBps()).setMaxInputSize(4096).build());
                    output.endTracks(); formatSent = true;
                }
                track.sampleData(new ParsableByteArray(frameBytes, info.frameBytes()), info.frameBytes());
                track.sampleMetadata(info.ptsUs(), C.BUFFER_FLAG_KEY_FRAME, info.frameBytes(), 0, null);
                return RESULT_CONTINUE;
            }
            if (result.kind() == FrameResult.Kind.END_OF_STREAM) return RESULT_END_OF_INPUT;
            if (result.kind() != FrameResult.Kind.NEED_INPUT || ended) throw new IOException("Unexpected AVS3 parser state: " + result.kind());
            int count = input.read(inputBytes, 0, inputBytes.length);
            if (count == C.RESULT_END_OF_INPUT) { ended = true; parser.signalEndOfInput(); }
            else if (count > 0) {
                if (parser.queueInput(inputBytes, 0, count, firstInput ? startTimeUs : Avs3Sdk.TIME_UNSET, epoch, 0) != QueueResult.ACCEPTED)
                    throw new IOException("AVS3 parser backpressure without output");
                firstInput = false;
            }
            return RESULT_CONTINUE;
        } catch (Avs3Exception e) { throw new IOException("AVS3 " + e.error(), e); }
    }
    @Override public void seek(long position, long timeUs) {
        if (parser != null) { parser.close(); parser = null; }
        epoch++; ended = false; firstInput = true; startTimeUs = timeUs;
    }
    @Override public void release() { if (parser != null) { parser.close(); parser = null; } }
}
