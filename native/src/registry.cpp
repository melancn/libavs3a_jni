#include "avs3a/registry.h"
#include <limits>

namespace avs3a {

Registry::Registry() {}

int64_t Registry::register_session(std::shared_ptr<Session> session) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (next_id_ <= 0 || next_id_ >= std::numeric_limits<int64_t>::max()) {
        next_id_ = 1;
    }
    while (sessions_.count(next_id_)) {
        ++next_id_;
        if (next_id_ <= 0 || next_id_ >= std::numeric_limits<int64_t>::max())
            next_id_ = 1;
    }
    int64_t id = next_id_++;
    sessions_[id] = std::move(session);
    return id;
}

std::shared_ptr<Session> Registry::find(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return nullptr;
    return it->second;
}

std::shared_ptr<Session> Registry::remove(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return nullptr;
    auto session = it->second;
    sessions_.erase(it);
    return session;
}

bool Registry::exists(int64_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.find(id) != sessions_.end();
}

} // namespace avs3a
