#include "avs3a/header_parser.h"
#include "avs3a/types.h"
#include <cstring>
#include <cmath>

namespace avs3a {

// All bitstream tables (sample rates, per-layout bitrate indexes, CRC16,
// channel configurations) come from the generated frame dialect contract:
// native/vendor/frame-dialect.json -> frame_dialect_generated.h.
// Evidence: frozen evidence tables.extracted.json (both ABIs identical) and
// vendor Avs3ParseBsFrameHeader disassembly.

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
        crc = ((crc << 8) ^ kCrc16VendorTable[(crc >> 8) & 255] ^ data[i]) & 0xffffu;
    }
    return static_cast<uint16_t>(crc);
}

const ChannelConfiguration* find_channel_configuration(int32_t channel_config) {
    for (int32_t i = 0; i < kChannelConfigurationCount; ++i) {
        if (kChannelConfigurations[i].channel_config == channel_config)
            return &kChannelConfigurations[i];
    }
    return nullptr;
}

static ChannelMode sdk_channel_mode(int32_t vendor_decoder_format) {
    switch (vendor_decoder_format) {
        case 0: return CHANNEL_MODE_MONO;
        case 1: return CHANNEL_MODE_STEREO;
        case 2: return CHANNEL_MODE_MC;
        default: return CHANNEL_MODE_UNKNOWN;
    }
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
    if (precision_idx == 3u)
        return HeaderResult::invalid("reserved precision index");
    if (precision_idx != 1u)
        return HeaderResult::unsupported("unsupported source precision");

    const ChannelConfiguration* cc = find_channel_configuration(
        static_cast<int32_t>(channel_cfg));
    if (!cc)
        return HeaderResult::unsupported("channel config out of range");
    if (!cc->bitrate_table)
        return HeaderResult::unsupported("channel config has no vendor bitrate table");

    int32_t bitrate = cc->bitrate_table[bitrate_idx];
    if (!bitrate)
        return HeaderResult::invalid("zero bitrate index");

    int32_t rate = kSampleRateIndexTable[sr_idx];
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

    ChannelMode mode = sdk_channel_mode(cc->decoder_format);

    FrameConfig cfg;
    cfg.sample_rate           = rate;
    cfg.bitrate               = bitrate;
    cfg.channels              = cc->channels;
    cfg.mode                  = mode;
    cfg.neural_type           = neural_type;
    cfg.channel_config        = channel_cfg;
    cfg.layout_id             = cc->layout_id;
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
    cfg.vendor_fields.channel_count       = static_cast<int16_t>(cc->channels);
    cfg.vendor_fields.object_count        = 0;
    cfg.vendor_fields.object_bitrate      = 0;
    cfg.vendor_fields.bed_bitrate         = 0;
    cfg.vendor_fields.mixed_content_type  = 0;
    cfg.vendor_fields.mixed_content       = 0;
    cfg.vendor_fields.lfe_flag            = static_cast<int16_t>(cc->lfe_flag);
    cfg.vendor_fields.decoder_format      = static_cast<int16_t>(cc->decoder_format);
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
