#include "rpc_state.h"

#include <chrono>
#include <utility>

namespace loom_rpc {

uint32_t PendingCalls::next_id() {
    return next_id_.fetch_add(1, std::memory_order_relaxed);
}

void PendingCalls::add(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_[id] = Entry{};
}

void PendingCalls::cancel(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.erase(id);
}

bool PendingCalls::complete(uint32_t id, uint8_t status,
                            std::vector<uint8_t> data) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_.find(id);
    if (it == pending_.end()) return false;

    it->second.done = true;
    it->second.result.status = status;
    it->second.result.data = std::move(data);
    cv_.notify_all();
    return true;
}

bool PendingCalls::wait(uint32_t id, Result& out, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);

    auto it = pending_.find(id);
    if (it == pending_.end()) return false;

    const auto timeout = std::chrono::milliseconds(timeout_ms > 0
                                                       ? timeout_ms
                                                       : 5000);
    const bool ready = cv_.wait_for(lock, timeout, [this, id] {
        auto current = pending_.find(id);
        return current == pending_.end() || current->second.done;
    });

    it = pending_.find(id);
    if (!ready || it == pending_.end()) {
        if (it != pending_.end()) pending_.erase(it);
        return false;
    }

    out = std::move(it->second.result);
    pending_.erase(it);
    return true;
}

void PendingCalls::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : pending_) {
        entry.second.done = true;
        entry.second.result.status = 1; // ERROR
        entry.second.result.data.clear();
    }
    cv_.notify_all();
}

PendingCalls& pending_calls() {
    static PendingCalls instance;
    return instance;
}

} // namespace loom_rpc
