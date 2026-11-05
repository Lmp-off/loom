#pragma once

#include "transport.h"
#include "../gateway/TDB/buffer/buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class TDB;

// TransportManager — роутинг исходящих пакетов.
//
// Архитектурные инварианты:
// - сначала пробует ConnectionCache (уже принятое входящее соединение);
// - при промахе резолвит endpoint через Cache, затем через ядро;
// - выбирает Transport по endpoint.type;
// - не владеет LinkManager/ConnectionCache и не знает их внутренности;
// - входящие пакеты идут мимо менеджера, напрямую в TDB.

class TransportManager {
public:
    TransportManager() = default;
    ~TransportManager();

    TransportManager(const TransportManager&) = delete;
    TransportManager& operator=(const TransportManager&) = delete;

    bool register_transport(std::unique_ptr<Transport> transport);
    void set_tdb(TDB* tdb);

    bool start_all();
    void stop_all();

    int send_request(const std::string& module,
                     const std::string& method,
                     const std::string& data);
    int send_async(const std::string& module,
                   const std::string& method,
                   const std::string& data);
    int send_outgoing(OutgoingPacket&& packet);

    Transport* get(connection_type type);
    size_t count() const;

private:
    bool resolve_endpoint(const std::string& module, endpoint& ep);
    int send_to(const std::string& module, const endpoint& ep,
                const std::vector<uint8_t>& frame);

    int send_packet(const std::string& module,
                    const std::string& method,
                    const std::string& data,
                    bool async);

    mutable std::mutex mutex_;
    std::map<connection_type, std::unique_ptr<Transport>> by_type_;
    TDB* tdb_ = nullptr;
};
