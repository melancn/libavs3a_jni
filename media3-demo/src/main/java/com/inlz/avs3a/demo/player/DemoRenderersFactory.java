package com.inlz.avs3a.demo.player;

import android.content.Context;
import android.os.Handler;
import androidx.media3.exoplayer.*;
import androidx.media3.exoplayer.audio.*;
import androidx.media3.exoplayer.mediacodec.MediaCodecSelector;
import com.inlz.avs3a.VerifiedModel;
import com.inlz.avs3a.demo.media3.Avs3AudioRenderer;
import com.inlz.avs3a.demo.diagnostics.PlaybackDiagnostics;
import java.util.ArrayList;

public final class DemoRenderersFactory extends DefaultRenderersFactory {
    private final VerifiedModel model;
    private final boolean downmix;
    private final PlaybackDiagnostics diagnostics;
    public DemoRenderersFactory(Context context, VerifiedModel model, boolean downmix, PlaybackDiagnostics diagnostics) {
        super(context); this.model = model; this.downmix = downmix; this.diagnostics = diagnostics;
        setEnableDecoderFallback(true);
    }
    @Override protected void buildAudioRenderers(Context context, int extensionMode, MediaCodecSelector selector,
            boolean fallback, AudioSink sink, Handler handler, AudioRendererEventListener listener, ArrayList<Renderer> out) {
        out.add(new Avs3AudioRenderer(handler, listener, sink, model, downmix, diagnostics));
        super.buildAudioRenderers(context, extensionMode, selector, fallback, sink, handler, listener, out);
    }
}
