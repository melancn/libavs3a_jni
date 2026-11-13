# AVS3A JNI SDK

An independent Android SDK for AV3A (Audio Vivid) elementary stream decoding.

## Overview

This SDK provides a Java API plus a self-built JNI bridge that statically links
the AVS3A reference decoder and its model into a single runtime library per ABI:

```text
arm64-v8a/libavs3a_jni.so
armeabi-v7a/libavs3a_jni.so
```

There is no separate `libavs3a_decoder.so`, no `dlopen` of a vendor library, and
no required `model.bin` asset. The model is embedded in the SO for the full
variant. The SDK does not include network, demux, AudioTrack, renderer, UI, or
player clock components.

## Artifacts

| Coordinate | Description |
| --- | --- |
| `com.inlz.avs3a:avs3a-sdk-bridge:<version>` | Java API + JNI bridge/parser, no decoder |
| `com.inlz.avs3a:avs3a-sdk:<version>` | Full AAR with decoder and embedded model (requires authorization) |

## Quick Start

```java
Avs3Capabilities caps = Avs3Sdk.capabilities(context);
VerifiedModel model = Avs3Sdk.prepareBundledModel(context);
Avs3Session session = Avs3Sdk.open(model, epoch);
session.queueInput(data, 0, data.length, ptsUs, epoch, 0);
PcmResult result = session.receivePcm(output, 0, output.length);
session.close();
```

The public API and JNI v2 contract are unchanged; existing business code does
not need new initialization or model-preparation steps.

## Bridge vs Full

- **bridge**: One `libavs3a_jni.so` per ABI with parser/JNI only. Decoder creation
  returns `VENDOR_UNAVAILABLE`; `prepareBundledModel` returns `MODEL_MISSING`.
- **full**: One `libavs3a_jni.so` per ABI with the static decoder and embedded
  model. No external decoder SO or model asset is needed.

Bridge and full are mutually exclusive products; both use the same public API.

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

- **16KB page compatibility**: the single SO is linked with 16KB-compatible
  alignment on arm64; real-device validation is still pending.
- **Redistribution**: `vendor/manifest.lock.json.redistributionApproved` is
  `false`. Full release requires authorization.
- **Runtime validation**: NOT_RUN. Static ABI/dialect contracts are verified but
  real PCM/device validation is pending.

## Media3 demo

See [media3-demo/README.md](media3-demo/README.md) for local MP4/AVS3 playback,
file selection, track selection, decoding diagnostics, build commands and
validation limits. `release-sdk.yml` also packages installable bridge/full debug
APKs alongside the SDK.
