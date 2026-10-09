package com.inlz.avs3a;

import org.junit.Test;
import static org.junit.Assert.*;

public class PcmResultTest {

    @Test
    public void testNeedInput() {
        PcmResult r = PcmResult.needInput();
        assertEquals(PcmResult.Kind.NEED_INPUT, r.kind());
        try { r.info(); fail("expected IllegalStateException"); } catch (IllegalStateException e) {}
        try { r.requiredBytes(); fail("expected IllegalStateException"); } catch (IllegalStateException e) {}
    }

    @Test
    public void testEndOfStream() {
        PcmResult r = PcmResult.endOfStream();
        assertEquals(PcmResult.Kind.END_OF_STREAM, r.kind());
    }

    @Test
    public void testOutputTooSmall() {
        PcmResult r = PcmResult.outputTooSmall(4096);
        assertEquals(PcmResult.Kind.OUTPUT_TOO_SMALL, r.kind());
        assertEquals(4096, r.requiredBytes());
    }

    @Test
    public void testReady() {
        long[] info = new long[]{1000, 48000, 2, 1024, 4096, 2, 0, 1};
        PcmInfo pcmInfo = new PcmInfo(info);
        PcmResult r = PcmResult.ready(pcmInfo);
        assertEquals(PcmResult.Kind.READY, r.kind());
        assertSame(pcmInfo, r.info());
    }

    @Test(expected = IllegalStateException.class)
    public void testReadyRequiredBytesFails() {
        PcmResult.ready(new PcmInfo(new long[]{0, 48000, 1, 1024, 2048, 1, 0, 1})).requiredBytes();
    }
}
