package com.inlz.avs3a.demo.media3;

import androidx.media3.common.C;
import androidx.media3.common.Format;
import androidx.media3.decoder.*;
import com.inlz.avs3a.*;
import com.inlz.avs3a.demo.diagnostics.PlaybackDiagnostics;
import java.nio.ByteBuffer;

/** One complete encoded frame -> one PCM block, matching the SDK's synchronous vendor backend. */
public final class Avs3Decoder extends SimpleDecoder<DecoderInputBuffer, SimpleDecoderOutputBuffer, Avs3DecoderException> {
    private final VerifiedModel model;
    private final Format format;
    private final boolean downmix;
    private final PlaybackDiagnostics diagnostics;
    private Avs3Session session;
    private Avs3Mp4SampleAdapter adapter;
    private long epoch = 1;
    private byte[] encoded = new byte[4096];
    private byte[] pcm = new byte[1024 * 12 * 2];
    private volatile int sampleRate;
    private volatile int outputChannels;

    public Avs3Decoder(Format format, VerifiedModel model, boolean downmix, PlaybackDiagnostics diagnostics) {
        super(new DecoderInputBuffer[8], new SimpleDecoderOutputBuffer[8]);
        this.format = format; this.model = model; this.downmix = downmix; this.diagnostics = diagnostics;
        sampleRate = format.sampleRate;
        outputChannels = downmix && format.channelCount == 6 ? 2 : format.channelCount;
        setInitialInputBufferSize(4096);
    }
    @Override public String getName() { return "libavs3a_decoder.so / JNI"; }
    @Override protected DecoderInputBuffer createInputBuffer() {
        return new DecoderInputBuffer(DecoderInputBuffer.BUFFER_REPLACEMENT_MODE_NORMAL);
    }
    @Override protected SimpleDecoderOutputBuffer createOutputBuffer() {
        return new SimpleDecoderOutputBuffer(this::releaseOutputBuffer);
    }
    @Override protected Avs3DecoderException createUnexpectedDecodeException(Throwable error) {
        return new Avs3DecoderException("AVS3 decoder failure", error);
    }
    @Override protected Avs3DecoderException decode(DecoderInputBuffer input, SimpleDecoderOutputBuffer output, boolean reset) {
        try {
            if (session == null) {
                session = Avs3Sdk.open(model, epoch);
                adapter = new Avs3Mp4SampleAdapter(format, epoch);
            } else if (reset) {
                epoch++; session.flush(epoch); adapter.flush(epoch);
            }
            ByteBuffer data = input.data;
            if (data == null || !data.hasRemaining()) throw new Avs3DecoderException("Empty AVS3 sample");
            int length = data.remaining();
            if (length > 4096) throw new Avs3DecoderException("AVS3 sample exceeds SDK admitted frame size");
            data.get(encoded, 0, length);
            adapter.validate(encoded, length, input.timeUs, epoch);
            long start = System.nanoTime();
            if (session.queueInput(encoded, 0, length, Avs3Mp4SampleAdapter.sdkTime(input.timeUs), epoch,
                    Avs3Sdk.INPUT_COMPLETE_SINGLE_FRAME) != QueueResult.ACCEPTED)
                throw new Avs3DecoderException("Unexpected decoder backpressure");
            PcmResult result = session.receivePcm(pcm, 0, pcm.length);
            if (result.kind() == PcmResult.Kind.OUTPUT_TOO_SMALL) {
                int required = result.requiredBytes();
                if (required <= 0 || required > 1024 * 24 * 2) throw new Avs3DecoderException("Invalid PCM capacity");
                pcm = new byte[required]; result = session.receivePcm(pcm, 0, pcm.length);
            }
            if (result.kind() != PcmResult.Kind.READY)
                throw new Avs3DecoderException("Expected synchronous PCM output, got " + result.kind());
            long elapsed = System.nanoTime() - start;
            PcmInfo info = result.info(); sampleRate = info.sampleRateHz();
            boolean mix = downmix && info.layout() == PcmLayout.MC_5_1_0;
            outputChannels = mix ? 2 : info.channels();
            ByteBuffer target = output.init(input.timeUs, mix ? info.byteCount() / 3 : info.byteCount());
            if (mix) StereoDownmix.mix51(pcm, info.byteCount(), target);
            else target.put(pcm, 0, info.byteCount());
            target.flip();
            diagnostics.decoded(info, elapsed, outputChannels);
            return null;
        } catch (Avs3Exception e) {
            return new Avs3DecoderException("SDK " + e.error().name() + " (" + e.error().code() + ")", e);
        } catch (Avs3DecoderException e) { return e; }
    }
    public Format outputFormat() {
        return new Format.Builder().setSampleMimeType("audio/raw").setPcmEncoding(C.ENCODING_PCM_16BIT)
                .setChannelCount(outputChannels).setSampleRate(sampleRate).build();
    }
    @Override public void release() {
        super.release(); // Joins the decode thread before closing native resources.
        if (adapter != null) adapter.close();
        if (session != null) session.close();
    }
}
