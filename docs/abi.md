# ABI Compatibility

## Supported

- **ABIs**: arm64-v8a, armeabi-v7a
- **Profile**: 0 (channel-based)
- **Channel configs**: 0 (mono), 1 (stereo)
- **Source precision**: 16-bit
- **Neural types**: 0 (Hyper), 1 (HyperLc)

## Not Supported

- Profile 1 (mixed/object), Profile 2 (HOA)
- 8-bit and 24-bit source precision
- Neural types 2-7
- Channel configs 2-127

## ABI Layout (static verified)

| Field | ARM64 offset | ARMv7 offset | Size | Storage |
| --- | --- | --- | --- | --- |
| firstFrame | 0 | 0 | 2 | int16 |
| sampleRate | 4 | 4 | 4 | int32 |
| sourceBits | 8 | 8 | 2 | int16 |
| totalBitrate | 12 | 12 | 4 | int32 |
| channelConfig | 20 | 20 | 4 | int32 |
| channels | 24 | 24 | 2 | int16 |
| frameSamples | 48 | 48 | 2 | int16 |
| payloadBits | 52 | 52 | 4 | int32 |
| neuralCodecType | 56 | 56 | 4 | int32 |
| modelType | 60 | 60 | 4 | int32 |
| bitstreamPointer | 80 | 72 | pointer | pointer |
| metadata | 248 | 156 | pointer | pointer |

- ARM64 state: 264 bytes (pointer=8)
- ARMv7 state: 164 bytes (pointer=4)
- Bitstream: 12304 bytes (payload[12300] + cursor[4])

## ABI GAPS

None for current scope. Runtime validation: NOT_RUN.
