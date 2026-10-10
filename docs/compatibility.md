# Compatibility

## Device Requirements

- Android API 24+ (minSdk 24)
- ARM (arm64-v8a or armeabi-v7a)
- 4KB page size (16KB page not verified for vendor)

## Known Limitations

1. **16KB Page Size**: Vendor LOAD alignment is 4096 bytes. Compatibility with 16KB page devices is NOT verified. The self-built JNI bridge targets `max-page-size=16384` on ARM64, but vendor compatibility is independent.

2. **Process Exit Risk**: The vendor decoder may call `exit()` on model open failure or certain internal errors. Java exceptions and C++ catch cannot intercept this. Device tests for exit-risk scenarios run in separate processes.

3. **Redistribution**: `vendor.lock.json.redistributionApproved = false`. Full AAR distribution requires explicit authorization from the rights holder.

4. **Runtime Validation Status**: NOT_RUN. Static ABI layout, frame dialect, and CRC are verified through cross-ABI binary analysis and reference C tests. Real audio decoding on ARM devices has not been validated.

5. **Supported Configurations**: Only profile 0, channel configs 0-10 (mono, stereo, and channel-based MC: 4.0, 5.1, 7.1, 5.1.2, 5.1.4, 7.1.2, 7.1.4), 16-bit source precision, neural types 0/1. Channel configs 4 (MC_10_2) and 5 (MC_22_2) are representable vendor layouts but have no bitrate table in this vendor build (NULL `codecBitrateConfigTable` slot) and are rejected as `UNSUPPORTED_MODE`. Not all index table combinations are safe for decoder initialization (signed16 budget limit: payloadBits <= 32767).

6. **Runtime Vendor Fingerprint**: `Avs3Capabilities.vendorFingerprintMatched()` reports a runtime size+SHA-256 check of the packaged `libavs3a_decoder.so` against the build-time vendor lock. File presence alone does not count as a match, and `Avs3Sdk.open` refuses vendors whose fingerprint does not match.
