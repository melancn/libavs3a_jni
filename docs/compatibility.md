# Compatibility

## Device Requirements

- Android API 24+ (minSdk 24)
- ARM (arm64-v8a or armeabi-v7a)
- The single SDK SO is linked with 16KB-compatible alignment on arm64; 16KB device validation is still pending.

## Runtime Layout

- Exactly one SDK runtime SO per ABI: `libavs3a_jni.so` (JNI + core + static reference decoder + embedded model in the full variant).
- No separate `libavs3a_decoder.so` is packaged or loaded, and no `model.bin` asset is required at runtime.

## Known Limitations

1. **16KB Page Size**: The single SO is built with `max-page-size=16384` on arm64. Real-device 16KB validation is NOT_RUN.

2. **Process Exit Risk**: The vendored decoder's `exit()`/`assert()`/`abort()` calls on allocation or invariant failure have been replaced with error returns. The static C++ runtime still uses `abort` for `std::terminate`, but decoder input/config errors no longer terminate the process.

3. **Redistribution**: `vendor/manifest.lock.json.redistributionApproved = false`. Full AAR distribution requires explicit authorization from the rights holder.

4. **Runtime Validation Status**: NOT_RUN. Static ABI layout, frame dialect, and CRC are verified through cross-ABI binary analysis and reference C tests. Real audio decoding on ARM devices has not been validated.

5. **Supported Configurations**: Only profile 0, channel configs 0-10 (mono, stereo, and channel-based MC: 4.0, 5.1, 7.1, 5.1.2, 5.1.4, 7.1.2, 7.1.4), 16-bit source precision, neural types 0/1. Channel configs 4 (MC_10_2) and 5 (MC_22_2) are representable vendor layouts but have no bitrate table in this vendor build (NULL `codecBitrateConfigTable` slot) and are rejected as `UNSUPPORTED_MODE`. Not all index table combinations are safe for decoder initialization (signed16 budget limit: payloadBits <= 32767).

6. **Decoder Provider**: `Avs3Capabilities.vendorPresent()` and `vendorFingerprintMatched()` now report whether the decoder is statically linked into the loaded `libavs3a_jni.so` (full variant). The historical getter names are kept, but no second library is searched or SHA-checked at runtime.

