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

5. **Supported Configurations**: Only profile 0, channel config 0/1 (mono/stereo), 16-bit source precision, neural types 0/1. Not all index table combinations are safe for decoder initialization (signed16 budget limit: payloadBits <= 32767).
