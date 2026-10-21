#include "strategy.hpp"
#include "../worker/worker.hpp"
#include "../buffer/buffer.hpp"

#include <memory>
#include <atomic>

class RoundRobinStrategy : public DistributionStrategy {
public:
    size_t dispatch(const IncomingPacket&,
                    const std::vector<std::unique_ptr<Worker>>& workers) override {
        if (workers.empty()) return 0;
        size_t idx = counter_.fetch_add(1, std::memory_order_relaxed);
        return idx % workers.size();
    }

private:
    std::atomic<size_t> counter_{0};
};