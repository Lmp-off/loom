#include "buffer.hpp"

Buffer::Buffer() : capacity_(0) {}

Buffer::~Buffer() {
    shutdown();
}

bool Buffer::init(size_t capacity) {
    if (capacity == 0) return false;
    capacity_ = capacity;
    slots_.resize(capacity);
    head_ = 0;
    tail_ = 0;
    return true;
}

void Buffer::shutdown() {
    slots_.clear();
    capacity_ = 0;
}

bool Buffer::push(IncomingPacket&& packet) {
    size_t head = head_.load(std::memory_order_acquire);
    size_t next = (head + 1) % capacity_;
    
    if (next == tail_.load(std::memory_order_acquire)) {
        return false;
    }
    
    slots_[head].packet = std::move(packet);
    slots_[head].has_data.store(true, std::memory_order_release);
    head_.store(next, std::memory_order_release);
    return true;
}

bool Buffer::pop(size_t worker_id, IncomingPacket& packet) {
    size_t tail = tail_.load(std::memory_order_acquire);
    
    if (tail == head_.load(std::memory_order_acquire)) {
        return false;
    }
    
    if (!slots_[tail].has_data.load(std::memory_order_acquire)) {
        return false;
    }
    
    packet = std::move(slots_[tail].packet);
    slots_[tail].has_data.store(false, std::memory_order_release);
    tail_.store((tail + 1) % capacity_, std::memory_order_release);
    return true;
}

size_t Buffer::size() const {
    size_t head = head_.load(std::memory_order_acquire);
    size_t tail = tail_.load(std::memory_order_acquire);
    if (head >= tail) return head - tail;
    return capacity_ - tail + head;
}

size_t Buffer::capacity() const {
    return capacity_;
}

bool Buffer::empty() const {
    return size() == 0;
}

bool Buffer::full() const {
    return size() == capacity_ - 1;
}