# ABI GAPS

## Current Status: NO GAPS FOR DECLARED SCOPE

All required ABI fields for profile 0, channel config 0/1, 16-bit source precision, neural types 0/1 are statically verified.

## Runtime Gaps (separate from ABI)

- Real mono/stereo audio fixture and independent reference PCM: NOT_PROVIDED
- SDK priming/tail delay: NOT_PROVIDED
- Dual ABI release AAR runtime reports: NOT_RUN
- Distribution authorization: NOT_PROVIDED (redistributionApproved=false)

These are tracked in T08_INPUTS_AND_ACCEPTANCE.md and do not block bridge development or full local test builds.
