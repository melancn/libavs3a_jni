#ifndef AVS3A_SESSION_H
#define AVS3A_SESSION_H

#include "types.h"
#include "status.h"
#include "framer.h"
#include "timeline.h"
#include "backend.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <optional>

namespace avs3a {

enum class SessionState : int32_t {
    WAITING_HEADER = 0,
    READY,
    DRAINING,
    ENDED,
    FAILED,
    CLOSED,
};

enum class HandleKind : int32_t {
    DECODER = 1,
    PARSER  = 2,
};

class Session {
public:
    Session(HandleKind kind, std::unique_ptr<DecoderBackend> backend,
            std::string model_path = std::string());
    ~Session();

    Status queue_input(const uint8_t* data, size_t size, int64_t pts_us,
                       int64_t epoch, int flags);
    Status receive(MutableByteSpan output, PcmMetadata& meta);
    Status receive_frame(MutableByteSpan output, FrameMetadata& meta);
    Status end_input();
    Status flush(int64_t new_epoch);
    void close();

    HandleKind kind() const { return kind_; }
    bool is_closed() const;
    bool is_parser() const { return kind_ == HandleKind::PARSER; }
    int64_t epoch() const { return epoch_; }
    void set_epoch(int64_t e) { epoch_ = e; }

    std::mutex& mutex() { return mutex_; }

private:
    mutable std::mutex mutex_;
    HandleKind kind_;
    int64_t epoch_ = 0;
    std::atomic<SessionState> state_{SessionState::WAITING_HEADER};
    Status fail_status_ = OK;
    BoundedFramer framer_;
    Timeline timeline_;
    std::unique_ptr<DecoderBackend> backend_;
    std::string model_path_;
    std::unique_ptr<OwnedPcm> pending_;
    std::unique_ptr<EncodedFrame> pending_frame_;
    bool input_ended_ = false;
    int64_t frame_index_ = 0;
    int64_t anchor_frame_index_ = 0;
    std::optional<FrameConfig> config_;

    Status fail(Status s);
    bool return_if_failed_or_closed();
    Status validate_crc_and_budget(const EncodedFrame& frame) const;
    Status validate_pcm_shape(const OwnedPcm& pcm, const FrameConfig& cfg) const;
};

} // namespace avs3a

#endif // AVS3A_SESSION_H
