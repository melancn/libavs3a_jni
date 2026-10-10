# Isolated Media3 MP4 extension

Upstream: AndroidX Media3 **1.11.1**, Apache-2.0.
Source: https://dl.google.com/dl/android/maven2/androidx/media3/media3-extractor/1.11.1/media3-extractor-1.11.1-sources.jar
SHA-256: `547129d7df7ef9cc3d2622036d3880ab205e5fe3241095049b39c6632c64bc4a`

Six source files in `src/main/java/com/inlz/avs3a/demo/media3/mp4/` retain their
upstream copyright headers. Package relocation isolates them from the library.
Only BoxParser's audio-entry dispatch is extended: recognize av3a, preserve dca3
configuration, and publish audio/av3a. MP4 sample tables, co64, edit lists, seeking,
video and other audio parsing remain upstream implementations. Other MP4 helper
classes are referenced from the version-locked library, not duplicated.

This demo selects this extractor for ordinary MP4. Fragmented/encrypted AVS3 MP4
is NOT a validated feature. Full transport headers in MP4 samples are required.
On an upgrade compare each file to the pinned sources and rerun extraction tests.
