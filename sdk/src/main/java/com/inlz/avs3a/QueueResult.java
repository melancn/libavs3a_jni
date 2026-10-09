package com.inlz.avs3a;

public enum QueueResult {
    ACCEPTED,
    BACKPRESSURE;

    static QueueResult fromNative(int code) {
        return code == 0 ? ACCEPTED : BACKPRESSURE;
    }
}
