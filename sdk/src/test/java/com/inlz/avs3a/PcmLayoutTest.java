package com.inlz.avs3a;

import org.junit.Test;
import static org.junit.Assert.*;

public class PcmLayoutTest {

    @Test
    public void testIdsAreContractual() {
        assertEquals(0, PcmLayout.UNKNOWN.id());
        assertEquals(1, PcmLayout.MONO.id());
        assertEquals(2, PcmLayout.STEREO.id());
        assertEquals(3, PcmLayout.MC_5_1_0.id());
        assertEquals(4, PcmLayout.MC_7_1_0.id());
        assertEquals(5, PcmLayout.MC_10_2.id());
        assertEquals(6, PcmLayout.MC_22_2.id());
        assertEquals(7, PcmLayout.MC_4_0.id());
        assertEquals(8, PcmLayout.MC_5_1_2.id());
        assertEquals(9, PcmLayout.MC_5_1_4.id());
        assertEquals(10, PcmLayout.MC_7_1_2.id());
        assertEquals(11, PcmLayout.MC_7_1_4.id());
    }

    @Test
    public void testChannelsIndependentOfLayoutName() {
        // Equal channel counts, distinct layouts: the whole point of layout ids.
        assertEquals(8, PcmLayout.MC_7_1_0.channels());
        assertEquals(8, PcmLayout.MC_5_1_2.channels());
        assertNotSame(PcmLayout.MC_7_1_0, PcmLayout.MC_5_1_2);
        assertEquals(10, PcmLayout.MC_5_1_4.channels());
        assertEquals(10, PcmLayout.MC_7_1_2.channels());
        assertNotSame(PcmLayout.MC_5_1_4, PcmLayout.MC_7_1_2);
        assertEquals(12, PcmLayout.MC_7_1_4.channels());
        assertEquals(6, PcmLayout.MC_5_1_0.channels());
        assertEquals(4, PcmLayout.MC_4_0.channels());
        assertEquals(1, PcmLayout.MONO.channels());
        assertEquals(2, PcmLayout.STEREO.channels());
    }

    @Test
    public void testFromId() {
        assertSame(PcmLayout.MONO, PcmLayout.fromId(1));
        assertSame(PcmLayout.STEREO, PcmLayout.fromId(2));
        assertSame(PcmLayout.MC_5_1_0, PcmLayout.fromId(3));
        assertSame(PcmLayout.MC_7_1_4, PcmLayout.fromId(11));
        assertSame(PcmLayout.UNKNOWN, PcmLayout.fromId(0));
        assertSame(PcmLayout.UNKNOWN, PcmLayout.fromId(99));
        assertSame(PcmLayout.UNKNOWN, PcmLayout.fromId(-1));
    }
}
