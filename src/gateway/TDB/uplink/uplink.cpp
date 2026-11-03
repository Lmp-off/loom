#include "uplink.hpp"

#include "../../../../metalogger/metalogger.h"

#include <chrono>
#include <exception>
#include <string>
#include <utility>

UPLINK::UPLINK() = default;

UPLINK::~UPLINK() {
    shutdown();
}

bool UPLINK::init(size_t workers, size_t queue_capacity,
                  std::unique_ptr<DistributionStrategy> strategy,
                  size_t max_direct_workers) {
    shutdown();

    if (workers == 0) {
        METALOG_ERROR("UPLINK: workers count must be > 0");
        return false;
    }

    if (!buffer_.init(queue_capacity)) {
        METALOG_ERROR("UPLINK: failed to init ingress buffer");
        return false;
    }

    if (!priority_buffer_.init(queue_capacity)) {
        METALOG_ERROR("UPLINK: failed to init priority buffer");
        buffer_.shutdown();
        return false;
    }

    max_direct_workers_ =
        (max_direct_workers == kAutoDirectWorkers) ? workers
                                                   : max_direct_workers;

    if (!strategy) {
        strategy = std::make_unique<RoundRobinStrategy>();
    }
    strategy_ = std::move(strategy);
    dropped_.store(0, std::memory_order_relaxed);
    dispatched_.store(0, std::memory_order_relaxed);

    workers_.reserve(workers);
    for (size_t i = 0; i < workers; ++i) {
        auto worker = std::make_unique<Worker>(i, &dispatcher_wake_);
        if (worker->start() != 0) {
            METALOG_ERROR("UPLINK: failed to start worker " + std::to_string(i));
            shutdown();
            return false;
        }
        workers_.push_back(std::move(worker));
    }

    running_.store(true, std::memory_order_release);
    try {
        dispatcher_ = std::thread(&UPLINK::dispatcher_loop, this);
    } catch (const std::exception& e) {
        running_.store(false, std::memory_order_release);
        METALOG_ERROR(std::string("UPLINK: failed to start dispatcher: ") +
                      e.what());
        shutdown();
        return false;
    }

    METALOG_INFO("UPLINK initialized with " + std::to_string(workers) +
                 " workers, ingress buffer capacity " +
                 std::to_string(buffer_.capacity()) +
                 ", max direct workers " +
                 std::to_string(max_direct_workers_));
    return true;
}

void UPLINK::shutdown() {
    const bool was_running = running_.exchange(false, std::memory_order_acq_rel);
    dispatcher_wake_.notify_all();

    if (dispatcher_.joinable()) dispatcher_.join();

    for (auto& worker : workers_) {
        worker->stop();
    }
    workers_.clear();

    {
        std::lock_guard<std::mutex> lock(direct_mutex_);
        for (auto& entry : direct_workers_) {
            entry.second->stop();
        }
        direct_workers_.clear();
    }

    buffer_.shutdown();
    priority_buffer_.shutdown();
    strategy_.reset();

    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        handler_ = nullptr;
        outgoing_sink_ = nullptr;
    }

    if (was_running) {
        METALOG_INFO("UPLINK shutdown");
    }
}

void UPLINK::push(IncomingPacket&& packet) {
    if (!running_.load(std::memory_order_acquire) || workers_.empty()) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        METALOG_WARNING("UPLINK: not running, packet dropped");
        return;
    }

    const bool priority = packet.priority;
    Buffer& target = priority ? priority_buffer_ : buffer_;

    if (!target.push(std::move(packet))) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        METALOG_WARNING(priority
                            ? "UPLINK: priority buffer is full, packet dropped"
                            : "UPLINK: ingress buffer is full, packet dropped");
        return;
    }

    dispatcher_wake_.notify_one();
}

bool UPLINK::pop_next(IncomingPacket& packet) {
    // Direct/priority трафик всегда разбирается первым.
    if (priority_buffer_.pop(packet)) return true;
    return buffer_.pop(packet);
}

