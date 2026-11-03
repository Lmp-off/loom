#pragma once

#include "../buffer/buffer.hpp"
#include "../common/wake_channel.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace gwp { class GWPPacket; }

// Worker — исполнитель одной задачи TDB.
//
// Архитектурные инварианты:
// - воркер не читает входной Buffer UPLINK, задача приходит через try_submit();
// - у воркера один слот задачи; пока слот занят, try_submit() возвращает false;
// - после освобождения воркер будит общий WakeChannel диспетчера;
// - воркер не вызывает транспорт напрямую, только OutgoingSink;
// - бизнес-логика находится в PacketHandler, а не в Worker.

using OutgoingSink = std::function<bool(OutgoingPacket&&)>;

using PacketHandler = std::function<void(const IncomingPacket&,
                                         gwp::GWPPacket&,
                                         const OutgoingSink&)>;

class Worker {
public:
    Worker(size_t id, WakeChannel* ready_wake);
    ~Worker();

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    uint8_t start();
    uint8_t stop();

    bool try_submit(IncomingPacket&& packet);
    bool is_free() const;

    void set_outgoing_sink(OutgoingSink sink);
    void set_handler(PacketHandler handler);

    size_t id() const { return id_; }
    size_t processed() const { return processed_.load(std::memory_order_relaxed); }
    size_t invalid() const { return invalid_.load(std::memory_order_relaxed); }

private:
    void loop();
    void handle(IncomingPacket& packet);

    size_t id_;
    WakeChannel* ready_wake_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::atomic<size_t> processed_{0};
    std::atomic<size_t> invalid_{0};

    mutable std::mutex task_mutex_;
    std::condition_variable task_cv_;
    IncomingPacket task_;
    bool has_task_{false};
    bool busy_{false};

    std::mutex config_mutex_;
    OutgoingSink outgoing_sink_;
    PacketHandler handler_;
};
