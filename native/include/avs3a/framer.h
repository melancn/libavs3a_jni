#ifndef AVS3A_FRAMER_H
#define AVS3A_FRAMER_H

#include "types.h"
#include "status.h"
#include "header_parser.h"
#include <vector>
#include <string>

namespace avs3a {

struct FramerResult {
    enum Kind { NeedMore, Ready, Invalid };
    Kind kind = NeedMore;
    Status error = OK;
    EncodedFrame frame;
    size_t consumed = 0;
    size_t required_minimum = 0;
    std::string reason;
};

class BoundedFramer {
public:
    BoundedFramer();

    Status queue_input(const uint8_t* data, size_t size, int64_t pts_us,
                       int64_t epoch, int flags);

    FramerResult next();

    void signal_end_of_input();
    bool has_nonpadding_tail() const;
    void flush();

    static constexpr size_t MAX_FRAME_BYTES_LIMIT = MAX_FRAME_BYTES;

private:
    std::vector<uint8_t> buffer_;
    size_t search_offset_ = 0;
    bool input_ended_ = false;
    int64_t pending_pts_ = 0;
    int64_t pending_epoch_ = 0;
    HeaderParser parser_;

    Status find_sync(size_t& sync_pos);
    bool validate_frame_bytes(size_t frame_bytes) const;
};

} // namespace avs3a

#endif // AVS3A_FRAMER_H
