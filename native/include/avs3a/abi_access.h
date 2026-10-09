#ifndef AVS3A_ABI_ACCESS_H
#define AVS3A_ABI_ACCESS_H

#include "avs3a/types.h"
#include "avs3a/status.h"
#include <cstdint>
#include <cstring>

namespace avs3a {

class AbiAccessor {
public:
    AbiAccessor(int32_t pointer_bytes, int32_t state_bytes, bool ready)
        : pointer_bytes_(pointer_bytes), state_bytes_(state_bytes), ready_(ready) {}

    bool ready() const { return ready_; }
    int32_t pointer_bytes() const { return pointer_bytes_; }
    int32_t state_bytes() const { return state_bytes_; }

    bool check_offset(size_t offset, size_t size) const {
        return offset + size <= static_cast<size_t>(state_bytes_) &&
               offset + size >= offset;
    }

    void write_int32(uint8_t* base, size_t offset, int32_t value) const {
        if (!check_offset(offset, 4)) return;
        std::memcpy(base + offset, &value, 4);
    }

    void write_int16(uint8_t* base, size_t offset, int16_t value) const {
        if (!check_offset(offset, 2)) return;
        std::memcpy(base + offset, &value, 2);
    }

    int32_t read_int32(const uint8_t* base, size_t offset) const {
        if (!check_offset(offset, 4)) return 0;
        int32_t v;
        std::memcpy(&v, base + offset, 4);
        return v;
    }

    int16_t read_int16(const uint8_t* base, size_t offset) const {
        if (!check_offset(offset, 2)) return 0;
        int16_t v;
        std::memcpy(&v, base + offset, 2);
        return v;
    }

    Status write_verified_initial_fields(uint8_t* handle, const FrameConfig& cfg) const {
        if (!ready_) return VENDOR_ABI_NOT_READY;
        if (!handle) return INVALID_ARGUMENT;

        write_int32(handle, 4, cfg.sample_rate);
        write_int16(handle, 8, 16);
        write_int32(handle, 12, cfg.bitrate);
        write_int32(handle, 16, cfg.bitrate);
        write_int32(handle, 20, cfg.channel_config);
        write_int16(handle, 24, static_cast<int16_t>(cfg.channels));
        write_int16(handle, 26, 0);
        write_int32(handle, 28, 0);
        write_int32(handle, 32, 0);
        write_int16(handle, 36, 0);
        write_int16(handle, 38, 0);
        write_int16(handle, 40, 0);
        write_int16(handle, 42, static_cast<int16_t>(cfg.channel_config));
        write_int16(handle, 44, 0);
        write_int16(handle, 46, 0);
        write_int16(handle, 48, 1024);
        write_int32(handle, 52, cfg.payload_bits);
        write_int32(handle, 56, cfg.neural_type);
        write_int32(handle, 60, 1);

        return OK;
    }

    size_t verified_bitstream_span_offset() const {
        return static_cast<size_t>(pointer_bytes_ == 8 ? 80 : 72);
    }

    int32_t bitstream_state_bytes() const { return 12304; }
    int32_t payload_capacity_bytes() const { return 12300; }

private:
    int32_t pointer_bytes_;
    int32_t state_bytes_;
    bool ready_;
};

inline AbiAccessor create_abi_accessor(int process_abi) {
    if (process_abi == 1) return AbiAccessor(8, 264, true);
    if (process_abi == 2) return AbiAccessor(4, 164, true);
    return AbiAccessor(0, 0, false);
}

} // namespace avs3a

#endif // AVS3A_ABI_ACCESS_H
