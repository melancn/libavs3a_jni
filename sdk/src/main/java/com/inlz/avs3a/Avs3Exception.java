package com.inlz.avs3a;

public final class Avs3Exception extends Exception {
    private final Avs3Error error;

    Avs3Exception(Avs3Error error) {
        super(error.name());
        this.error = error;
    }

    Avs3Exception(Avs3Error error, String sanitizedMessage) {
        super(error.name() + ": " + sanitizedMessage);
        this.error = error;
    }

    public Avs3Error error() {
        return error;
    }

    static Avs3Exception fromNative(int code) {
        return new Avs3Exception(Avs3Error.fromCode(code));
    }

    static Avs3Exception fromSanitizedFailure(Avs3Error error, String sanitizedCause) {
        return new Avs3Exception(error, sanitizedCause);
    }
}
