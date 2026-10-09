#include "avs3a/framer.h"
#include "avs3a/timeline.h"
#include <cstring>
#include <algorithm>
#include <cstdio>

namespace avs3a {

static const uint8_t SYNC_BYTE0 = 0xFF;
static const uint8_t SYNC_BYTE1 = 0xF2;

#ifdef AVS3A_FRAMER_DEBUG
#define FRAMER_DBG(...) fprintf(stderr, __VA_ARGS__)
#else
#define FRAMER_DBG(...) (void)0
#endif

BoundedFramer::BoundedFramer() {}

Status BoundedFramer::queue_input(const uint8_t* data, size_t size, int64_t pts_us,
                                   int64_t epoch, int flags) {
    if (!data && size > 0) return INVALID_ARGUMENT;
    if (size > MAX_INPUT_CHUNK) return INPUT_TOO_LARGE;

    size_t new_total = buffer_.size() + size;
    if (new_total > QUEUE_LIMIT) return QUEUE_BACKPRESSURE;

    if (size > 0) {
        size_t old_size = buffer_.size();
        buffer_.resize(new_total);
        std::memcpy(buffer_.data() + old_size, data, size);
    }

    if (flags == 1) {
        if (buffer_.size() > MAX_FRAME_BYTES)
            return INPUT_TOO_LARGE;
    }

    if (pts_us != TIME_UNSET && size > 0) {
        pending_pts_ = pts_us;
        pending_epoch_ = epoch;
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
        FRAMER_DBG("framer: empty buffer, ended=%d\n", input_ended_);
        result.kind = FramerResult::NeedMore;
        return result;
    }

    size_t sync_pos = 0;
    bool sync_found = false;
    Status sync_status = find_sync(sync_pos, sync_found);

    if (!sync_found) {
        if (is_error(sync_status)) {
            FRAMER_DBG("framer: sync error=%d\n", (int)sync_status);
            result.kind = FramerResult::Invalid;
            result.error = sync_status;
            return result;
        }
        FRAMER_DBG("framer: sync not found, need more (buf=%zu)\n", buffer_.size());
        result.kind = FramerResult::NeedMore;
        if (input_ended_ && buffer_.size() > 0 && buffer_.size() < 7) {
            result.kind = FramerResult::Invalid;
            result.error = TRUNCATED_FRAME;
            result.reason = "incomplete header at EOS";
        }
        return result;
    }

    FRAMER_DBG("framer: sync found at %zu\n", sync_pos);

    if (sync_pos > 0) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + sync_pos);
        search_offset_ = 0;
    }

    HeaderResult hr = parser_.parse_header(ByteSpan(buffer_.data(), buffer_.size()));
    if (hr.kind == HeaderResult::NeedMore) {
        FRAMER_DBG("framer: header need more (%zu bytes, need %zu)\n",
               buffer_.size(), hr.required_minimum_bytes);
        result.kind = FramerResult::NeedMore;
        result.required_minimum = hr.required_minimum_bytes;
        return result;
    }
    if (hr.kind == HeaderResult::Invalid) {
        FRAMER_DBG("framer: header invalid: %s (error=%d)\n",
               hr.reason.c_str(), (int)hr.error);
        if (buffer_.size() > 2) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + 1);
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

    FRAMER_DBG("framer: header ready, frame_bytes=%zu payload_size=%zu\n",
           hr.frame_bytes, hr.payload_size);

    if (!validate_frame_bytes(hr.frame_bytes)) {
        FRAMER_DBG("framer: frame bytes out of bounds (%zu)\n", hr.frame_bytes);
        result.kind = FramerResult::Invalid;
        result.error = INVALID_HEADER;
        result.reason = "frame bytes out of bounds";
        if (buffer_.size() > 1) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + 1);
            search_offset_ = 0;
        }
        return result;
    }

    if (buffer_.size() < hr.frame_bytes) {
        FRAMER_DBG("framer: need more data (have %zu, need %zu)\n",
               buffer_.size(), hr.frame_bytes);
        result.kind = FramerResult::NeedMore;
        result.required_minimum = hr.frame_bytes;
        return result;
    }

    if (hr.frame_bytes > MAX_ADMITTED_FRAME) {
        FRAMER_DBG("framer: frame exceeds admitted size (%zu > %zu)\n",
               hr.frame_bytes, (size_t)MAX_ADMITTED_FRAME);
        result.kind = FramerResult::Invalid;
        result.error = UNSUPPORTED_MODE;
        result.reason = "frame exceeds admitted size";
        buffer_.erase(buffer_.begin(), buffer_.begin() + hr.frame_bytes);
        search_offset_ = 0;
        return result;
    }

    uint16_t computed_crc = compute_crc16(buffer_.data() + 7, hr.payload_size);
    if (computed_crc != hr.config.crc) {
        FRAMER_DBG("framer: CRC mismatch computed=0x%04x stored=0x%04x\n",
               computed_crc, hr.config.crc);
        result.kind = FramerResult::Invalid;
        result.error = INVALID_HEADER;
        result.reason = "CRC mismatch";
        buffer_.erase(buffer_.begin(), buffer_.begin() + 1);
        search_offset_ = 0;
        return result;
    }

    FRAMER_DBG("framer: frame ready, %zu bytes\n", hr.frame_bytes);

    EncodedFrame frame;
    frame.bytes.assign(buffer_.data(), buffer_.data() + hr.frame_bytes);
    frame.config = hr.config;
    frame.payload_offset = hr.payload_offset;
    frame.payload_size = hr.payload_size;
    frame.pts_us = (pending_pts_ != TIME_UNSET) ? pending_pts_ : TIME_UNSET;

    buffer_.erase(buffer_.begin(), buffer_.begin() + hr.frame_bytes);
    search_offset_ = 0;
    pending_pts_ = TIME_UNSET;

    result.kind = FramerResult::Ready;
    result.frame = std::move(frame);
    result.consumed = hr.frame_bytes;

    return result;
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
    pending_pts_ = 0;
    pending_epoch_ = 0;
}

} // namespace avs3a
