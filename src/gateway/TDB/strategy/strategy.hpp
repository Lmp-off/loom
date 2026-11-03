#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

class Worker;
struct IncomingPacket;

// DistributionStrategy — политика выбора воркера.
//
// Архитектурные инварианты:
// - вызывается только диспетчером UPLINK;
// - возвращает индекс свободного воркера или npos;
// - не читает Buffer и не передаёт задачи сама;
// - RoundRobinStrategy — политика по умолчанию.

class DistributionStrategy {
public:
    static constexpr size_t npos = static_cast<size_t>(-1);

    virtual ~DistributionStrategy() = default;

    virtual size_t dispatch(
        const IncomingPacket& packet,
        const std::vector<std::unique_ptr<Worker>>& workers) = 0;
};

class RoundRobinStrategy final : public DistributionStrategy {
public:
    size_t dispatch(
        const IncomingPacket& packet,
        const std::vector<std::unique_ptr<Worker>>& workers) override;

private:
    std::atomic<size_t> counter_{0};
};