void UPLINK::dispatch_packet(IncomingPacket&& packet) {
    const bool priority = packet.priority;
    const std::string module = packet.client_address;

    while (true) {
        if (priority) {
            Worker* dedicated = ensure_direct_worker(module);
            if (dedicated != nullptr &&
                dedicated->try_submit(std::move(packet))) {
                dispatched_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }

        const size_t index =
            strategy_ ? strategy_->dispatch(packet, workers_)
                      : DistributionStrategy::npos;

        if (index < workers_.size() && workers_[index] &&
            workers_[index]->try_submit(std::move(packet))) {
            dispatched_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        std::unique_lock<std::mutex> lock(dispatcher_wake_.mutex);
        dispatcher_wake_.cv.wait_for(lock, std::chrono::milliseconds(1));
    }
}

Worker* UPLINK::ensure_direct_worker(const std::string& module) {
    if (module.empty()) return nullptr;

    {
        std::lock_guard<std::mutex> lock(direct_mutex_);
        auto it = direct_workers_.find(module);
        if (it != direct_workers_.end()) return it->second.get();
        if (direct_workers_.size() >= max_direct_workers_) return nullptr;
    }

    PacketHandler handler;
    OutgoingSink sink;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        handler = handler_;
        sink = outgoing_sink_;
    }

    size_t id = 0;
    {
        std::lock_guard<std::mutex> lock(direct_mutex_);
        id = workers_.size() + direct_workers_.size();
    }

    auto worker = std::make_unique<Worker>(id, &dispatcher_wake_);
    worker->set_handler(handler);
    worker->set_outgoing_sink(sink);

    if (worker->start() != 0) {
        METALOG_ERROR("UPLINK: failed to start direct worker for " + module);
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(direct_mutex_);
    auto inserted = direct_workers_.emplace(module, std::move(worker));
    METALOG_INFO("UPLINK: direct worker created for module " + module);
    return inserted.first->second.get();
}

void UPLINK::dispatcher_loop() {
    while (true) {
        IncomingPacket packet;
        if (!pop_next(packet)) {
            if (!running_.load(std::memory_order_acquire)) break;

            std::unique_lock<std::mutex> lock(dispatcher_wake_.mutex);
            dispatcher_wake_.cv.wait_for(lock, std::chrono::milliseconds(1));
            continue;
        }

        dispatch_packet(std::move(packet));
    }
}

void UPLINK::set_outgoing_sink(OutgoingSink sink) {
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        outgoing_sink_ = sink;
    }

    for (auto& worker : workers_) {
        worker->set_outgoing_sink(sink);
    }

    std::lock_guard<std::mutex> lock(direct_mutex_);
    for (auto& entry : direct_workers_) {
        entry.second->set_outgoing_sink(sink);
    }
}

void UPLINK::set_handler(PacketHandler handler) {
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        handler_ = handler;
    }

    for (auto& worker : workers_) {
        worker->set_handler(handler);
    }

    std::lock_guard<std::mutex> lock(direct_mutex_);
    for (auto& entry : direct_workers_) {
        entry.second->set_handler(handler);
    }
}

size_t UPLINK::queue_size() const {
    return buffer_.size();
}

size_t UPLINK::priority_queue_size() const {
    return priority_buffer_.size();
}

size_t UPLINK::workers_count() const {
    return workers_.size();
}

size_t UPLINK::direct_workers_count() const {
    std::lock_guard<std::mutex> lock(direct_mutex_);
    return direct_workers_.size();
}

size_t UPLINK::processed_count() const {
    size_t total = 0;
    for (const auto& worker : workers_) {
        total += worker->processed();
    }

    {
        std::lock_guard<std::mutex> lock(direct_mutex_);
        for (const auto& entry : direct_workers_) {
            total += entry.second->processed();
        }
    }
    return total;
}

size_t UPLINK::invalid_count() const {
    size_t total = 0;
    for (const auto& worker : workers_) {
        total += worker->invalid();
    }

    {
        std::lock_guard<std::mutex> lock(direct_mutex_);
        for (const auto& entry : direct_workers_) {
            total += entry.second->invalid();
        }
    }
    return total;
}

size_t UPLINK::worker_processed(size_t index) const {
    if (index >= workers_.size()) return 0;
    return workers_[index]->processed();
}

size_t UPLINK::direct_worker_processed(const std::string& module) const {
    std::lock_guard<std::mutex> lock(direct_mutex_);
    auto it = direct_workers_.find(module);
    return it == direct_workers_.end() ? 0 : it->second->processed();
}
