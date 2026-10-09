#ifndef AVS3A_REGISTRY_H
#define AVS3A_REGISTRY_H

#include "session.h"
#include <map>
#include <mutex>
#include <memory>

namespace avs3a {

class Registry {
public:
    Registry();

    int64_t register_session(std::shared_ptr<Session> session);
    std::shared_ptr<Session> find(int64_t id);
    std::shared_ptr<Session> remove(int64_t id);
    bool exists(int64_t id) const;

private:
    mutable std::mutex mutex_;
    std::map<int64_t, std::shared_ptr<Session>> sessions_;
    int64_t next_id_ = 1;
};

} // namespace avs3a

#endif // AVS3A_REGISTRY_H
