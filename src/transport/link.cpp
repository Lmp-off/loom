#include "link.hpp"

#include "handshake.hpp"
#include "local/uds.h"
#include "../../metalogger/metalogger.h"
#include "../protocol/gwp.h"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace {

std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int send_all(int fd, const uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const ssize_t sent = ::send(fd,
                                    data + offset,
                                    size - offset,
                                    MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (sent == 0) return -1;
        offset += static_cast<size_t>(sent);
    }
    return static_cast<int>(offset);
}

bool recv_exact(int fd, uint8_t* buffer, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const ssize_t received = ::recv(fd, buffer + offset, size - offset, 0);
        if (received == 0) return false;
        if (received < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        offset += static_cast<size_t>(received);
    }
    return true;
}

} // namespace

Link::Link(const endpoint& ep, std::string remote_module, int fd,
           bool priority, IncomingSink sink)
    : remote_(ep),
      remote_module_(std::move(remote_module)),
      fd_(fd),
      priority_(priority),
      sink_(std::move(sink)) {
    last_used_ns_.store(now_ns(), std::memory_order_relaxed);

    if (sink_) {
        try {
            reader_running_.store(true, std::memory_order_release);
            reader_ = std::thread(&Link::reader_loop, this);
        } catch (...) {
            reader_running_.store(false, std::memory_order_release);
            open_.store(false, std::memory_order_release);
            if (fd_ >= 0) {
                ::close(fd_);
                fd_ = -1;
            }
        }
    }
}

Link::~Link() {
    stop_reader();

    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void Link::reader_loop() {
    uint8_t header_buffer[gwp::HEADER_SIZE];
    std::vector<uint8_t> frame;
    const auto ingress_connection =
        priority_ ? std::make_shared<IngressConnection>() : nullptr;

    while (open_.load(std::memory_order_acquire)) {
        if (!recv_exact(fd_, header_buffer, sizeof(header_buffer))) break;

        gwp::GWPHeader header;
        if (!gwp::parse_header(header_buffer, sizeof(header_buffer), header)) {
            METALOG_WARNING("Link: invalid GWP header from " + remote_module_);
            break;
        }

        const size_t payload_size =
            static_cast<size_t>(header.method_name_len) + header.data_len;

        frame.resize(sizeof(header_buffer) + payload_size);
        std::memcpy(frame.data(), header_buffer, sizeof(header_buffer));

        if (payload_size != 0 &&
            !recv_exact(fd_, frame.data() + sizeof(header_buffer),
                        payload_size)) {
            break;
        }

        IncomingPacket packet;
        packet.data = std::move(frame);
        packet.client_fd = -1;
        packet.client_address = remote_module_;
        packet.client_port = 0;
        packet.transport_type = static_cast<int>(CONNECTION_UDS);
        packet.priority = priority_;
        packet.connection = ingress_connection;

        if (sink_) sink_(std::move(packet));
    }

    open_.store(false, std::memory_order_release);
    reader_running_.store(false, std::memory_order_release);
}

void Link::stop_reader() {
    open_.store(false, std::memory_order_release);

    if (reader_running_.load(std::memory_order_acquire)) {
        if (fd_ >= 0) ::shutdown(fd_, SHUT_RDWR);
    }

    if (reader_.joinable()) reader_.join();
    reader_running_.store(false, std::memory_order_release);
}

int Link::send(const std::vector<uint8_t>& frame) {
    if (frame.empty()) return -1;

    std::lock_guard<std::mutex> lock(send_mutex_);
    if (fd_ < 0 || !open_.load(std::memory_order_acquire)) return -1;

    const int result = send_all(fd_, frame.data(), frame.size());
    if (result < 0) {
        open_.store(false, std::memory_order_release);
        return result;
    }

    last_used_ns_.store(now_ns(), std::memory_order_relaxed);
    return result;
}

std::chrono::steady_clock::time_point Link::last_used() const {
    const auto ns = last_used_ns_.load(std::memory_order_relaxed);
    return std::chrono::steady_clock::time_point(
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::nanoseconds(ns)));
}

LinkManager::~LinkManager() {
    close_all();
}

std::string LinkManager::key_for(const endpoint& ep) {
    return std::to_string(static_cast<int>(ep.type)) + "|" +
           ep.address + "|" + std::to_string(ep.port);
}

