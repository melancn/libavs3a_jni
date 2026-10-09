package com.inlz.avs3a;

import org.junit.Test;
import static org.junit.Assert.*;
import org.junit.Before;

public class Avs3ErrorTest {

    @Test
    public void testErrorCodesMatchContract() {
        assertEquals(-1000, Avs3Error.INVALID_ARGUMENT.code());
        assertEquals(-1001, Avs3Error.BRIDGE_UNAVAILABLE.code());
        assertEquals(-1004, Avs3Error.VENDOR_ABI_NOT_READY.code());
        assertEquals(-1005, Avs3Error.NATIVE_CONTRACT_MISMATCH.code());
        assertEquals(-1030, Avs3Error.CLOSED_OR_INVALID_HANDLE.code());
        assertEquals(-1031, Avs3Error.STALE_EPOCH.code());
        assertEquals(-1090, Avs3Error.NO_MEMORY.code());
        assertEquals(-1091, Avs3Error.INTERNAL.code());
    }

    @Test
    public void testFromCode() {
        assertEquals(Avs3Error.INVALID_ARGUMENT, Avs3Error.fromCode(-1000));
        assertEquals(Avs3Error.STALE_EPOCH, Avs3Error.fromCode(-1031));
        assertEquals(Avs3Error.INTERNAL, Avs3Error.fromCode(-99999));
    }
}
