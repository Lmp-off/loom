#pragma once

#include "../buffer/buffer.hpp"
#include "../common/wake_channel.hpp"
#include "../strategy/strategy.hpp"
#include "../worker/worker.hpp"

#include <atomic>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// UPLINK — входящая часть TDB.
//
// Архитектурные инварианты:
// - Buffer — буфер перегрузки, а не очередь задач воркеров;
// - единственный потребитель обоих Buffer — диспетчер UPLINK;
// - диспетчер выбирает воркера через DistributionStrategy;
// - priority Buffer всегда вычитывается раньше обычного;
// - для priority-модуля создаётся выделенный Worker, пока не исчерпан
//   max_direct_workers;
// - если выделенного Worker нет или он занят, priority-пакет берёт свободный
//   Worker общего пула;
// - воркеры не читают Buffer и не конкурируют за задачи.
//
// max_direct_workers == kAutoDirectWorkers означает «столько же, сколько
// обычных воркеров»; 0 отключает выделенные воркеры.

class UPLINK {
public:
    static constexpr size_t kAutoDirectWorkers =
        std::numeric_limits<size_t>::max();

    UPLINK();
    ~UPLINK();

    UPLINK(const UPLINK&) = delete;
    UPLINK& operator=(const UPLINK&) = delete;

    bool init(size_t workers, size_t queue_capacity = 1024,
              std::unique_ptr<DistributionStrategy> strategy = nullptr,
              size_t max_direct_workers = kAutoDirectWorkers);
    void shutdown();

    void push(IncomingPacket&& packet);

    void set_outgoing_sink(OutgoingSink sink);
    void set_handler(PacketHandler handler);

    size_t queue_size() const;
    size_t priority_queue_size() const;
    size_t workers_count() const;
    size_t direct_workers_count() const;
    size_t processed_count() const;
    size_t invalid_count() const;
    size_t dropped_count() const {
        return dropped_.load(std::memory_order_relaxed);
    }
    size_t dispatched_count() const {
        return dispatched_.load(std::memory_order_relaxed);
    }
    size_t worker_processed(size_t index) const;
    size_t direct_worker_processed(const std::string& module) const;

private:
    void dispatcher_loop();
    bool pop_next(IncomingPacket& packet);
    void dispatch_packet(IncomingPacket&& packet);
    Worker* ensure_direct_worker(const std::string& module);

    Buffer buffer_;
    Buffer priority_buffer_;
    WakeChannel dispatcher_wake_;
    std::vector<std::unique_ptr<Worker>> workers_;

    mutable std::mutex direct_mutex_;
    std::map<std::string, std::unique_ptr<Worker>> direct_workers_;
    size_t max_direct_workers_ = 0;

    std::unique_ptr<DistributionStrategy> strategy_;
    std::thread dispatcher_;

    std::mutex config_mutex_;
    PacketHandler handler_;
    OutgoingSink outgoing_sink_;

    std::atomic<bool> running_{false};
    std::atomic<size_t> dropped_{0};
    std::atomic<size_t> dispatched_{0};
};
