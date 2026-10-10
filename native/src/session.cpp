#include "avs3a/session.h"
#include <cstring>
#include <algorithm>

namespace avs3a {

Session::Session(HandleKind kind, std::unique_ptr<DecoderBackend> backend, std::string model_path)
    : kind_(kind), backend_(std::move(backend)), model_path_(std::move(model_path)) {}

Session::~Session() {
    close();
}

Status Session::fail(Status s) {
    fail_status_ = s;
    state_ = SessionState::FAILED;
    return s;
}

bool Session::return_if_failed_or_closed() {
    auto s = state_.load();
    if (s == SessionState::CLOSED) return true;
    if (s == SessionState::FAILED) return true;
    return false;
}

Status Session::queue_input(const uint8_t* data, size_t size, int64_t pts_us,
                            int64_t epoch, int flags) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (return_if_failed_or_closed())
        return (state_ == SessionState::CLOSED) ? CLOSED_OR_INVALID_HANDLE : fail_status_;
    if (epoch != epoch_ && epoch_ != 0)
        return STALE_EPOCH;
    if (flags != 0 && flags != 1)
        return INVALID_ARGUMENT;
    if (!data && size > 0)
        return INVALID_ARGUMENT;

    return framer_.queue_input(data, size, pts_us, epoch, flags);
}

Status Session::validate_crc_and_budget(const EncodedFrame& frame) const {
    if (frame.payload_size > 12300)
        return INVALID_HEADER;
    if (frame.config.payload_bits > SDK_MAX_PAYLOAD_BITS)
        return UNSUPPORTED_MODE;
    if (static_cast<size_t>(frame.config.frame_bytes) > MAX_ADMITTED_FRAME)
        return UNSUPPORTED_MODE;
    return OK;
}

void Session::validate_pcm_shape(const OwnedPcm& pcm, const FrameConfig& cfg) const {
    size_t expected = static_cast<size_t>(cfg.samples_per_channel) * static_cast<size_t>(cfg.channels);
    if (pcm.samples.size() < expected) return;
}

Status Session::receive(MutableByteSpan output, PcmMetadata& meta) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (return_if_failed_or_closed())
        return (state_ == SessionState::CLOSED) ? CLOSED_OR_INVALID_HANDLE : fail_status_;

    if (!pending_) {
        FramerResult fr = framer_.next();

        if (fr.kind == FramerResult::Invalid) {
            return fail(is_error(fr.error) ? fr.error : INVALID_HEADER);
        }

        if (fr.kind == FramerResult::NeedMore) {
            if (!input_ended_)
                return RECEIVE_NEED_INPUT;
            if (framer_.has_nonpadding_tail())
                return fail(TRUNCATED_FRAME);
            state_ = SessionState::ENDED;
            return RECEIVE_END_OF_STREAM;
        }

        auto& frame = fr.frame;

        if (!is_channel_based_mono_or_stereo(frame.config))
            return fail(UNSUPPORTED_MODE);

        Status crc_status = validate_crc_and_budget(frame);
        if (is_error(crc_status))
            return fail(crc_status);

        if (!config_) {
            if (kind_ == HandleKind::PARSER)
                return fail(INVALID_STATE);
            if (!backend_ || !backend_->is_ready())
                return fail(VENDOR_ABI_NOT_READY);

            VerifiedModelPath model;
            model.absolute_path = model_path_;
            Status init_status = backend_->initialize(frame.config, model);
            if (is_error(init_status))
                return fail(init_status);
            config_ = frame.config;
        } else if (!same_codec_configuration(*config_, frame.config)) {
            return fail(UNSUPPORTED_CONFIG_CHANGE);
        }

        if (!timeline_.has_anchor() && frame.pts_us != TIME_UNSET) {
            timeline_.set_anchor(frame.pts_us, epoch_);
        }

        int64_t computed_pts = timeline_.compute_pts(frame_index_, frame.config.sample_rate,
                                                      frame.config.samples_per_channel);
        frame.pts_us = (computed_pts != TIME_UNSET) ? computed_pts : frame.pts_us;

        auto pcm = std::make_unique<OwnedPcm>();
        Status decode_status = backend_->decode(frame, *pcm);
        if (is_error(decode_status))
            return fail(decode_status);

        validate_pcm_shape(*pcm, frame.config);
        pcm->config = frame.config;
        pcm->pts_us = frame.pts_us;
        pcm->epoch = epoch_;
        pending_ = std::move(pcm);
        ++frame_index_;
    }

    size_t bytes_needed = static_cast<size_t>(pending_->config.samples_per_channel) *
                          static_cast<size_t>(pending_->config.channels) * 2;
    if (output.size < bytes_needed) {
        meta.pts_us = pending_->pts_us;
        meta.sample_rate = pending_->config.sample_rate;
        meta.channels = pending_->config.channels;
        meta.samples_per_channel = pending_->config.samples_per_channel;
        meta.byte_count = static_cast<int64_t>(bytes_needed);
        meta.layout_id = (pending_->config.channels == 1) ? 1 : 2;
        meta.flags = 0;
        meta.epoch = pending_->epoch;
        return RECEIVE_OUTPUT_TOO_SMALL;
    }

    size_t copied = std::min(bytes_needed, pending_->samples.size() * 2);
    std::memcpy(output.data, pending_->samples.data(), copied);

    meta.pts_us = pending_->pts_us;
    meta.sample_rate = pending_->config.sample_rate;
    meta.channels = pending_->config.channels;
    meta.samples_per_channel = pending_->config.samples_per_channel;
    meta.byte_count = static_cast<int64_t>(copied);
    meta.layout_id = (pending_->config.channels == 1) ? 1 : 2;
    meta.flags = 0;
    meta.epoch = pending_->epoch;

    pending_.reset();
    return RECEIVE_READY;
}