void LinkManager::set_local_module(const std::string& module) {
    std::lock_guard<std::mutex> lock(local_module_mutex_);
    local_module_ = module;
}

void LinkManager::set_incoming_sink(IncomingSink sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    incoming_sink_ = std::move(sink);
}

void LinkManager::set_priority(const endpoint& ep, bool priority) {
    const std::string key = key_for(ep);

    std::lock_guard<std::mutex> lock(mutex_);
    priority_[key] = priority;

    auto it = links_.find(key);
    if (it != links_.end() && it->second &&
        it->second->priority() != priority) {
        // Handshake отправляется только при открытии соединения. Чтобы
        // сообщить новый режим, соединение нужно переоткрыть.
        links_.erase(it);
    }
}

std::shared_ptr<Link> LinkManager::acquire(const std::string& module,
                                           const endpoint& ep) {
    if (ep.type != CONNECTION_UDS || ep.address.empty()) {
        METALOG_ERROR("LinkManager: unsupported endpoint");
        return nullptr;
    }

    const std::string key = key_for(ep);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = links_.find(key);
        if (it != links_.end()) return it->second;
    }

    // Соединение открываем без удержания mutex_, чтобы один медленный
    // connect не блокировал отправку в другие endpoint'ы.
    const int fd = uds::connect_uds(ep.address);
    if (fd < 0) {
        METALOG_ERROR("LinkManager: failed to connect to " + ep.address);
        return nullptr;
    }

    // Первый кадр на новом соединении — handshake с именем нашего модуля.
    // Flags сообщают, что модуль хочет priority/direct обработку.
    std::string local_module;
    {
        std::lock_guard<std::mutex> lock(local_module_mutex_);
        local_module = local_module_;
    }

    bool priority = false;
    IncomingSink sink;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = priority_.find(key);
        if (it != priority_.end()) priority = it->second;
        sink = incoming_sink_;
    }

    const std::vector<uint8_t> handshake =
        loom_transport::make_handshake(local_module, priority);
    if (send_all(fd, handshake.data(), handshake.size()) < 0) {
        METALOG_ERROR("LinkManager: failed to send handshake to " + ep.address);
        ::close(fd);
        return nullptr;
    }

    auto link = std::make_shared<Link>(ep, module, fd, priority, sink);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = links_.find(key);
    if (it != links_.end()) return it->second; // конкурент уже создал Link
    links_[key] = link;
    return link;
}

std::shared_ptr<Link> LinkManager::acquire(const endpoint& ep) {
    return acquire(std::string(), ep);
}

int LinkManager::send(const std::string& module, const endpoint& ep,
                      const std::vector<uint8_t>& frame) {
    if (frame.empty()) return -1;

    auto link = acquire(module, ep);
    if (!link) return -1;

    int result = link->send(frame);
    if (result >= 0) return result;

    // Соединение могло быть закрыто удалённой стороной. Выбрасываем его
    // и один раз пробуем новое: частично записанный кадр останется
    // в старом (уже закрытом) соединении и не будет доставлен.
    invalidate(ep, link);
    METALOG_WARNING("LinkManager: send failed to " + ep.address +
                    ", reconnecting");

    link = acquire(module, ep);
    if (!link) return -1;

    result = link->send(frame);
    if (result < 0) {
        invalidate(ep, link);
        METALOG_WARNING("LinkManager: retry failed to " + ep.address);
    }
    return result;
}

int LinkManager::send(const endpoint& ep,
                      const std::vector<uint8_t>& frame) {
    return send(std::string(), ep, frame);
}

void LinkManager::invalidate(const endpoint& ep,
                             const std::shared_ptr<Link>& link) {
    const std::string key = key_for(ep);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = links_.find(key);
    if (it != links_.end() && it->second == link) {
        links_.erase(it);
    }
}

void LinkManager::prune_idle(std::chrono::seconds idle) {
    const auto now = std::chrono::steady_clock::now();
    const auto idle_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(idle).count();

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = links_.begin(); it != links_.end();) {
        const auto age = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             now - it->second->last_used())
                             .count();
        if (age > idle_ns) {
            it = links_.erase(it);
        } else {
            ++it;
        }
    }
}

void LinkManager::close_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    links_.clear();
    priority_.clear();
}

size_t LinkManager::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return links_.size();
}

LinkManager& get_link_manager() {
    static LinkManager instance;
    return instance;
}
