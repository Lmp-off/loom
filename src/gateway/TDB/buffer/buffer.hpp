#pragma once

#include <vector>
#include <atomic>
#include <cstddef>
#include <string>

struct IncomingPacket {
    std::vector<uint8_t> data;
    int client_fd;
    std::string client_address;
    int client_port;
    int transport_type;
};

class Buffer {
public:
    Buffer();
    ~Buffer();

    bool init(size_t capacity);
    void shutdown();

    bool push(IncomingPacket&& packet);
    bool pop(size_t worker_id, IncomingPacket& packet);
    
    size_t size() const;
    size_t capacity() const;
    bool empty() const;
    bool full() const;

private:
    struct Slot {
        std::atomic<bool> has_data{false};
        IncomingPacket packet;
    };

    std::vector<Slot> slots_;
    size_t capacity_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};