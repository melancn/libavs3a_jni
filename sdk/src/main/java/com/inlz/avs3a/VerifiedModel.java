package com.inlz.avs3a;

import android.content.Context;

public final class VerifiedModel {
    private final Context appContext;
    private final String internalPath;
    private final String vendorId;
    private final String sha256;

    VerifiedModel(Context appContext, String internalPath, String vendorId, String sha256) {
        this.appContext = appContext == null ? null : appContext.getApplicationContext();
        this.internalPath = internalPath;
        this.vendorId = vendorId;
        this.sha256 = sha256;
    }

    Context appContext() { return appContext; }
    String internalPath() { return internalPath; }
    String vendorId() { return vendorId; }
    String sha256() { return sha256; }

    @Override
    public String toString() {
        return "VerifiedModel{vendorId=" + vendorId + ", sha256=" + sha256.substring(0, 12) + "...}";
    }
}
