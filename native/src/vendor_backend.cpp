#include "avs3a/backend.h"
#include "avs3a/types.h"
#include "avs3a/status.h"
#include "avs3a/header_parser.h"
#include "avs3a/abi_access.h"
#include "vendor_api.h"
#include <dlfcn.h>
#include <cstring>
#include <string>
#include <memory>

namespace avs3a {

class VendorBackend : public DecoderBackend {
public:
    VendorBackend(int process_abi, void* lib_handle, const VendorApi& api)
        : process_abi_(process_abi), lib_handle_(lib_handle), api_(api),
          abi_(create_abi_accessor(process_abi)) {}

    ~VendorBackend() override {
        destroy();
        if (lib_handle_) {
            dlclose(lib_handle_);
            lib_handle_ = nullptr;
        }
    }

    Status initialize(const FrameConfig& cfg, const VerifiedModelPath& model) override {
        if (!abi_.ready()) return VENDOR_ABI_NOT_READY;

        Status v = validate_supported_config(cfg);
        if (is_error(v)) return v;

        if (!model.absolute_path.empty()) {
            FILE* f = fopen(model.absolute_path.c_str(), "rb");
            if (!f) return MODEL_MISSING;
            fclose(f);
        }

        if (!api_.alloc) return VENDOR_SYMBOL_MISSING;

        handle_ = api_.alloc();
        if (!handle_) return NO_MEMORY;

        Status w = abi_.write_verified_initial_fields(static_cast<uint8_t*>(handle_), cfg);
        if (is_error(w)) {
            api_.destroy(handle_);
            handle_ = nullptr;
            return w;
        }

        if (api_.init && !model.absolute_path.empty()) {
            api_.init(handle_, model.absolute_path.c_str());
        }

        config_ = cfg;
        bitstream_ptr_ = nullptr;
        size_t bs_offset = abi_.verified_bitstream_span_offset();
        std::memcpy(&bitstream_ptr_, static_cast<uint8_t*>(handle_) + bs_offset, abi_.pointer_bytes());

        return OK;
    }

    Status decode(const EncodedFrame& frame, OwnedPcm& output) override {
        if (!handle_) return INVALID_STATE;

        Status crc = validate_crc_and_budget(frame);
        if (is_error(crc)) return crc;

        if (!bitstream_ptr_) {
            size_t bs_offset = abi_.verified_bitstream_span_offset();
            std::memcpy(&bitstream_ptr_, static_cast<uint8_t*>(handle_) + bs_offset, abi_.pointer_bytes());
        }
        if (!bitstream_ptr_) return VENDOR_UNAVAILABLE;

        size_t payload_size = frame.payload_size;
        if (payload_size > static_cast<size_t>(abi_.payload_capacity_bytes()))
            return INVALID_HEADER;

        std::memcpy(bitstream_ptr_, frame.bytes.data() + frame.payload_offset, payload_size);

        size_t total_samples = static_cast<size_t>(config_.samples_per_channel) *
                               static_cast<size_t>(config_.channels);
        output.samples.resize(total_samples);

        if (api_.decode) {
            api_.decode(handle_, output.samples.data());
        }

        if (api_.reset_bitstream && bitstream_ptr_) {
            api_.reset_bitstream(bitstream_ptr_);
        }

        output.config = config_;
        output.pts_us = frame.pts_us;
        return OK;
    }

    void destroy() noexcept override {
        if (handle_) {
            if (api_.destroy) api_.destroy(handle_);
            handle_ = nullptr;
        }
        bitstream_ptr_ = nullptr;
    }

    bool is_ready() const override { return abi_.ready() && api_.alloc != nullptr; }

private:
    [[maybe_unused]] int process_abi_;
    void* lib_handle_;
    VendorApi api_;
    AbiAccessor abi_;
    void* handle_ = nullptr;
    void* bitstream_ptr_ = nullptr;
    FrameConfig config_;

    Status validate_supported_config(const FrameConfig& cfg) const {
        if (!is_supported_channel_based_config(cfg)) return UNSUPPORTED_MODE;
        if (cfg.channel_config < 0 || cfg.channel_config > 10) return UNSUPPORTED_MODE;
        if (cfg.source_bits != 16) return UNSUPPORTED_MODE;
        if (cfg.samples_per_channel != 1024) return UNSUPPORTED_MODE;
        if (cfg.neural_type > 1) return UNSUPPORTED_MODE;
        if (cfg.payload_bits > SDK_MAX_PAYLOAD_BITS) return UNSUPPORTED_MODE;
        if (static_cast<size_t>(cfg.frame_bytes) > MAX_ADMITTED_FRAME) return UNSUPPORTED_MODE;
        return OK;
    }

    Status validate_crc_and_budget(const EncodedFrame& frame) const {
        if (frame.payload_size > static_cast<size_t>(abi_.payload_capacity_bytes()))
            return INVALID_HEADER;
        return OK;
    }
};

std::unique_ptr<DecoderBackend> create_vendor_backend(int process_abi, void* lib_handle, const VendorApi& api) {
    return std::make_unique<VendorBackend>(process_abi, lib_handle, api);
}

} // namespace avs3a
