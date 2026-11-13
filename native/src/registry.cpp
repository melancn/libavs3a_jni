#include "avs3a/registry.h"
#include <limits>

namespace avs3a {

Registry::Registry() {}

int64_t Registry::register_session(std::shared_ptr<Session> session) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Never wrap or reuse historical IDs; report exhaustion to the caller.
    if (next_id_ <= 0 || next_id_ >= std::numeric_limits<int64_t>::max())
        return 0;
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

std::shared_ptr<Session> Registry::remove_kind(int64_t id, HandleKind kind) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return nullptr;
    if (it->second->kind() != kind) return nullptr;
    auto session = it->second;
    sessions_.erase(it);
    return session;
}

bool Registry::exists(int64_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.find(id) != sessions_.end();
}

} // namespace avs3a
