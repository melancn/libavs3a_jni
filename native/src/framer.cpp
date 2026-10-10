#include "avs3a/framer.h"
#include "avs3a/timeline.h"
#include <cstring>
#include <algorithm>

namespace avs3a {

static const uint8_t SYNC_BYTE0 = 0xFF;
static const uint8_t SYNC_BYTE1 = 0xF2;

BoundedFramer::BoundedFramer() {}

Status BoundedFramer::queue_input(const uint8_t* data, size_t size, int64_t pts_us,
                                   int64_t epoch, int flags) {
    if (!data && size > 0) return INVALID_ARGUMENT;
    if (size > MAX_INPUT_CHUNK) return INPUT_TOO_LARGE;

    if (input_ended_) return INVALID_STATE;
    if (flags != 0 && flags != 1) return INVALID_ARGUMENT;
    (void)epoch; // Session owns epoch validation; timestamps belong to byte positions.

    if (flags == 1) {
        // Validate this sample, not all previously queued samples. Rejection is atomic.
        HeaderResult hr = parser_.parse_header(ByteSpan(data, size));
        bool exact = hr.kind == HeaderResult::Ready && validate_frame_bytes(hr.frame_bytes)
                     && hr.frame_bytes == size;
        if (!exact) {
            // Preserve the SDK's existing completion-marker convention for a split frame.
            // Complete samples already in the queue must not affect standalone validation.
            if (buffer_.size() + size > QUEUE_LIMIT) return QUEUE_BACKPRESSURE;
            std::vector<uint8_t> combined(buffer_);
            if (size > 0) combined.insert(combined.end(), data, data + size);
            hr = parser_.parse_header(ByteSpan(combined.data(), combined.size()));
            if (hr.kind != HeaderResult::Ready || !validate_frame_bytes(hr.frame_bytes) ||
                hr.frame_bytes != combined.size()) return COMPLETE_SAMPLE_CONTRACT;
        }
    }
    if (buffer_.size() + size > QUEUE_LIMIT) return QUEUE_BACKPRESSURE;
    if (size > 0) {
        if (pts_us != TIME_UNSET) timestamps_.push_back({queued_bytes_, pts_us});
        buffer_.insert(buffer_.end(), data, data + size);
        queued_bytes_ += size;
    }

    return QUEUE_ACCEPTED;
}

Status BoundedFramer::find_sync(size_t& sync_pos, bool& found) {
    found = false;
    if (buffer_.size() < 7) return RECEIVE_NEED_INPUT;

    size_t max_search = buffer_.size() - 1;
    for (size_t i = search_offset_; i < max_search; ++i) {
        if (buffer_[i] == SYNC_BYTE0 && buffer_[i + 1] == SYNC_BYTE1) {
            if (i + 7 <= buffer_.size()) {
                sync_pos = i;
                found = true;
                return OK;
            }
        }
    }

    size_t last_two = std::min(static_cast<size_t>(2), buffer_.size());
    search_offset_ = buffer_.size() - last_two;

    if (input_ended_) {
        if (buffer_.size() > 0 && search_offset_ > MAX_FRAME_BYTES) {
            return RESYNC_LIMIT;
        }
    }
    return RECEIVE_NEED_INPUT;
}

bool BoundedFramer::validate_frame_bytes(size_t frame_bytes) const {
    return frame_bytes >= 11 && frame_bytes <= MAX_FRAME_BYTES;
}

FramerResult BoundedFramer::next() {
    FramerResult result;

    if (buffer_.empty()) {
        result.kind = FramerResult::NeedMore;
        return result;
    }

    size_t sync_pos = 0;
    bool sync_found = false;
    Status sync_status = find_sync(sync_pos, sync_found);

    if (!sync_found) {
        if (is_error(sync_status)) {
            result.kind = FramerResult::Invalid;
            result.error = sync_status;
            return result;
        }
        result.kind = FramerResult::NeedMore;
        if (input_ended_ && buffer_.size() > 0 && buffer_.size() < 7) {
            result.kind = FramerResult::Invalid;
            result.error = TRUNCATED_FRAME;
            result.reason = "incomplete header at EOS";
        }
        return result;
    }

    if (sync_pos > 0) {
        discard_prefix(sync_pos);
        search_offset_ = 0;
    }

    HeaderResult hr = parser_.parse_header(ByteSpan(buffer_.data(), buffer_.size()));
    if (hr.kind == HeaderResult::NeedMore) {
        result.kind = FramerResult::NeedMore;
        result.required_minimum = hr.required_minimum_bytes;
        return result;
    }
    if (hr.kind == HeaderResult::Invalid) {
        if (buffer_.size() > 2) {
            discard_prefix(1);
            search_offset_ = 0;
        }
        if (input_ended_) {
            result.kind = FramerResult::Invalid;
            result.error = TRUNCATED_FRAME;
            result.reason = "invalid header at EOS";
            return result;
        }
        result.kind = FramerResult::Invalid;
        result.error = hr.error;
        result.reason = hr.reason;
        return result;
    }

    if (!validate_frame_bytes(hr.frame_bytes)) {
        result.kind = FramerResult::Invalid;
        result.error = INVALID_HEADER;
        result.reason = "frame bytes out of bounds";
        if (buffer_.size() > 1) {
            discard_prefix(1);
            search_offset_ = 0;
        }
        return result;
    }

    if (buffer_.size() < hr.frame_bytes) {
        result.kind = FramerResult::NeedMore;
        result.required_minimum = hr.frame_bytes;
        return result;
    }

    if (hr.frame_bytes > MAX_ADMITTED_FRAME) {
        result.kind = FramerResult::Invalid;
        result.error = UNSUPPORTED_MODE;
        result.reason = "frame exceeds admitted size";
        discard_prefix(hr.frame_bytes);
        search_offset_ = 0;
        return result;
    }

    uint16_t computed_crc = compute_crc16(buffer_.data() + 7, hr.payload_size);
    if (computed_crc != hr.config.crc) {
        result.kind = FramerResult::Invalid;
        result.error = INVALID_HEADER;
        result.reason = "CRC mismatch";
        discard_prefix(1);
        search_offset_ = 0;
        return result;
    }

    EncodedFrame frame;
    frame.bytes.assign(buffer_.data(), buffer_.data() + hr.frame_bytes);
    frame.config = hr.config;
    frame.payload_offset = hr.payload_offset;
    frame.payload_size = hr.payload_size;
    frame.pts_us = TIME_UNSET;
    while (!timestamps_.empty() && timestamps_.front().offset <= consumed_bytes_) {
        frame.pts_us = timestamps_.front().pts_us;
        timestamps_.pop_front();
    }

    discard_prefix(hr.frame_bytes);
    search_offset_ = 0;
    // A timestamp on a continuation chunk does not migrate to an unrelated frame.
    while (!timestamps_.empty() && timestamps_.front().offset < consumed_bytes_)
        timestamps_.pop_front();

    result.kind = FramerResult::Ready;
    result.frame = std::move(frame);
    result.consumed = hr.frame_bytes;

    return result;
}

void BoundedFramer::discard_prefix(size_t bytes) {
    buffer_.erase(buffer_.begin(), buffer_.begin() + bytes);
    consumed_bytes_ += bytes;
}

void BoundedFramer::signal_end_of_input() {
    input_ended_ = true;
}

bool BoundedFramer::has_nonpadding_tail() const {
    return !buffer_.empty();
}

void BoundedFramer::flush() {
    buffer_.clear();
    search_offset_ = 0;
    input_ended_ = false;
    timestamps_.clear();
    consumed_bytes_ = queued_bytes_ = 0;
}

} // namespace avs3a
