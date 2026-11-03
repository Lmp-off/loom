#include "outlink.hpp"

#include "../../../../metalogger/metalogger.h"

#include <chrono>
#include <exception>
#include <string>
#include <utility>

OUTLINK::~OUTLINK() {
    shutdown();
}

bool OUTLINK::init(size_t senders, size_t queue_capacity) {
    shutdown();

    if (senders == 0) {
        METALOG_ERROR("OUTLINK: senders count must be > 0");
        return false;
    }

    if (!buffer_.init(queue_capacity)) {
        METALOG_ERROR("OUTLINK: failed to init shared queue");
        return false;
    }

    if (!priority_buffer_.init(queue_capacity)) {
        METALOG_ERROR("OUTLINK: failed to init priority queue");
        buffer_.shutdown();
        return false;
    }

    threads_.reserve(senders);
    running_.store(true, std::memory_order_release);

    try {
        for (size_t i = 0; i < senders; ++i) {
            threads_.emplace_back(&OUTLINK::sender_loop, this);
        }
    } catch (const std::exception& e) {
        METALOG_ERROR(std::string("OUTLINK: failed to start sender: ") + e.what());
        shutdown();
        return false;
    }

    METALOG_INFO("OUTLINK initialized with " + std::to_string(senders) +
                 " senders, shared queue capacity " +
                 std::to_string(buffer_.capacity()));
    return true;
}

void OUTLINK::shutdown() {
    running_.store(false, std::memory_order_release);
    wake_.notify_all();

    for (auto& thread : threads_) {
        if (thread.joinable()) thread.join();
    }
    threads_.clear();

    buffer_.shutdown();
    priority_buffer_.shutdown();

    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        sender_ = nullptr;
    }

    METALOG_INFO("OUTLINK shutdown");
}

void OUTLINK::set_sender(SendCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    sender_ = std::move(callback);
}

bool OUTLINK::push(OutgoingPacket&& packet) {
    if (!running_.load(std::memory_order_acquire)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        METALOG_WARNING("OUTLINK: not running, packet dropped");
        return false;
    }

    const bool priority = packet.priority;
    RingBuffer<OutgoingPacket>& target =
        priority ? priority_buffer_ : buffer_;

    if (!target.push(std::move(packet))) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        METALOG_WARNING(priority
                            ? "OUTLINK: priority queue is full, packet dropped"
                            : "OUTLINK: shared queue is full, packet dropped");
        return false;
    }

    wake_.notify_one();
    return true;
}

void OUTLINK::sender_loop() {
    // После shutdown дочитываем обе очереди, чтобы отправить уже принятые пакеты.
    while (running_.load(std::memory_order_acquire) ||
           !priority_buffer_.empty() || !buffer_.empty()) {
        OutgoingPacket packet;
        if (priority_buffer_.pop(packet) || buffer_.pop(packet)) {
            SendCallback callback;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                callback = sender_;
            }

            if (!callback) {
                failed_.fetch_add(1, std::memory_order_relaxed);
                METALOG_ERROR("OUTLINK: no sender callback, packet for module '" +
                              packet.module + "' dropped");
                continue;
            }

            int result = -1;
            try {
                result = callback(std::move(packet));
            } catch (const std::exception& e) {
                METALOG_ERROR(std::string("OUTLINK: sender callback threw: ") +
                              e.what());
            } catch (...) {
                METALOG_ERROR("OUTLINK: sender callback threw unknown exception");
            }

            if (result < 0) {
                failed_.fetch_add(1, std::memory_order_relaxed);
                METALOG_WARNING("OUTLINK: send failed with code " +
                                std::to_string(result));
            } else {
                sent_.fetch_add(1, std::memory_order_relaxed);
            }
            continue;
        }

        if (!running_.load(std::memory_order_acquire)) break;

        std::unique_lock<std::mutex> lock(wake_.mutex);
        wake_.cv.wait_for(lock, std::chrono::milliseconds(1));
    }
}

size_t OUTLINK::queue_size() const {
    return buffer_.size() + priority_buffer_.size();
}

size_t OUTLINK::senders_count() const {
    return threads_.size();
}
