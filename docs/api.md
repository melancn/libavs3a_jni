# API Reference

## Avs3Sdk

| Method | Description |
| --- | --- |
| `capabilities(Context)` | Load bridge, return ABI/vendor readiness |
| `prepareBundledModel(Context)` | Stage bundled model.bin to private storage |
| `prepareModel(Context, ModelSource)` | Stage external model with SHA verification |
| `open(VerifiedModel, long epoch)` | Create decoder session |
| `newFrameParser(long epoch)` | Create header-only frame parser |

## Avs3Session

| Method | Returns |
| --- | --- |
| `queueInput(byte[], int, int, long, long, int)` | QueueResult (ACCEPTED/BACKPRESSURE) |
| `receivePcm(byte[], int, int)` | PcmResult (NEED_INPUT/READY/END_OF_STREAM/OUTPUT_TOO_SMALL) |
| `signalEndOfInput()` | void |
| `flush(long newEpoch)` | void |
| `close()` | void (idempotent) |

## Avs3FrameParser

| Method | Returns |
| --- | --- |
| `queueInput(byte[], int, int, long, long, int)` | QueueResult |
| `receiveFrame(byte[], int, int)` | FrameResult (NEED_INPUT/READY/END_OF_STREAM/OUTPUT_TOO_SMALL) |
| `signalEndOfInput()` | void |
| `flush(long newEpoch)` | void |
| `close()` | void (idempotent) |

## Constants

- `API_CONTRACT_VERSION = 2`
- `TIME_UNSET = Long.MIN_VALUE`
- `INPUT_COMPLETE_SINGLE_FRAME = 1`

## PcmLayout

PCM speaker layouts reported by `PcmInfo.layout()` and `EncodedFrameInfo.layout()`.
Ids are contractual and equal the stream's channelConfig + 1:

| Layout | id | channels | Decodable by this SDK |
| --- | ---: | ---: | --- |
| `MONO` | 1 | 1 | yes |
| `STEREO` | 2 | 2 | yes |
| `MC_5_1_0` | 3 | 6 | yes |
| `MC_7_1_0` | 4 | 8 | yes |
| `MC_10_2` | 5 | 12 | no (no vendor bitrate table) |
| `MC_22_2` | 6 | 24 | no (no vendor bitrate table) |
| `MC_4_0` | 7 | 4 | yes (only MC layout without LFE) |
| `MC_5_1_2` | 8 | 8 | yes |
| `MC_5_1_4` | 9 | 10 | yes |
| `MC_7_1_2` | 10 | 10 | yes |
| `MC_7_1_4` | 11 | 12 | yes |

`channelMode` values in `EncodedFrameInfo`: 1 = mono, 2 = stereo, 3 = multichannel.

## Timestamp and queue behavior

Explicit input PTS anchors are retained by byte position rather than overwritten by the next queued input. Each explicit frame PTS reanchors the SDK timeline; frames without PTS are inferred from the most recent anchor. A flush restores TIME_UNSET until a new anchor is supplied. Queueing after signalEndOfInput is rejected until flush.

INPUT_COMPLETE_SINGLE_FRAME validates a standalone complete sample even when previous complete samples remain queued. For compatibility, it may also complete an existing single partial frame; a rejected chunk does not modify the queue. Media3 always supplies one standalone complete frame per sample.

VerifiedModel is a public opaque token; callers cannot construct it or obtain its private storage path.
