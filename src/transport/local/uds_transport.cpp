#include "uds_transport.hpp"

#include "uds.h"
#include "../connection_cache.hpp"
#include "../handshake.hpp"
#include "../link.hpp"
#include "../../gateway/TDB/tdb.hpp"
#include "../../protocol/gwp.h"
#include "../../../metalogger/metalogger.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <exception>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace {

bool recv_exact(int fd, uint8_t* buffer, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const ssize_t received =
            ::recv(fd, buffer + offset, size - offset, 0);
        if (received == 0) return false;
        if (received < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        offset += static_cast<size_t>(received);
    }
    return true;
}

// Первый кадр на каждом входящем UDS-соединении — handshake с именем
// модуля-отправителя и флагом priority. Дальше по этому же fd идут
// обычные GWP-кадры.
bool recv_handshake(int fd, std::string& module, bool& priority) {
    uint8_t header[loom_transport::HANDSHAKE_HEADER_SIZE];
    if (!recv_exact(fd, header, sizeof(header))) return false;

    uint32_t magic = 0;
    uint32_t flags = 0;
    uint32_t name_len = 0;
    std::memcpy(&magic, header, sizeof(magic));
    std::memcpy(&flags, header + 4, sizeof(flags));
    std::memcpy(&name_len, header + 8, sizeof(name_len));

    if (magic != loom_transport::HANDSHAKE_MAGIC ||
        name_len > loom_transport::MAX_MODULE_NAME) {
        return false;
    }

    std::vector<uint8_t> payload(sizeof(header) + static_cast<size_t>(name_len));
    std::memcpy(payload.data(), header, sizeof(header));
    if (name_len != 0 &&
        !recv_exact(fd, payload.data() + sizeof(header), name_len)) {
        return false;
    }

    return loom_transport::parse_handshake(payload.data(), payload.size(),
                                           module, &priority);
}

} // namespace

UdsTransport::UdsTransport(std::string path)
    : path_(std::move(path)) {}

UdsTransport::~UdsTransport() {
    stop();
}

bool UdsTransport::start() {
    if (running_.load(std::memory_order_acquire)) return true;

    listen_fd_ = uds::create_uds_socket(path_);
    if (listen_fd_ < 0) {
        METALOG_ERROR("[UDS] failed to create socket " + path_);
        return false;
    }

    running_.store(true, std::memory_order_release);
    try {
        accept_thread_ = std::thread(&UdsTransport::accept_loop, this);
    } catch (const std::exception& e) {
        running_.store(false, std::memory_order_release);
        ::close(listen_fd_);
        listen_fd_ = -1;
        ::unlink(path_.c_str());
        METALOG_ERROR(std::string("[UDS] failed to start accept thread: ") +
                      e.what());
        return false;
    }

    METALOG_INFO("[UDS] listening on " + path_);
    return true;
}

void UdsTransport::stop() {
    running_.store(false, std::memory_order_release);

    if (accept_thread_.joinable()) accept_thread_.join();

    std::vector<std::shared_ptr<InboundConnection>> connections;
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        connections.swap(connections_);
        threads.swap(conn_threads_);
    }

    // shutdown будит client_loop, который может быть в blocking recv.
    // Закрываем fd только после join: иначе close() гоняется с recv().
    for (auto& connection : connections) {
        if (connection) connection->shutdown();
    }

    for (auto& thread : threads) {
        if (thread.joinable()) thread.join();
    }

    for (auto& connection : connections) {
        if (connection) connection->close();
    }

    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        ::unlink(path_.c_str());
        METALOG_INFO("[UDS] stopped listening on " + path_);
    }
}

