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

- `API_CONTRACT_VERSION = 1`
- `TIME_UNSET = Long.MIN_VALUE`
- `INPUT_COMPLETE_SINGLE_FRAME = 1`
