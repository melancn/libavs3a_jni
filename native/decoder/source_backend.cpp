#include "avs3a/backend.h"
#include "avs3a/status.h"
#include "avs3a/types.h"
#include "decoder_adapter.h"

#include <memory>
#include <vector>

namespace avs3a {

bool source_backend_available() {
#ifdef AVS3A_WITH_DECODER
    return true;
#else
    return false;
#endif
}

#ifdef AVS3A_WITH_DECODER

namespace {

Status map_source_status(int rc) {
    switch (rc) {
        case AVS3A_SRC_OK:              return OK;
        case AVS3A_SRC_NO_MEMORY:       return NO_MEMORY;
        case AVS3A_SRC_INVALID_CONFIG:  return UNSUPPORTED_MODE;
        case AVS3A_SRC_INIT_FAILED:     return INTERNAL;
        case AVS3A_SRC_DECODE_FAILED:   return INVALID_HEADER;
        case AVS3A_SRC_INVALID_STATE:   return INVALID_STATE;
        default:                        return INTERNAL;
    }
}

int codec_mode_of(const FrameConfig& cfg) {
    switch (cfg.mode) {
        case CHANNEL_MODE_MONO:   return 0;
        case CHANNEL_MODE_STEREO: return 1;
        case CHANNEL_MODE_MC:     return 2;
        default:                  return -1;
    }
}

Status validate_supported_config(const FrameConfig& cfg) {
    if (!is_supported_channel_based_config(cfg)) return UNSUPPORTED_MODE;
    if (cfg.channel_config < 0 || cfg.channel_config > 10) return UNSUPPORTED_MODE;
    if (cfg.source_bits != 16) return UNSUPPORTED_MODE;
    if (cfg.samples_per_channel != 1024) return UNSUPPORTED_MODE;
    if (cfg.neural_type > 1) return UNSUPPORTED_MODE;
    if (cfg.payload_bits > SDK_MAX_PAYLOAD_BITS) return UNSUPPORTED_MODE;
    if (static_cast<size_t>(cfg.frame_bytes) > MAX_ADMITTED_FRAME) return UNSUPPORTED_MODE;
    return OK;
}

} // namespace

class SourceDecoderBackend : public DecoderBackend {
public:
    ~SourceDecoderBackend() override { destroy(); }

    Status initialize(const FrameConfig& cfg, const VerifiedModelPath& model) override {
        Status v = validate_supported_config(cfg);
        if (is_error(v)) return v;

        int mode = codec_mode_of(cfg);
        if (mode < 0) return UNSUPPORTED_MODE;

        if (decoder_) destroy();
        decoder_ = avs3a_source_create();
        if (!decoder_) return NO_MEMORY;

        const char* model_path = model.absolute_path.empty() ? NULL : model.absolute_path.c_str();
        int rc = avs3a_source_initialize(decoder_, cfg.sample_rate, cfg.channels,
                                         cfg.samples_per_channel, cfg.bitrate, mode,
                                         cfg.neural_type, cfg.channel_config,
                                         cfg.source_bits, model_path);
        if (rc != AVS3A_SRC_OK) {
            destroy();
            return map_source_status(rc);
        }
        config_ = cfg;
        return OK;
    }

    Status decode(const EncodedFrame& frame, OwnedPcm& output) override {
        if (!decoder_) return INVALID_STATE;

        int expected = avs3a_source_expected_samples(decoder_);
        if (expected <= 0) return INTERNAL;

        output.samples.resize(static_cast<size_t>(expected));
        int produced = 0;
        int rc = avs3a_source_decode(decoder_,
                                     frame.bytes.data() + frame.payload_offset,
                                     static_cast<int>(frame.payload_size),
                                     output.samples.data(), expected, &produced);
        if (rc != AVS3A_SRC_OK) return map_source_status(rc);
        if (produced != expected) return INTERNAL;

        output.config = config_;
        output.pts_us = frame.pts_us;
        return OK;
    }

    void destroy() noexcept override {
        if (decoder_) {
            avs3a_source_destroy(decoder_);
            decoder_ = nullptr;
        }
    }

    bool is_ready() const override { return true; }

private:
    avs3a_source_decoder* decoder_ = nullptr;
    FrameConfig config_;
};

std::unique_ptr<DecoderBackend> create_source_backend() {
    return std::make_unique<SourceDecoderBackend>();
}

#else  // !AVS3A_WITH_DECODER

std::unique_ptr<DecoderBackend> create_source_backend() {
    return nullptr;
}

#endif  // AVS3A_WITH_DECODER

} // namespace avs3a
