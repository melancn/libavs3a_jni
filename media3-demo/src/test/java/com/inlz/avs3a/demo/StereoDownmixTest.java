package com.inlz.avs3a.demo;

import com.inlz.avs3a.demo.media3.StereoDownmix;
import org.junit.Test;
import java.nio.*;
import static org.junit.Assert.*;

public class StereoDownmixTest {
    private static short[] mix(short... channels) {
        ByteBuffer input = ByteBuffer.allocate(12).order(ByteOrder.LITTLE_ENDIAN);
        for (short v : channels) input.putShort(v);
        ByteBuffer out = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN);
        StereoDownmix.mix51(input.array(), 12, out);
        out.flip(); return new short[]{out.getShort(), out.getShort()};
    }
    @Test public void frontLeftDoesNotLeakIntoRight() {
        short[] out = mix((short)20000,(short)0,(short)0,(short)0,(short)0,(short)0);
        assertTrue(out[0] > 0); assertEquals(0, out[1]);
    }
    @Test public void centerIsEqualAndLfeIsOmitted() {
        short[] center = mix((short)0,(short)0,(short)20000,(short)0,(short)0,(short)0);
        assertTrue(center[0] > 0); assertEquals(center[0],center[1]);
        assertArrayEquals(new short[]{0,0},mix((short)0,(short)0,(short)0,(short)32767,(short)0,(short)0));
    }
    @Test public void fullScaleDoesNotWrapOrClip() {
        short[] out = mix((short)32767,(short)32767,(short)32767,(short)32767,(short)32767,(short)32767);
        assertEquals(32767, out[0]); assertEquals(32767, out[1]);
    }
    @Test(expected=IllegalArgumentException.class) public void rejectsPartialPcmFrame() {
        StereoDownmix.mix51(new byte[13],13,ByteBuffer.allocate(32));
    }
}
