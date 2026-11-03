#pragma once

#include "../transport.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class InboundConnection;

// UdsTransport — UDS-транспорт модуля.
//
// Архитектурные инварианты:
// - слушает /tmp/loom_<module>.sock и принимает GWP-кадры;
// - первым кадром читает handshake с именем модуля и priority-флагом;
// - регистрирует принятое соединение в ConnectionCache;
// - каждое соединение обслуживается отдельным client-loop потоком;
// - отправка идёт через LinkManager, а не открытием соединения на каждый кадр;
// - stop() сначала shutdown'ит соединения, затем join'ит потоки и только
//   после этого закрывает fd.

class UdsTransport final : public Transport {
public:
    explicit UdsTransport(std::string path);
    ~UdsTransport() override;

    UdsTransport(const UdsTransport&) = delete;
    UdsTransport& operator=(const UdsTransport&) = delete;

    bool start() override;
    void stop() override;

    int send(const endpoint& ep, const std::vector<uint8_t>& frame) override;
    int send_to(const std::string& module, const endpoint& ep,
                const std::vector<uint8_t>& frame) override;

    connection_type type() const override { return CONNECTION_UDS; }
    int get_listen_fd() const override { return listen_fd_; }

private:
    void accept_loop();
    void client_loop(int fd);

    std::string path_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;

    std::mutex conn_mutex_;
    std::vector<std::thread> conn_threads_;
    std::vector<std::shared_ptr<InboundConnection>> connections_;
};
