#include "connection_cache.hpp"

#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int send_all(int fd, const uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const ssize_t sent =
            ::send(fd, data + offset, size - offset, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (sent == 0) return -1;
        offset += static_cast<size_t>(sent);
    }
    return static_cast<int>(offset);
}

} // namespace

InboundConnection::InboundConnection(std::string module, int fd, bool priority)
    : module_(std::move(module)), fd_(fd), priority_(priority) {}

InboundConnection::~InboundConnection() {
    close();
}

int InboundConnection::send(const std::vector<uint8_t>& frame) {
    if (frame.empty()) return -1;

    std::lock_guard<std::mutex> lock(send_mutex_);
    const int fd = fd_.load(std::memory_order_acquire);
    if (fd < 0) return -1;

    const int result = send_all(fd, frame.data(), frame.size());
    if (result < 0) {
        ::close(fd);
        fd_.store(-1, std::memory_order_release);
    }
    return result;
}

void InboundConnection::shutdown() {
    std::lock_guard<std::mutex> lock(send_mutex_);
    const int fd = fd_.load(std::memory_order_acquire);
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
    }
}

void InboundConnection::close() {
    std::lock_guard<std::mutex> lock(send_mutex_);
    const int fd = fd_.load(std::memory_order_acquire);
    if (fd >= 0) {
        ::close(fd);
        fd_.store(-1, std::memory_order_release);
    }
}

void ConnectionCache::put_inbound(
    const std::shared_ptr<InboundConnection>& connection) {
    if (!connection) return;

    std::lock_guard<std::mutex> lock(mutex_);
    inbound_[connection->module()] = connection;
}

void ConnectionCache::remove_inbound(
    const std::shared_ptr<InboundConnection>& connection) {
    if (!connection) return;

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = inbound_.find(connection->module());
    if (it != inbound_.end() && it->second == connection) {
        inbound_.erase(it);
    }
}

std::shared_ptr<InboundConnection> ConnectionCache::find(
    const std::string& module) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = inbound_.find(module);
    return it == inbound_.end() ? nullptr : it->second;
}

int ConnectionCache::send(const std::string& module,
                          const std::vector<uint8_t>& frame) {
    auto connection = find(module);
    if (!connection) return -1;

    const int result = connection->send(frame);
    if (result < 0) {
        // Соединение могло быть закрыто удалённой стороной. Убираем его,
        // чтобы следующий вызов не тратил время на заведомо мёртвый fd.
        remove_inbound(connection);
    }
    return result;
}

void ConnectionCache::set_priority(const std::string& module, bool priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    priority_[module] = priority;
    auto it = inbound_.find(module);
    if (it != inbound_.end() && it->second) {
        it->second->set_priority(priority);
    }
}

bool ConnectionCache::is_priority(const std::string& module) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = priority_.find(module);
    return it != priority_.end() && it->second;
}

void ConnectionCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    inbound_.clear();
    priority_.clear();
}

size_t ConnectionCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inbound_.size();
}

ConnectionCache& get_connection_cache() {
    static ConnectionCache instance;
    return instance;
}
