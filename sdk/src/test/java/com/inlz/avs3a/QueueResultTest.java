package com.inlz.avs3a;

import org.junit.Test;
import static org.junit.Assert.*;

public class QueueResultTest {

    @Test
    public void testFromNative() {
        assertEquals(QueueResult.ACCEPTED, QueueResult.fromNative(0));
        assertEquals(QueueResult.BACKPRESSURE, QueueResult.fromNative(1));
        assertEquals(QueueResult.BACKPRESSURE, QueueResult.fromNative(99));
    }
}
