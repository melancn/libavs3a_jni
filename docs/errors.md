# Error Codes

| Code | Name | Description |
| --- | --- | --- |
| -1000 | INVALID_ARGUMENT | Bad offset/length/flags/epoch |
| -1001 | BRIDGE_UNAVAILABLE | JNI library load failed |
| -1002 | VENDOR_UNAVAILABLE | Vendor SO not found |
| -1003 | VENDOR_SYMBOL_MISSING | Required symbol missing |
| -1004 | VENDOR_ABI_NOT_READY | ABI contract not verified |
| -1005 | NATIVE_CONTRACT_MISMATCH | Version mismatch |
| -1010 | MODEL_MISSING | Model file not found |
| -1011 | MODEL_CORRUPT | Model SHA mismatch |
| -1012 | MODEL_IO_FAILED | IO error during staging |
| -1020 | UNSUPPORTED_MODE | Unsupported stream config (non-channel-based profile, neural type > 1, source precision != 16, channel config without vendor bitrate table, e.g. MC_10_2/MC_22_2) |
| -1021 | INVALID_HEADER | Bad frame header or CRC |
| -1022 | INPUT_TOO_LARGE | Chunk exceeds MAX_INPUT_CHUNK |
| -1023 | TRUNCATED_FRAME | Incomplete frame at EOS |
| -1024 | UNSUPPORTED_CONFIG_CHANGE | Mid-stream config change |
| -1025 | FRAME_DIALECT_NOT_READY | Dialect not verified |
| -1026 | RESYNC_LIMIT | Too many resync attempts |
| -1030 | CLOSED_OR_INVALID_HANDLE | Session closed or invalid |
| -1031 | STALE_EPOCH | Old epoch after flush |
| -1032 | INVALID_STATE | State machine violation |
| -1033 | COMPLETE_SAMPLE_CONTRACT | Complete-frame contract violated |
| -1034 | HANDLE_KIND_MISMATCH | Wrong handle type for operation |
| -1090 | NO_MEMORY | Allocation failure |
| -1091 | INTERNAL | Unexpected internal error |
