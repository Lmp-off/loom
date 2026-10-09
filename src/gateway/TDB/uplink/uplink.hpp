#pragma once

#include "../buffer/buffer.hpp"
#include "../worker/worker.hpp"
#include "../strategy/strategy.hpp"
#include <memory>
#include <vector>

class UPLINK {
public:
    UPLINK();
    ~UPLINK();

    bool init(size_t workers, DistributionStrategy* strategy);
    void shutdown();

    void push(IncomingPacket&& packet);

    size_t queue_size() const;
    size_t workers_count() const;

private:
    Buffer buffer_;
    std::vector<std::unique_ptr<Worker>> workers_;
    DistributionStrategy* strategy_;
    std::atomic<bool> running_{false};
};