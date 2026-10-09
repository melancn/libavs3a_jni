#ifndef AVS3A_BACKEND_H
#define AVS3A_BACKEND_H

#include "types.h"
#include "status.h"
#include <string>
#include <cstdio>

namespace avs3a {

struct VerifiedModelPath {
    std::string absolute_path;
    std::string vendor_id;
    std::string sha256;
    bool check_still_readable() const {
        if (absolute_path.empty()) return true;
        FILE* f = std::fopen(absolute_path.c_str(), "rb");
        if (!f) return false;
        std::fclose(f);
        return true;
    }
};

class DecoderBackend {
public:
    virtual ~DecoderBackend() = default;
    virtual Status initialize(const FrameConfig& cfg, const VerifiedModelPath& model) = 0;
    virtual Status decode(const EncodedFrame& frame, OwnedPcm& output) = 0;
    virtual void destroy() noexcept = 0;
    virtual bool is_ready() const = 0;
};

} // namespace avs3a

#endif // AVS3A_BACKEND_H
