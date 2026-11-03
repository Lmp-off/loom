#include "strategy.hpp"

#include "../worker/worker.hpp"

size_t RoundRobinStrategy::dispatch(
    const IncomingPacket&,
    const std::vector<std::unique_ptr<Worker>>& workers) {
    const size_t count = workers.size();
    if (count == 0) return npos;

    const size_t start = counter_.fetch_add(1, std::memory_order_relaxed);
    for (size_t offset = 0; offset < count; ++offset) {
        const size_t index = (start + offset) % count;
        if (workers[index] && workers[index]->is_free()) {
            return index;
        }
    }

    return npos;
}
