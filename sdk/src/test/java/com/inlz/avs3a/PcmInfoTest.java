package com.inlz.avs3a;

import org.junit.Test;
import static org.junit.Assert.*;

public class PcmInfoTest {

    @Test
    public void testFieldsFromLongs() {
        long[] info = {12345L, 48000L, 2L, 1024L, 4096L, 2L, 0L, 42L};
        PcmInfo p = new PcmInfo(info);
        assertEquals(12345L, p.ptsUs());
        assertEquals(48000, p.sampleRateHz());
        assertEquals(2, p.channels());
        assertEquals(1024, p.samplesPerChannel());
        assertEquals(4096, p.byteCount());
        assertEquals(PcmLayout.STEREO, p.layout());
        assertEquals(0, p.flags());
        assertEquals(42L, p.epoch());
    }

    @Test
    public void testMonoLayout() {
        long[] info = {0, 48000, 1, 1024, 2048, 1, 0, 1};
        PcmInfo p = new PcmInfo(info);
        assertEquals(PcmLayout.MONO, p.layout());
    }
}
