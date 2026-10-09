#ifndef AVS3A_HEADER_PARSER_H
#define AVS3A_HEADER_PARSER_H

#include "types.h"
#include "status.h"
#include <cstdint>

namespace avs3a {

uint16_t compute_crc16(const uint8_t* data, size_t n);

struct HeaderResult {
    enum Kind { NeedMore, Invalid, Ready };
    Kind kind = NeedMore;
    Status error = OK;
    size_t required_minimum_bytes = 0;
    FrameConfig config;
    size_t frame_bytes = 0;
    size_t payload_offset = 0;
    size_t payload_size = 0;
    std::string reason;

    static HeaderResult need_more(size_t min_bytes) {
        HeaderResult r;
        r.kind = NeedMore;
        r.required_minimum_bytes = min_bytes;
        return r;
    }
    static HeaderResult invalid(const char* why) {
        HeaderResult r;
        r.kind = Invalid;
        r.error = INVALID_HEADER;
        r.reason = why ? why : "";
        return r;
    }
    static HeaderResult unsupported(const char* why) {
        HeaderResult r;
        r.kind = Invalid;
        r.error = UNSUPPORTED_MODE;
        r.reason = why ? why : "";
        return r;
    }
    static HeaderResult ready(const FrameConfig& cfg, size_t frame_bytes,
                             size_t payload_offset, size_t payload_size) {
        HeaderResult r;
        r.kind = Ready;
        r.config = cfg;
        r.frame_bytes = frame_bytes;
        r.payload_offset = payload_offset;
        r.payload_size = payload_size;
        return r;
    }
};

class HeaderParser {
public:
    HeaderResult parse_header(ByteSpan bytes) const;
    Status validate_frame(const uint8_t* data, size_t size, FrameConfig& cfg) const;
    bool is_ready() const { return ready_; }
    void set_ready(bool v) { ready_ = v; }
private:
    bool ready_ = true;
};

} // namespace avs3a

#endif // AVS3A_HEADER_PARSER_H
