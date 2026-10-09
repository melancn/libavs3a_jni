package com.inlz.avs3a.smoke;

import android.content.Context;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class Avs3BridgeInstrumentedTest {

    @Test
    public void testProcessAbi() {
        Context ctx = InstrumentationRegistry.getInstrumentation().getTargetContext();
        assertNotNull(ctx);
    }
}