void UdsTransport::accept_loop() {
    while (running_.load(std::memory_order_acquire)) {
        struct pollfd pfd;
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;
        pfd.revents = 0;

        const int ready = ::poll(&pfd, 1, 100);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) continue;
        if ((pfd.revents & POLLIN) == 0) break;

        const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            if (!running_.load(std::memory_order_acquire)) break;
            METALOG_WARNING("[UDS] accept failed");
            continue;
        }

        std::lock_guard<std::mutex> lock(conn_mutex_);
        try {
            conn_threads_.emplace_back(&UdsTransport::client_loop, this,
                                       client_fd);
        } catch (const std::exception& e) {
            ::close(client_fd);
            METALOG_ERROR(std::string("[UDS] failed to start client thread: ") +
                          e.what());
        }
    }
}

void UdsTransport::client_loop(int fd) {
    uint8_t header_buffer[gwp::HEADER_SIZE];
    std::vector<uint8_t> frame;

    // Первым кадром идёт handshake с именем модуля-отправителя и флагом
    // priority. Соединение сразу регистрируется в ConnectionCache, чтобы
    // ответы можно было отправлять по этому же fd.
    std::string peer_module;
    bool priority = false;
    if (!recv_handshake(fd, peer_module, priority)) {
        METALOG_WARNING("[UDS] invalid or missing handshake");
        ::close(fd);
        return;
    }

    auto connection =
        std::make_shared<InboundConnection>(peer_module, fd, priority);
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        // stop() мог уже начаться, пока мы читали handshake.
        if (!running_.load(std::memory_order_acquire)) {
            connection->close();
            return;
        }
        connections_.push_back(connection);
    }

    if (!peer_module.empty()) {
        get_connection_cache().put_inbound(connection);
    }

    const auto ingress_connection =
        priority ? std::make_shared<IngressConnection>() : nullptr;

    // Одно соединение может нести несколько кадров: LinkManager
    // переиспользует fd, поэтому читаем в цикле до закрытия/ошибки.
    while (running_.load(std::memory_order_acquire)) {
        if (!recv_exact(fd, header_buffer, sizeof(header_buffer))) {
            METALOG_DEBUG("[UDS] client closed before header");
            break;
        }

        gwp::GWPHeader header;
        if (!gwp::parse_header(header_buffer, sizeof(header_buffer), header)) {
            METALOG_WARNING("[UDS] invalid GWP header");
            break;
        }

        const size_t payload_size =
            static_cast<size_t>(header.method_name_len) + header.data_len;

        frame.resize(sizeof(header_buffer) + payload_size);
        std::memcpy(frame.data(), header_buffer, sizeof(header_buffer));

        if (payload_size != 0 &&
            !recv_exact(fd, frame.data() + sizeof(header_buffer),
                        payload_size)) {
            METALOG_WARNING("[UDS] client closed before payload");
            break;
        }

        if (tdb_ != nullptr) {
            IncomingPacket packet;
            packet.data = std::move(frame);
            packet.client_fd = -1;
            // Имя отправителя пришло в handshake этого соединения.
            packet.client_address = peer_module;
            packet.client_port = 0;
            packet.transport_type = static_cast<int>(CONNECTION_UDS);
            packet.priority = priority;
            packet.connection = ingress_connection;
            tdb_->pass_packet(std::move(packet));
        } else {
            METALOG_WARNING("[UDS] TDB is not set, packet dropped");
        }
    }

    if (!peer_module.empty()) {
        get_connection_cache().remove_inbound(connection);
    }

    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        connections_.erase(
            std::remove(connections_.begin(), connections_.end(), connection),
            connections_.end());
    }
    connection->close();
}

int UdsTransport::send(const endpoint& ep,
                       const std::vector<uint8_t>& frame) {
    return send_to(std::string(), ep, frame);
}

int UdsTransport::send_to(const std::string& module, const endpoint& ep,
                          const std::vector<uint8_t>& frame) {
    if (ep.type != CONNECTION_UDS) return -1;
    if (frame.empty()) return -1;

    // LinkManager сам откроет соединение при первом обращении
    // и переиспользует его для следующих кадров.
    return get_link_manager().send(module, ep, frame);
}