Status Session::receive_frame(MutableByteSpan output, FrameMetadata& meta) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (return_if_failed_or_closed())
        return (state_ == SessionState::CLOSED) ? CLOSED_OR_INVALID_HANDLE : fail_status_;

    if (!pending_frame_) {
        FramerResult fr = framer_.next();

        if (fr.kind == FramerResult::Invalid) {
            return fail(is_error(fr.error) ? fr.error : INVALID_HEADER);
        }

        if (fr.kind == FramerResult::NeedMore) {
            if (!input_ended_)
                return RECEIVE_NEED_INPUT;
            if (framer_.has_nonpadding_tail())
                return fail(TRUNCATED_FRAME);
            state_ = SessionState::ENDED;
            return RECEIVE_END_OF_STREAM;
        }

        auto& frame = fr.frame;

        if (!is_channel_based_mono_or_stereo(frame.config))
            return fail(UNSUPPORTED_MODE);

        Status crc_status = validate_crc_and_budget(frame);
        if (is_error(crc_status))
            return fail(crc_status);

        if (!timeline_.has_anchor() && frame.pts_us != TIME_UNSET) {
            timeline_.set_anchor(frame.pts_us, epoch_);
        }

        int64_t computed_pts = timeline_.compute_pts(frame_index_, frame.config.sample_rate,
                                                      frame.config.samples_per_channel);
        frame.pts_us = (computed_pts != TIME_UNSET) ? computed_pts : frame.pts_us;

        pending_frame_ = std::make_unique<EncodedFrame>(std::move(frame));
        ++frame_index_;
    }

    size_t needed = static_cast<size_t>(pending_frame_->config.frame_bytes);
    if (output.size < needed) {
        meta.pts_us = pending_frame_->pts_us;
        meta.sample_rate = pending_frame_->config.sample_rate;
        meta.channels = pending_frame_->config.channels;
        meta.samples_per_channel = pending_frame_->config.samples_per_channel;
        meta.frame_bytes = static_cast<int64_t>(needed);
        meta.payload_offset = pending_frame_->payload_offset;
        meta.payload_bytes = static_cast<int64_t>(pending_frame_->payload_size);
        meta.bitrate_bps = pending_frame_->config.bitrate;
        meta.channel_mode = static_cast<int64_t>(pending_frame_->config.mode);
        meta.epoch = epoch_;
        return RECEIVE_OUTPUT_TOO_SMALL;
    }

    std::memcpy(output.data, pending_frame_->bytes.data(),
                std::min(needed, pending_frame_->bytes.size()));

    meta.pts_us = pending_frame_->pts_us;
    meta.sample_rate = pending_frame_->config.sample_rate;
    meta.channels = pending_frame_->config.channels;
    meta.samples_per_channel = pending_frame_->config.samples_per_channel;
    meta.frame_bytes = static_cast<int64_t>(needed);
    meta.payload_offset = pending_frame_->payload_offset;
    meta.payload_bytes = static_cast<int64_t>(pending_frame_->payload_size);
    meta.bitrate_bps = pending_frame_->config.bitrate;
    meta.channel_mode = static_cast<int64_t>(pending_frame_->config.mode);
    meta.epoch = epoch_;

    pending_frame_.reset();
    return RECEIVE_READY;
}

Status Session::end_input() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (return_if_failed_or_closed())
        return (state_ == SessionState::CLOSED) ? CLOSED_OR_INVALID_HANDLE : fail_status_;
    input_ended_ = true;
    return OK;
}

Status Session::flush(int64_t new_epoch) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == SessionState::CLOSED)
        return CLOSED_OR_INVALID_HANDLE;
    if (new_epoch <= epoch_ && epoch_ != 0)
        return STALE_EPOCH;

    if (backend_) {
        backend_->destroy();
    }
    framer_.flush();
    timeline_.flush();
    pending_.reset();
    pending_frame_.reset();
    input_ended_ = false;
    epoch_ = new_epoch;
    frame_index_ = 0;
    config_.reset();
    state_ = SessionState::WAITING_HEADER;
    return OK;
}

void Session::close() {
    auto expected = SessionState::CLOSED;
    auto prev = state_.exchange(expected);
    if (prev == SessionState::CLOSED) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (backend_) {
        backend_->destroy();
    }
    pending_.reset();
    pending_frame_.reset();
    config_.reset();
}

bool Session::is_closed() const {
    return state_.load() == SessionState::CLOSED;
}

} // namespace avs3a
