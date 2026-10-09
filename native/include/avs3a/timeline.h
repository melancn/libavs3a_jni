#ifndef AVS3A_TIMELINE_H
#define AVS3A_TIMELINE_H

#include "types.h"
#include <cstdint>

namespace avs3a {

class Timeline {
public:
    Timeline();
    void set_anchor(int64_t pts_us, int64_t epoch);
    int64_t compute_pts(int64_t frame_index, int32_t sample_rate, int32_t samples_per_channel) const;
    bool has_anchor() const { return has_anchor_; }
    int64_t anchor_pts() const { return anchor_pts_; }
    int64_t anchor_epoch() const { return anchor_epoch_; }
    void flush();
private:
    bool has_anchor_ = false;
    int64_t anchor_pts_ = 0;
    int64_t anchor_epoch_ = 0;
};

} // namespace avs3a

#endif // AVS3A_TIMELINE_H
