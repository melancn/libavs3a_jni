package com.inlz.avs3a;

import org.junit.Test;
import static org.junit.Assert.*;

public class EncodedFrameInfoTest {

    @Test
    public void testFieldsFromLongs() {
        long[] info = {1000L, 48000L, 2L, 1024L, 100L, 7L, 93L, 128000L, 2L, 42L};
        EncodedFrameInfo f = new EncodedFrameInfo(info);
        assertEquals(1000L, f.ptsUs());
        assertEquals(48000, f.sampleRateHz());
        assertEquals(2, f.channels());
        assertEquals(1024, f.samplesPerChannel());
        assertEquals(100, f.frameBytes());
        assertEquals(7, f.payloadOffset());
        assertEquals(93, f.payloadBytes());
        assertEquals(128000, f.bitrateBps());
        assertEquals(2, f.channelMode());
        assertEquals(42L, f.epoch());
    }
}
