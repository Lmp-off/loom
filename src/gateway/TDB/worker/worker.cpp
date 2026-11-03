#include "worker.hpp"

#include "../buffer/buffer.hpp"
#include "../../../protocol/gwp.h"
#include "../../../../metalogger/metalogger.h"

#include <exception>
#include <string>
#include <utility>

namespace {
const char* packet_type_name(gwp::PacketType type) {
    switch (type) {
        case gwp::PacketType::REQUEST:    return "REQUEST";
        case gwp::PacketType::RESPONSE:   return "RESPONSE";
        case gwp::PacketType::ASYNC_DATA: return "ASYNC_DATA";
        case gwp::PacketType::HEARTBEAT:  return "HEARTBEAT";
        case gwp::PacketType::CLOSE:      return "CLOSE";
    }
    return "UNKNOWN";
}
}

Worker::Worker(size_t id, WakeChannel* ready_wake)
    : id_(id), ready_wake_(ready_wake) {}

Worker::~Worker() {
    stop();
}

uint8_t Worker::start() {
    if (running_.load(std::memory_order_acquire)) return 0;
    if (thread_.joinable()) return 1;

    {
        std::lock_guard<std::mutex> lock(task_mutex_);
        has_task_ = false;
        busy_ = false;
    }

    running_.store(true, std::memory_order_release);
    try {
        thread_ = std::thread(&Worker::loop, this);
    } catch (const std::exception& e) {
        running_.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            has_task_ = false;
        }
        task_cv_.notify_all();
        METALOG_ERROR("[TDB] worker " + std::to_string(id_) +
                      " start failed: " + e.what());
        return 1;
    }
    return 0;
}

uint8_t Worker::stop() {
    running_.store(false, std::memory_order_release);
    task_cv_.notify_all();

    if (thread_.joinable()) thread_.join();
    return 0;
}

bool Worker::try_submit(IncomingPacket&& packet) {
    {
        std::lock_guard<std::mutex> lock(task_mutex_);
        if (!running_.load(std::memory_order_acquire) || busy_ || has_task_) {
            return false;
        }

        task_ = std::move(packet);
        has_task_ = true;
    }

    task_cv_.notify_one();
    return true;
}

bool Worker::is_free() const {
    std::lock_guard<std::mutex> lock(task_mutex_);
    return running_.load(std::memory_order_acquire) && !busy_ && !has_task_;
}

void Worker::set_outgoing_sink(OutgoingSink sink) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    outgoing_sink_ = std::move(sink);
}

void Worker::set_handler(PacketHandler handler) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    handler_ = std::move(handler);
}

void Worker::loop() {
    while (true) {
        IncomingPacket packet;
        {
            std::unique_lock<std::mutex> lock(task_mutex_);
            task_cv_.wait(lock, [this] {
                return has_task_ || !running_.load(std::memory_order_acquire);
            });

            if (!has_task_) break;

            packet = std::move(task_);
            has_task_ = false;
            busy_ = true;
        }

        handle(packet);

        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            busy_ = false;
        }

        if (ready_wake_ != nullptr) {
            ready_wake_->notify_one();
        }
    }
}

void Worker::handle(IncomingPacket& packet) {
    gwp::GWPPacket gwp_packet;
    if (packet.data.empty() ||
        !gwp_packet.deserialize(packet.data.data(), packet.data.size())) {
        invalid_.fetch_add(1, std::memory_order_relaxed);
        METALOG_WARNING("[TDB] worker " + std::to_string(id_) +
                        ": invalid GWP frame from " + packet.client_address +
                        " (" + std::to_string(packet.data.size()) + " bytes)");
        return;
    }

    processed_.fetch_add(1, std::memory_order_relaxed);

    METALOG_DEBUG("[TDB] worker " + std::to_string(id_) +
                  " packet id=" + std::to_string(gwp_packet.get_id()) +
                  " type=" + packet_type_name(gwp_packet.get_type()) +
                  " method=" + gwp_packet.get_method() +
                  " bytes=" + std::to_string(gwp_packet.get_data_size()));

    PacketHandler handler;
    OutgoingSink sink;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        handler = handler_;
        sink = outgoing_sink_;
    }

    if (!handler) return;

    if (!sink) {
        METALOG_WARNING("[TDB] worker " + std::to_string(id_) +
                        ": handler is set, but outgoing sink is empty");
        return;
    }

    try {
        handler(packet, gwp_packet, sink);
    } catch (const std::exception& e) {
        METALOG_ERROR("[TDB] worker " + std::to_string(id_) +
                      " handler failed: " + e.what());
    } catch (...) {
        METALOG_ERROR("[TDB] worker " + std::to_string(id_) +
                      " handler failed with unknown exception");
    }
}
