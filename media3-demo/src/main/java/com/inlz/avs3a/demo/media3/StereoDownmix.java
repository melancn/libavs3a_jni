package com.inlz.avs3a.demo.media3;

import java.nio.ByteBuffer;

/** Demo 5.1 matrix: L R C LFE Ls Rs -> L R, -3dB center/surround; LFE omitted.
 * Gain normalization prevents clipping. This speaker convention needs device/reference validation.
 * This is not an Audio Vivid binaural/spatial renderer.
 */
public final class StereoDownmix {
    private static final double SURROUND = Math.sqrt(0.5);
    private static final double SCALE = 1.0 / (1.0 + 2.0 * SURROUND);
    private StereoDownmix() {}
    public static void mix51(byte[] pcm, int bytes, ByteBuffer output) {
        if (bytes < 0 || bytes > pcm.length || bytes % 12 != 0 || output.remaining() < bytes / 3)
            throw new IllegalArgumentException("Invalid interleaved S16 5.1 buffer");
        for (int p = 0; p < bytes; p += 12) {
            double center = s16(pcm, p + 4) * SURROUND;
            put(output, (s16(pcm, p) + center + s16(pcm, p + 8) * SURROUND) * SCALE);
            put(output, (s16(pcm, p + 2) + center + s16(pcm, p + 10) * SURROUND) * SCALE);
        }
    }
    private static int s16(byte[] b, int p) { return (short)((b[p] & 255) | (b[p + 1] << 8)); }
    private static void put(ByteBuffer output, double value) {
        int sample = Math.max(-32768, Math.min(32767, (int)Math.round(value)));
        output.put((byte)sample).put((byte)(sample >> 8));
    }
}
