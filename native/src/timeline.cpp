#include "avs3a/timeline.h"
#include <limits>

namespace avs3a {

Timeline::Timeline() {}

void Timeline::set_anchor(int64_t pts_us, int64_t epoch) {
    anchor_pts_ = pts_us;
    anchor_epoch_ = epoch;
    has_anchor_ = true;
}

int64_t Timeline::compute_pts(int64_t frame_index, int32_t sample_rate, int32_t samples_per_channel) const {
    if (!has_anchor_) return TIME_UNSET;
    if (sample_rate <= 0 || samples_per_channel <= 0) return TIME_UNSET;

    int64_t total_samples = frame_index * static_cast<int64_t>(samples_per_channel);
    int64_t frame_duration_us = total_samples * 1000000LL / static_cast<int64_t>(sample_rate);
    return anchor_pts_ + frame_duration_us;
}

void Timeline::flush() {
    has_anchor_ = false;
    anchor_pts_ = 0;
    anchor_epoch_ = 0;
}

} // namespace avs3a
