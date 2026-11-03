#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// ConnectionCache — процесс-глобальный реестр уже принятых входящих UDS-соединений.
//
// Архитектурные инварианты:
// - fd принадлежит транспорту; в TDB всегда уходит client_fd = -1;
// - ключ реестра — имя удалённого модуля из handshake;
// - исходящий путь сначала пробует ConnectionCache, затем Cache -> ядро -> LinkManager;
// - ответы переиспользуют входящее соединение, новые запросы — только при совпадении priority;
// - send(), shutdown() и close() сериализованы между собой.
//
// send() возвращает >= 0 при успехе и < 0 при промахе или ошибке,
// чтобы вызывающий мог откатиться на LinkManager.

class InboundConnection {
public:
    InboundConnection(std::string module, int fd, bool priority);
    ~InboundConnection();

    InboundConnection(const InboundConnection&) = delete;
    InboundConnection& operator=(const InboundConnection&) = delete;

    int send(const std::vector<uint8_t>& frame);
    void shutdown();
    void close();

    const std::string& module() const { return module_; }
    bool priority() const { return priority_.load(std::memory_order_acquire); }
    void set_priority(bool priority) {
        priority_.store(priority, std::memory_order_release);
    }
    int fd() const { return fd_.load(std::memory_order_acquire); }
    bool is_open() const {
        return fd_.load(std::memory_order_acquire) >= 0;
    }

private:
    std::string module_;
    std::atomic<int> fd_{-1};
    std::atomic<bool> priority_{false};
    std::mutex send_mutex_;
};

class ConnectionCache {
public:
    void put_inbound(const std::shared_ptr<InboundConnection>& connection);
    void remove_inbound(const std::shared_ptr<InboundConnection>& connection);

    std::shared_ptr<InboundConnection> find(const std::string& module);
    int send(const std::string& module, const std::vector<uint8_t>& frame);

    void set_priority(const std::string& module, bool priority);
    bool is_priority(const std::string& module) const;

    void clear();
    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<InboundConnection>> inbound_;
    std::map<std::string, bool> priority_;
};

ConnectionCache& get_connection_cache();
