# AVS3A JNI SDK

An independent Android SDK for AV3A (Audio Vivid) elementary stream decoding via a vendor JNI bridge.

## Overview

This SDK provides a Java API + dual-ABI self-built JNI bridge that adapts to a verified vendor decoder (`libavs3a_decoder.so` + `model.bin`). It does not include network, demux, AudioTrack, renderer, UI, or player clock components.

## Artifacts

| Coordinate | Description |
| --- | --- |
| `com.inlz.avs3a:avs3a-sdk-bridge:<version>` | Java API + JNI bridge, no vendor SO/model |
| `com.inlz.avs3a:avs3a-sdk:<version>` | Full AAR with vendor inputs (requires authorization) |

## Quick Start

```java
Avs3Capabilities caps = Avs3Sdk.capabilities(context);
VerifiedModel model = Avs3Sdk.prepareBundledModel(context);
Avs3Session session = Avs3Sdk.open(model, epoch);
session.queueInput(data, 0, data.length, ptsUs, epoch, 0);
PcmResult result = session.receivePcm(output, 0, output.length);
session.close();
```

## Bridge vs Full

- **bridge**: Compiles and runs without vendor binaries. Decoder capability returns `VENDOR_ABI_NOT_READY` when vendor is absent.
- **full**: Requires staged vendor inputs (SHA-verified), ABI/dialect readiness, and ARM device validation before release.

## Error Codes

| Code | Name |
| --- | --- |
| -1000 | INVALID_ARGUMENT |
| -1001 | BRIDGE_UNAVAILABLE |
| -1004 | VENDOR_ABI_NOT_READY |
| -1030 | CLOSED_OR_INVALID_HANDLE |
| -1031 | STALE_EPOCH |
| -1091 | INTERNAL |

Full table: [CODE_CONTRACTS.md](../libavs3a_jni-execution-plan/CODE_CONTRACTS.md)

## Threading & Buffer Ownership

- Session methods are `synchronized`; native has a second mutex layer.
- Output `byte[]` is caller-owned. One `receive` writes only the valid range.
- Consumer must copy before reusing the same array in the next `receive`.

## Limitations

- **16KB page compatibility**: vendor LOAD alignment is 4KB; 16KB page support not verified.
- **Vendor exit risk**: model open failure may exit the process; Java/C++ catch cannot intercept.
- **Redistribution**: `vendor.lock.json.redistributionApproved` is `false`. Full release requires authorization.
- **Runtime validation**: NOT_RUN. Static ABI/dialect contracts are verified but real PCM/device validation is pending.
