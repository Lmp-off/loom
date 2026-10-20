#pragma once
#include <atomic>
#include <string>
#include <thread>
#include "../transport.h"

class TDB;

class UdsTransport : public transport::Transport {
public:
    explicit UdsTransport(std::string path);
    ~UdsTransport() override;

    int  send_request(const std::string& module,
                      const std::string& method,
                      const std::string& data) override;
    int  get_listen_fd() override { return listen_fd_; }

    void set_tdb(TDB* tdb) { tdb_ = tdb; }
    bool start();
    void stop();
    int  type() const;

private:
    void accept_loop();
    void client_loop(int fd);

    std::string path_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    TDB* tdb_ = nullptr;
};