package com.inlz.avs3a;

import org.junit.Test;
import static org.junit.Assert.*;

public class Avs3SdkTest {

    @Test(expected = Avs3Exception.class)
    public void testOpenNullModel() throws Avs3Exception {
        Avs3Sdk.open(null, 0);
    }

    @Test(expected = Avs3Exception.class)
    public void testOpenNegativeEpoch() throws Avs3Exception {
        Avs3Sdk.open(null, -1);
    }

    @Test(expected = Avs3Exception.class)
    public void testNewFrameParserNegativeEpoch() throws Avs3Exception {
        Avs3Sdk.newFrameParser(-1);
    }

    @Test(expected = Avs3Exception.class)
    public void testCapabilitiesBeforeLoad() throws Avs3Exception {
        Avs3Sdk.capabilities(null);
    }
}
