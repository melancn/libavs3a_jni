package com.inlz.avs3a;

public enum Avs3Error {
    INVALID_ARGUMENT(-1000),
    BRIDGE_UNAVAILABLE(-1001),
    VENDOR_UNAVAILABLE(-1002),
    VENDOR_SYMBOL_MISSING(-1003),
    VENDOR_ABI_NOT_READY(-1004),
    NATIVE_CONTRACT_MISMATCH(-1005),
    MODEL_MISSING(-1010),
    MODEL_CORRUPT(-1011),
    MODEL_IO_FAILED(-1012),
    UNSUPPORTED_MODE(-1020),
    INVALID_HEADER(-1021),
    INPUT_TOO_LARGE(-1022),
    TRUNCATED_FRAME(-1023),
    UNSUPPORTED_CONFIG_CHANGE(-1024),
    FRAME_DIALECT_NOT_READY(-1025),
    RESYNC_LIMIT(-1026),
    CLOSED_OR_INVALID_HANDLE(-1030),
    STALE_EPOCH(-1031),
    INVALID_STATE(-1032),
    COMPLETE_SAMPLE_CONTRACT(-1033),
    HANDLE_KIND_MISMATCH(-1034),
    NO_MEMORY(-1090),
    INTERNAL(-1091);

    private final int code;

    Avs3Error(int code) {
        this.code = code;
    }

    public int code() {
        return code;
    }

    static Avs3Error fromCode(int code) {
        for (Avs3Error e : values()) {
            if (e.code == code) return e;
        }
        return INTERNAL;
    }
}
