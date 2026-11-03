#pragma once

#include "../buffer/buffer.hpp"
#include "../common/wake_channel.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// OUTLINK — исходящая часть TDB.
//
// Архитектурные инварианты:
// - воркеры кладут готовые пакеты в обычную или priority-очередь;
// - priority-очередь всегда вычитывается первой;
// - sender'ы конкурируют за пакеты, собственной очереди у sender'а нет;
// - SendCallback — единственный выход к транспорту, TDB не знает TransportManager;
// - после shutdown sender'ы дочитывают обе очереди.

using SendCallback = std::function<int(OutgoingPacket&&)>;

class OUTLINK {
public:
    OUTLINK() = default;
    ~OUTLINK();

    OUTLINK(const OUTLINK&) = delete;
    OUTLINK& operator=(const OUTLINK&) = delete;

    bool init(size_t senders, size_t queue_capacity = 1024);
    void shutdown();

    void set_sender(SendCallback callback);
    bool push(OutgoingPacket&& packet);

    size_t queue_size() const;
    size_t senders_count() const;
    size_t sent_count() const {
        return sent_.load(std::memory_order_relaxed);
    }
    size_t failed_count() const {
        return failed_.load(std::memory_order_relaxed);
    }
    size_t dropped_count() const {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    void sender_loop();

    RingBuffer<OutgoingPacket> buffer_;
    RingBuffer<OutgoingPacket> priority_buffer_;
    WakeChannel wake_;
    std::vector<std::thread> threads_;

    std::atomic<bool> running_{false};

    std::mutex callback_mutex_;
    SendCallback sender_;

    std::atomic<size_t> sent_{0};
    std::atomic<size_t> failed_{0};
    std::atomic<size_t> dropped_{0};
};
