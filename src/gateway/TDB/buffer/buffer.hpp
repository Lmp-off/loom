#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// Buffer и транспортные конверты TDB.
//
// Архитектурные инварианты:
// - IncomingPacket.client_fd всегда -1: fd принадлежит транспорту;
// - client_address — имя отправителя из handshake, нужно для ответа и логов;
// - priority наследуется входящим и исходящим пакетом;
// - RingBuffer — ограниченная lock-free MPMC-очередь (алгоритм Vyukov);
// - push из транспорта/воркера, pop только диспетчером/сендером;
// - ёмкость округляется вверх до степени двойки;
// - при переполнении push возвращает false, пакет отбрасывается вызывающим.

// Метка живого соединения. Читатель сокета владеет меткой до закрытия.
struct IngressConnection {};

struct IncomingPacket {
    std::vector<uint8_t> data;
    int client_fd = -1;
    std::string client_address;
    int client_port = 0;
    int transport_type = -1;
    bool priority = false;
    std::weak_ptr<const IngressConnection> connection;
};

struct OutgoingPacket {
    std::string module;
    std::vector<uint8_t> data;
    int transport_type = -1;
    bool priority = false;
};

template <typename T>
class RingBuffer {
public:
    RingBuffer() = default;
    ~RingBuffer() { shutdown(); }

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    bool init(size_t capacity);
    void shutdown();

    bool push(T&& packet);
    bool pop(T& packet);

    // Для очереди с одним потребителем. consume перемещает пакет только при true.
    template <typename Consumer>
    bool try_consume(Consumer&& consume);

    size_t size() const;
    size_t capacity() const;
    bool empty() const;
    bool full() const;

private:
    struct Slot {
        std::atomic<size_t> seq{0};
        T packet{};
    };

    std::unique_ptr<Slot[]> slots_;
    size_t capacity_ = 0;
    size_t mask_ = 0;
    std::atomic<size_t> enqueue_pos_{0};
    std::atomic<size_t> dequeue_pos_{0};
};

using Buffer = RingBuffer<IncomingPacket>;

template <typename T>
bool RingBuffer<T>::init(size_t capacity) {
    if (capacity < 2) capacity = 2;
    if (capacity > (std::numeric_limits<size_t>::max() >> 2)) return false;

    size_t rounded = 1;
    while (rounded < capacity) rounded <<= 1;

    auto slots = std::make_unique<Slot[]>(rounded);
    for (size_t i = 0; i < rounded; ++i) {
        slots[i].seq.store(i, std::memory_order_relaxed);
    }

    slots_ = std::move(slots);
    capacity_ = rounded;
    mask_ = rounded - 1;
    enqueue_pos_.store(0, std::memory_order_relaxed);
    dequeue_pos_.store(0, std::memory_order_relaxed);
    return true;
}

template <typename T>
void RingBuffer<T>::shutdown() {
    slots_.reset();
    capacity_ = 0;
    mask_ = 0;
    enqueue_pos_.store(0, std::memory_order_relaxed);
    dequeue_pos_.store(0, std::memory_order_relaxed);
}

template <typename T>
bool RingBuffer<T>::push(T&& packet) {
    if (!slots_ || capacity_ == 0) return false;

    size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
    for (;;) {
        Slot& slot = slots_[pos & mask_];
        const size_t seq = slot.seq.load(std::memory_order_acquire);
        const std::intptr_t dif =
            static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

        if (dif == 0) {
            if (enqueue_pos_.compare_exchange_weak(
                    pos, pos + 1,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                slot.packet = std::move(packet);
                slot.seq.store(pos + 1, std::memory_order_release);
                return true;
            }
        } else if (dif < 0) {
            return false;
        } else {
            pos = enqueue_pos_.load(std::memory_order_relaxed);
        }
    }
}

template <typename T>
bool RingBuffer<T>::pop(T& packet) {
    if (!slots_ || capacity_ == 0) return false;

    size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
    for (;;) {
        Slot& slot = slots_[pos & mask_];
        const size_t seq = slot.seq.load(std::memory_order_acquire);
        const std::intptr_t dif =
            static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);

        if (dif == 0) {
            if (dequeue_pos_.compare_exchange_weak(
                    pos, pos + 1,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                packet = std::move(slot.packet);
                slot.seq.store(pos + capacity_, std::memory_order_release);
                return true;
            }
        } else if (dif < 0) {
            return false;
        } else {
            pos = dequeue_pos_.load(std::memory_order_relaxed);
        }
    }
}

template <typename T>
template <typename Consumer>
bool RingBuffer<T>::try_consume(Consumer&& consume) {
    if (!slots_) return false;

    const size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
    Slot& slot = slots_[pos & mask_];
    if (slot.seq.load(std::memory_order_acquire) != pos + 1 ||
        !consume(slot.packet)) {
        return false;
    }

    dequeue_pos_.store(pos + 1, std::memory_order_relaxed);
    slot.seq.store(pos + capacity_, std::memory_order_release);
    return true;
}

template <typename T>
size_t RingBuffer<T>::size() const {
    const size_t enq = enqueue_pos_.load(std::memory_order_acquire);
    const size_t deq = dequeue_pos_.load(std::memory_order_acquire);
    return deq > enq ? 0 : enq - deq;
}

template <typename T>
size_t RingBuffer<T>::capacity() const {
    return capacity_;
}

template <typename T>
bool RingBuffer<T>::empty() const {
    return size() == 0;
}

template <typename T>
bool RingBuffer<T>::full() const {
    return capacity_ != 0 && size() >= capacity_;
}
