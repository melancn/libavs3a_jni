# ABI GAPS

## Current Status: NO GAPS FOR DECLARED SCOPE

All required ABI fields for profile 0, channel configs 0-10 (mono/stereo plus channel-based MC), 16-bit source precision, neural types 0/1 are statically verified.

Channel configs 4 (MC_10_2) and 5 (MC_22_2) are representable layouts in the vendor's `mcChannelConfigTable`, but `codecBitrateConfigTable` slots 4/5 are NULL in both vendor ABIs; the vendor build cannot decode them, so the SDK rejects them with `UNSUPPORTED_MODE` before initialization.

## Runtime Gaps (separate from ABI)

- Real mono/stereo and MC audio fixtures and independent reference PCM: NOT_PROVIDED
- SDK priming/tail delay: NOT_PROVIDED
- Dual ABI release AAR runtime reports: NOT_RUN
- MC layouts are statically wired (channel counts, LFE flag, decoder format, per-layout bitrate tables) but runtime acceptance targets the 5.1 sample first; other layouts are marked per-sample as evidence arrives
- Distribution authorization: NOT_PROVIDED (redistributionApproved=false)

These are tracked in T08_INPUTS_AND_ACCEPTANCE.md and do not block bridge development or full local test builds.
