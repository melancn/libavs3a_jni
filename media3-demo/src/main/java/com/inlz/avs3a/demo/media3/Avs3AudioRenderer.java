package com.inlz.avs3a.demo.media3;

import android.os.Handler;
import androidx.media3.common.*;
import androidx.media3.decoder.CryptoConfig;
import androidx.media3.exoplayer.audio.*;
import com.inlz.avs3a.VerifiedModel;
import com.inlz.avs3a.demo.diagnostics.PlaybackDiagnostics;

public final class Avs3AudioRenderer extends DecoderAudioRenderer<Avs3Decoder> {
    private final VerifiedModel model;
    private final boolean downmix;
    private final PlaybackDiagnostics diagnostics;
    public Avs3AudioRenderer(Handler handler, AudioRendererEventListener listener, AudioSink sink,
            VerifiedModel model, boolean downmix, PlaybackDiagnostics diagnostics) {
        super(handler, listener, sink);
        this.model = model; this.downmix = downmix; this.diagnostics = diagnostics;
    }
    @Override public String getName() { return "Avs3AudioRenderer"; }
    @Override protected int supportsFormatInternal(Format format) {
        if (!Avs3Format.MIME_TYPE.equals(format.sampleMimeType)) return C.FORMAT_UNSUPPORTED_TYPE;
        if (model == null) return C.FORMAT_UNSUPPORTED_SUBTYPE;
        if (format.drmInitData != null) return C.FORMAT_UNSUPPORTED_DRM;
        // The first demo validates mono/stereo/5.1, not arbitrary speaker/height mappings.
        if (format.sampleRate <= 0 || (format.channelCount != 1 && format.channelCount != 2 && format.channelCount != 6))
            return C.FORMAT_UNSUPPORTED_SUBTYPE;
        int channels = downmix && format.channelCount == 6 ? 2 : format.channelCount;
        Format pcm = new Format.Builder().setSampleMimeType(MimeTypes.AUDIO_RAW).setSampleRate(format.sampleRate)
                .setChannelCount(channels).setPcmEncoding(C.ENCODING_PCM_16BIT).build();
        return sinkSupportsFormat(pcm) ? C.FORMAT_HANDLED : C.FORMAT_UNSUPPORTED_SUBTYPE;
    }
    @Override protected Avs3Decoder createDecoder(Format format, CryptoConfig crypto) throws Avs3DecoderException {
        if (model == null || crypto != null) throw new Avs3DecoderException("Verified vendor/model required; DRM unsupported");
        return new Avs3Decoder(format, model, downmix, diagnostics);
    }
    @Override protected Format getOutputFormat(Avs3Decoder decoder) { return decoder.outputFormat(); }
}
