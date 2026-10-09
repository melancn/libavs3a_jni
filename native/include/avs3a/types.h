#ifndef AVS3A_TYPES_H
#define AVS3A_TYPES_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <limits>
#include "status.h"

namespace avs3a {

constexpr int64_t TIME_UNSET = std::numeric_limits<int64_t>::min();

enum ChannelMode : int32_t {
    CHANNEL_MODE_UNKNOWN = 0,
    CHANNEL_MODE_MONO    = 1,
    CHANNEL_MODE_STEREO  = 2,
};

struct ByteSpan {
    const uint8_t* data;
    size_t size;
    ByteSpan() : data(nullptr), size(0) {}
    ByteSpan(const uint8_t* d, size_t s) : data(d), size(s) {}
};

struct MutableByteSpan {
    uint8_t* data;
    size_t size;
    MutableByteSpan() : data(nullptr), size(0) {}
    MutableByteSpan(uint8_t* d, size_t s) : data(d), size(s) {}
};

struct VerifiedHeaderFields {
    int32_t sample_rate       = 0;
    int32_t total_bitrate      = 0;
    int32_t bitrate_copy       = 0;
    int32_t channel_config     = 0;
    int16_t channel_count       = 1;
    int16_t object_count        = 0;
    int32_t object_bitrate      = 0;
    int32_t bed_bitrate         = 0;
    int16_t mixed_content_type  = 0;
    int16_t mixed_content       = 0;
    int16_t lfe_flag            = 0;
    int16_t decoder_format      = 0;
    int16_t option44            = 0;
    int16_t hoa_order           = 0;
    int16_t frame_samples       = 1024;
    int32_t payload_bits        = 0;
    int32_t neural_codec_type   = 0;
    int32_t model_type          = 1;
};

struct FrameConfig {
    int32_t sample_rate            = 0;
    int32_t channels               = 1;
    int32_t samples_per_channel    = 1024;
    int32_t bitrate                = 0;
    ChannelMode mode               = CHANNEL_MODE_MONO;
    int32_t neural_type            = 0;
    int32_t channel_config         = 0;
    int32_t source_bits            = 16;
    int32_t payload_bits            = 0;
    int32_t payload_bytes           = 0;
    int32_t frame_bytes             = 0;
    int32_t header_bytes            = 7;
    uint16_t crc                   = 0;
    VerifiedHeaderFields vendor_fields;
};

struct EncodedFrame {
    std::vector<uint8_t> bytes;
    FrameConfig config;
    size_t payload_offset  = 0;
    size_t payload_size   = 0;
    int64_t pts_us         = 0;
};

struct OwnedPcm {
    std::vector<int16_t> samples;
    FrameConfig config;
    int64_t pts_us  = 0;
    int64_t epoch    = 0;
};

struct PcmMetadata {
    int64_t pts_us;
    int64_t sample_rate;
    int64_t channels;
    int64_t samples_per_channel;
    int64_t byte_count;
    int64_t layout_id;
    int64_t flags;
    int64_t epoch;
};

struct FrameMetadata {
    int64_t pts_us;
    int64_t sample_rate;
    int64_t channels;
    int64_t samples_per_channel;
    int64_t frame_bytes;
    int64_t payload_offset;
    int64_t payload_bytes;
    int64_t bitrate_bps;
    int64_t channel_mode;
    int64_t epoch;
};

constexpr size_t MAX_INPUT_CHUNK   = 256 * 1024;
constexpr size_t QUEUE_LIMIT        = 512 * 1024;
constexpr size_t MAX_PENDING_PCM    = 4096;
constexpr size_t MAX_FRAME_BYTES     = 5120;
constexpr size_t MAX_ADMITTED_FRAME  = 4096;
constexpr int32_t SDK_MAX_PAYLOAD_BITS = 32767;

inline bool is_channel_based_mono_or_stereo(const FrameConfig& cfg) {
    return cfg.channels == 1 || cfg.channels == 2;
}

inline bool same_codec_configuration(const FrameConfig& a, const FrameConfig& b) {
    return a.sample_rate     == b.sample_rate &&
           a.channels        == b.channels &&
           a.bitrate         == b.bitrate &&
           a.mode            == b.mode &&
           a.neural_type     == b.neural_type &&
           a.channel_config  == b.channel_config &&
           a.source_bits     == b.source_bits;
}

} // namespace avs3a

#endif // AVS3A_TYPES_H
