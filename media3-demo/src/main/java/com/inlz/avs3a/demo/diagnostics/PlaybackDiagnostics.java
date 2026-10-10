package com.inlz.avs3a.demo.diagnostics;

import com.inlz.avs3a.PcmInfo;
import java.util.Locale;

/** Decoder thread writes; UI reads immutable text snapshots at 2 Hz. No per-frame UI callbacks. */
public final class PlaybackDiagnostics {
    private long frames, pcmBytes, samples, totalNs, maxNs, ptsUs;
    private String pcm = "尚未产生 AVS3 PCM";
    public synchronized void decoded(PcmInfo info, long elapsedNs, int outputChannels) {
        frames++; pcmBytes += info.byteCount(); samples += info.samplesPerChannel();
        totalNs += elapsedNs; maxNs = Math.max(maxNs, elapsedNs); ptsUs = info.ptsUs();
        pcm = String.format(Locale.ROOT, "%d Hz / %s / S16 / %d ch → AudioSink %d ch",
                info.sampleRateHz(), info.layout(), info.channels(), outputChannels);
    }
    public synchronized void reset() {
        frames = pcmBytes = samples = totalNs = maxNs = ptsUs = 0;
        pcm = "尚未产生 AVS3 PCM";
    }
    public synchronized String snapshot() {
        return pcm + String.format(Locale.ROOT,
                "\n解码帧 %d | 每声道累计样本 %d | PCM %d bytes\n最近 PCM PTS %d µs（非播放位置）\n解码调用平均 %.3f ms / 最大 %.3f ms",
                frames, samples, pcmBytes, ptsUs,
                frames == 0 ? 0 : totalNs / 1e6 / frames, maxNs / 1e6);
    }
}
