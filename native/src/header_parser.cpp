#include "avs3a/header_parser.h"
#include "avs3a/types.h"
#include <cstring>
#include <cmath>

namespace avs3a {

static const int32_t rates[9] = {
    192000, 96000, 48000, 44100, 32000, 24000, 22050, 16000, 8000
};

static const int32_t mono_bitrates[16] = {
    16000, 32000, 44000, 56000, 64000, 72000, 80000, 96000,
    128000, 144000, 164000, 192000, 0, 0, 0, 0
};

static const int32_t stereo_bitrates[16] = {
    24000, 32000, 48000, 64000, 80000, 96000, 128000, 144000,
    192000, 256000, 320000, 0, 0, 0, 0, 0
};

static const uint16_t crc_table[256] = {
    0, 4129, 8258, 12387, 16516, 20645, 24774, 28903,
    33032, 37161, 41290, 45419, 49548, 53677, 57806, 61935,
    4657, 528, 12915, 8786, 21173, 17044, 29431, 25302,
    37689, 33560, 45947, 41818, 54205, 50076, 62463, 58334,
    9314, 13379, 1056, 5121, 25830, 29895, 17572, 21637,
    42346, 46411, 34088, 38153, 58862, 62927, 50604, 54669,
    13907, 9842, 5649, 1584, 30423, 26358, 22165, 18100,
    46939, 42874, 38681, 34616, 63455, 59390, 55197, 51132,
    18628, 22757, 26758, 30887, 2112, 6241, 10242, 14371,
    51660, 55789, 59790, 63919, 35144, 39273, 43274, 47403,
    23285, 19156, 31415, 27286, 6769, 2640, 14899, 10770,
    56317, 52188, 64447, 60318, 39801, 35672, 47931, 43802,
    27814, 31879, 19684, 23749, 11298, 15363, 3168, 7233,
    60846, 64911, 52716, 56781, 44330, 48395, 36200, 40265,
    32407, 28342, 24277, 20212, 15891, 11826, 7761, 3696,
    65439, 61374, 57309, 53244, 48923, 44858, 40793, 36728,
    37256, 33193, 45514, 41451, 53516, 49453, 61774, 57711,
    4224, 161, 12482, 8419, 20484, 16421, 28742, 24679,
    33721, 37784, 41979, 46042, 49981, 54044, 58239, 62302,
    689, 4752, 8947, 13010, 16949, 21012, 25207, 29270,
    46570, 42443, 38312, 34185, 62830, 58703, 54572, 50445,
    13538, 9411, 5280, 1153, 29798, 25671, 21540, 17413,
    42971, 47098, 34713, 38840, 59231, 63358, 50973, 55100,
    9939, 14066, 1681, 5808, 26199, 30326, 17941, 22068,
    55628, 51565, 63758, 59695, 39368, 35305, 47498, 43435,
    22596, 18533, 30726, 26663, 6336, 2273, 14466, 10403,
    52093, 56156, 60223, 64286, 35833, 39896, 43963, 48026,
    19061, 23124, 27191, 31254, 2801, 6864, 10931, 14994,
    64814, 60687, 56684, 52557, 48554, 44427, 40424, 36297,
    31782, 27655, 23652, 19525, 15522, 11395, 7392, 3265,
    61215, 65342, 53085, 57212, 44955, 49082, 36825, 40952,
    28183, 32310, 20053, 24180, 11923, 16050, 3793, 7920
};

static uint32_t read_bits(const uint8_t* p, unsigned start, unsigned n) {
    uint32_t v = 0;
    for (unsigned i = 0; i < n; ++i) {
        unsigned b = start + i;
        v = (v << 1) | ((p[b / 8] >> (7 - b % 8)) & 1u);
    }
    return v;
}

uint16_t compute_crc16(const uint8_t* data, size_t n) {
    uint32_t crc = 0xffffu;
    for (size_t i = 0; i < n; ++i) {
        crc = ((crc << 8) ^ crc_table[(crc >> 8) & 255] ^ data[i]) & 0xffffu;
    }
    return static_cast<uint16_t>(crc);
}

HeaderResult HeaderParser::parse_header(ByteSpan bytes) const {
    if (!bytes.data || bytes.size == 0)
        return HeaderResult::invalid("null input");
    if (bytes.size < 7)
        return HeaderResult::need_more(7);

    const uint8_t* p = bytes.data;

    if (read_bits(p, 0, 12) != 4095u)
        return HeaderResult::invalid("sync mismatch");
    if (read_bits(p, 12, 4) != 2u)
        return HeaderResult::invalid("codec id mismatch");
    if (read_bits(p, 16, 1) != 0u)
        return HeaderResult::invalid("reserved flag not zero");

    uint32_t neural_type = read_bits(p, 17, 3);
    uint32_t profile = read_bits(p, 20, 3);
    uint32_t sr_idx = read_bits(p, 23, 4);
    uint32_t channel_cfg = read_bits(p, 35, 7);
    uint32_t precision_idx = read_bits(p, 42, 2);
    uint32_t bitrate_idx = read_bits(p, 44, 4);

    if (profile != 0u)
        return HeaderResult::unsupported("non-channel-based profile");
    if (neural_type > 1u)
        return HeaderResult::unsupported("neural type out of range");
    if (sr_idx >= 9u)
        return HeaderResult::invalid("sample rate index out of range");
    if (channel_cfg > 1u)
        return HeaderResult::unsupported("channel config out of range");
    if (precision_idx == 3u)
        return HeaderResult::invalid("reserved precision index");
    if (precision_idx != 1u)
        return HeaderResult::unsupported("unsupported source precision");

    const int32_t* br_table = (channel_cfg == 0u) ? mono_bitrates : stereo_bitrates;
    int32_t bitrate = br_table[bitrate_idx];
    if (!bitrate)
        return HeaderResult::invalid("zero bitrate index");

    int32_t rate = rates[sr_idx];
    volatile float ratio = static_cast<float>(bitrate) / static_cast<float>(rate);
    volatile float total = ratio * 1024.0f;
    uint32_t frame_bits = static_cast<uint32_t>(total);
    if (frame_bits <= 56u)
        return HeaderResult::invalid("frame bits too small");

    uint32_t payload_bits = frame_bits - 56u;
    uint32_t payload_bytes = (payload_bits + 7u) / 8u;
    uint32_t frame_bytes = 7u + payload_bytes;

    if (payload_bytes > 12300u)
        return HeaderResult::invalid("payload exceeds capacity");
    if (payload_bits > static_cast<uint32_t>(SDK_MAX_PAYLOAD_BITS))
        return HeaderResult::unsupported("payload bits exceed signed16 budget");

    FrameConfig cfg;
    cfg.sample_rate           = rate;
    cfg.bitrate               = bitrate;
    cfg.channels              = channel_cfg + 1u;
    cfg.mode                  = (channel_cfg == 0u) ? CHANNEL_MODE_MONO : CHANNEL_MODE_STEREO;
    cfg.neural_type           = neural_type;
    cfg.channel_config        = channel_cfg;
    cfg.source_bits           = 16;
    cfg.samples_per_channel  = 1024;
    cfg.payload_bits          = payload_bits;
    cfg.payload_bytes         = payload_bytes;
    cfg.frame_bytes           = frame_bytes;
    cfg.header_bytes          = 7;
    cfg.crc                   = static_cast<uint16_t>((read_bits(p, 27, 8) << 8) | read_bits(p, 48, 8));

    cfg.vendor_fields.sample_rate       = rate;
    cfg.vendor_fields.total_bitrate      = bitrate;
    cfg.vendor_fields.bitrate_copy       = bitrate;
    cfg.vendor_fields.channel_config     = channel_cfg;
    cfg.vendor_fields.channel_count       = static_cast<int16_t>(channel_cfg + 1u);
    cfg.vendor_fields.object_count        = 0;
    cfg.vendor_fields.object_bitrate      = 0;
    cfg.vendor_fields.bed_bitrate         = 0;
    cfg.vendor_fields.mixed_content_type  = 0;
    cfg.vendor_fields.mixed_content       = 0;
    cfg.vendor_fields.lfe_flag            = 0;
    cfg.vendor_fields.decoder_format      = static_cast<int16_t>(channel_cfg);
    cfg.vendor_fields.option44            = 0;
    cfg.vendor_fields.hoa_order           = 0;
    cfg.vendor_fields.frame_samples       = 1024;
    cfg.vendor_fields.payload_bits        = static_cast<int32_t>(payload_bits);
    cfg.vendor_fields.neural_codec_type   = static_cast<int32_t>(neural_type);
    cfg.vendor_fields.model_type          = 1;

    return HeaderResult::ready(cfg, frame_bytes, 7, payload_bytes);
}

Status HeaderParser::validate_frame(const uint8_t* data, size_t size, FrameConfig& cfg) const {
    if (!data || size < 7) return INVALID_ARGUMENT;

    HeaderResult hr = parse_header(ByteSpan(data, size));
    if (hr.kind == HeaderResult::NeedMore) return RECEIVE_NEED_INPUT;
    if (hr.kind == HeaderResult::Invalid) return hr.error;

    if (size < hr.frame_bytes) return RECEIVE_NEED_INPUT;

    uint16_t computed = compute_crc16(data + 7, hr.payload_size);
    if (computed != hr.config.crc) {
        cfg = hr.config;
        return INVALID_HEADER;
    }
    cfg = hr.config;
    return OK;
}

} // namespace avs3a
