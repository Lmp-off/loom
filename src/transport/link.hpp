#pragma once

#include "../discovery/cache.h"
#include "../gateway/TDB/buffer/buffer.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// LinkManager — пул исходящих UDS-соединений.
//
// Архитектурные инварианты:
// - ключ пула — type|address|port, один endpoint = один Link;
// - Link дуплексный: send() пишет кадры, reader-поток читает встречные кадры
//   с того же fd и отдаёт их в TDB;
// - Link не знает GWP-семантику, reader использует parse_header только для границ кадра;
// - имя локального модуля и priority-флаг уходят в handshake при открытии;
// - при ошибке отправки Link инвалидируется и переоткрывается один раз;
// - смена priority инвалидирует существующий Link, потому что handshake
//   отправляется только при открытии;
// - prune_idle() — заготовка под будущий janitor.

class Link {
public:
    using IncomingSink = std::function<void(IncomingPacket&&)>;

    Link(const endpoint& ep, std::string remote_module, int fd, bool priority,
         IncomingSink sink);
    ~Link();

    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    int send(const std::vector<uint8_t>& frame);

    int fd() const { return fd_; }
    bool is_open() const {
        return fd_ >= 0 && open_.load(std::memory_order_acquire);
    }
    bool priority() const { return priority_; }
    const std::string& remote_module() const { return remote_module_; }
    const endpoint& remote() const { return remote_; }
    std::chrono::steady_clock::time_point last_used() const;

private:
    void reader_loop();
    void stop_reader();

    endpoint remote_;
    std::string remote_module_;
    int fd_ = -1;
    bool priority_ = false;
    IncomingSink sink_;

    std::mutex send_mutex_;
    std::atomic<int64_t> last_used_ns_{0};
    std::atomic<bool> open_{true};
    std::atomic<bool> reader_running_{false};
    std::thread reader_;
};

class LinkManager {
public:
    using IncomingSink = Link::IncomingSink;

    LinkManager() = default;
    ~LinkManager();

    LinkManager(const LinkManager&) = delete;
    LinkManager& operator=(const LinkManager&) = delete;

    void set_local_module(const std::string& module);
    void set_incoming_sink(IncomingSink sink);
    void set_priority(const endpoint& ep, bool priority);

    std::shared_ptr<Link> acquire(const std::string& module,
                                  const endpoint& ep);
    std::shared_ptr<Link> acquire(const endpoint& ep);

    int send(const std::string& module, const endpoint& ep,
             const std::vector<uint8_t>& frame);
    int send(const endpoint& ep, const std::vector<uint8_t>& frame);

    void invalidate(const endpoint& ep, const std::shared_ptr<Link>& link);
    void prune_idle(std::chrono::seconds idle);
    void close_all();
    size_t size() const;

private:
    static std::string key_for(const endpoint& ep);

    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Link>> links_;
    std::map<std::string, bool> priority_;

    mutable std::mutex local_module_mutex_;
    std::string local_module_;

    IncomingSink incoming_sink_;
};

LinkManager& get_link_manager();
