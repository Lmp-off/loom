#pragma once
#include <cstddef>
#include <memory>
#include <vector>

class Worker;
struct IncomingPacket;

class DistributionStrategy {
public:
    virtual ~DistributionStrategy() = default;

    // Возвращает [0, workers.size()).
    virtual size_t dispatch(const IncomingPacket& packet,
                            const std::vector<std::unique_ptr<Worker>>& workers) = 0;
};