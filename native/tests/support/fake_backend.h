#ifndef AVS3A_FAKE_BACKEND_H
#define AVS3A_FAKE_BACKEND_H

#include "avs3a/backend.h"
#include "avs3a/types.h"
#include <atomic>

namespace avs3a {

class FakeBackend : public DecoderBackend {
public:
    FakeBackend() = default;
    ~FakeBackend() override { destroy(); }

    Status initialize(const FrameConfig& cfg, const VerifiedModelPath& model) override {
        config_ = cfg;
        initialized_ = true;
        destroyed_ = false;
        init_count_++;
        return OK;
    }

    Status decode(const EncodedFrame& frame, OwnedPcm& output) override {
        if (!initialized_) return INVALID_STATE;
        size_t total = static_cast<size_t>(frame.config.samples_per_channel) *
                       static_cast<size_t>(frame.config.channels);
        output.samples.resize(total, 0);
        output.config = frame.config;
        output.pts_us = frame.pts_us;
        decode_count_++;
        return OK;
    }

    void destroy() noexcept override {
        if (!destroyed_) {
            destroy_count_++;
            destroyed_ = true;
        }
        initialized_ = false;
    }

    bool is_ready() const override { return true; }

    int init_count() const { return init_count_; }
    int decode_count() const { return decode_count_; }
    int destroy_count() const { return destroy_count_; }

private:
    FrameConfig config_;
    bool initialized_ = false;
    bool destroyed_ = false;
    std::atomic<int> init_count_{0};
    std::atomic<int> decode_count_{0};
    std::atomic<int> destroy_count_{0};
};

} // namespace avs3a

#endif // AVS3A_FAKE_BACKEND_H
