#include "transport_manager.hpp"

#include "../discovery/cache.h"
#include "../core/server_core_api.h"
#include "../gateway/TDB/tdb.hpp"
#include "../protocol/gwp.h"
#include "connection_cache.hpp"
#include "link.hpp"
#include "../../metalogger/metalogger.h"

#include <utility>

TransportManager::~TransportManager() {
    stop_all();
}

bool TransportManager::register_transport(std::unique_ptr<Transport> transport) {
    if (!transport) {
        METALOG_ERROR("TransportManager: null transport");
        return false;
    }

    const connection_type type = transport->type();

    std::lock_guard<std::mutex> lock(mutex_);
    if (by_type_.find(type) != by_type_.end()) {
        METALOG_ERROR("TransportManager: transport for type " +
                      std::to_string(static_cast<int>(type)) +
                      " is already registered");
        return false;
    }

    transport->set_tdb(tdb_);
    by_type_[type] = std::move(transport);
    return true;
}

void TransportManager::set_tdb(TDB* tdb) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tdb_ = tdb;
        for (auto& entry : by_type_) {
            entry.second->set_tdb(tdb);
        }
    }

    // Reader-потоки Link должны отдавать встречные кадры в тот же TDB,
    // который принимает обычный входящий трафик.
    if (tdb != nullptr) {
        get_link_manager().set_incoming_sink(
            [tdb](IncomingPacket&& packet) {
                tdb->pass_packet(std::move(packet));
            });
    } else {
        get_link_manager().set_incoming_sink(nullptr);
        get_link_manager().close_all();
    }
}

bool TransportManager::start_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : by_type_) {
        if (!entry.second->start()) {
            METALOG_ERROR("TransportManager: failed to start transport " +
                          std::to_string(static_cast<int>(entry.first)));
            return false;
        }
    }
    return true;
}

void TransportManager::stop_all() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : by_type_) {
            entry.second->stop();
        }
    }

    // Сначала останавливаем Link reader'ов, чтобы после shutdown TDB
    // ни один поток не пытался положить пакет в уже уничтоженный TDB.
    get_link_manager().set_incoming_sink(nullptr);
    get_link_manager().close_all();
}

int TransportManager::send_request(const std::string& module,
                                   const std::string& method,
                                   const std::string& data,
                                   uint32_t packet_id) {
    return send_packet(module, method, data, packet_id, false);
}

int TransportManager::send_async(const std::string& module,
                                 const std::string& method,
                                 const std::string& data) {
    return send_packet(module, method, data, 0, true);
}

int TransportManager::send_packet(const std::string& module,
                                  const std::string& method,
                                  const std::string& data,
                                  uint32_t packet_id,
                                  bool async) {
    if (module.empty()) {
        METALOG_ERROR("TransportManager: request has no module");
        return -1;
    }

    gwp::GWPPacket packet;
    packet.set_type(async ? gwp::PacketType::ASYNC_DATA
                          : gwp::PacketType::REQUEST);
    packet.set_id(packet_id != 0
                      ? packet_id
                      : next_packet_id_.fetch_add(1,
                                                  std::memory_order_relaxed));
    packet.set_method(method);
    packet.set_data(data.empty() ? nullptr : data.data(), data.size());

    const std::vector<uint8_t> frame = packet.make_package();

    const bool priority = get_connection_cache().is_priority(module);

    // Если модуль уже соединён с нами в нужном режиме, пишем по тому же
    // fd. Для priority-запроса неподходящее соединение не переиспользуем:
    // handshake отправляется только при открытии, поэтому режим нужно
    // сообщить новым соединением.
    auto inbound = get_connection_cache().find(module);
    if (inbound && inbound->priority() == priority) {
        const int reused = inbound->send(frame);
        if (reused >= 0) return reused;
        get_connection_cache().remove_inbound(inbound);
    }

    endpoint ep;
    if (!resolve_endpoint(module, ep)) {
        METALOG_ERROR("TransportManager: failed to resolve module: " + module);
        return -1;
    }

    get_link_manager().set_priority(ep, priority);

    return send_to(module, ep, frame);
}

int TransportManager::send_outgoing(OutgoingPacket&& packet) {
    if (packet.module.empty()) {
        METALOG_ERROR("TransportManager: outgoing packet has no module");
        return -1;
    }
    if (packet.data.empty()) {
        METALOG_ERROR("TransportManager: outgoing packet is empty");
        return -1;
    }

    // Ответ на входящий запрос идёт по уже открытому соединению.
    const int reused =
        get_connection_cache().send(packet.module, packet.data);
    if (reused >= 0) return reused;

    endpoint ep;
    if (!resolve_endpoint(packet.module, ep)) {
        METALOG_ERROR("TransportManager: failed to resolve module: " +
                      packet.module);
        return -1;
    }

    const bool priority =
        packet.priority || get_connection_cache().is_priority(packet.module);
    get_link_manager().set_priority(ep, priority);

    return send_to(packet.module, ep, packet.data);
}

Transport* TransportManager::get(connection_type type) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = by_type_.find(type);
    return it == by_type_.end() ? nullptr : it->second.get();
}

size_t TransportManager::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return by_type_.size();
}

bool TransportManager::resolve_endpoint(const std::string& module,
                                        endpoint& ep) {
    if (get_cache().get(module, ep)) return true;

    // В Cache нет endpoint'а — спрашиваем ядро. Пока server_api::locate
    // возвращает локальный путь, но контракт уже правильный: ядро знает,
    // где живёт модуль, а результат кладётся в Cache как обычно.
    METALOG_INFO("TransportManager: cache miss for '" + module +
                 "', asking core");
    const std::string address = server_api::locate(module);
    if (address.empty()) {
        METALOG_ERROR("TransportManager: core did not locate module: " + module);
        return false;
    }

    save_endpoint_to_cache(module, address);
    return get_cache().get(module, ep);
}

int TransportManager::send_to(const std::string& module, const endpoint& ep,
                              const std::vector<uint8_t>& frame) {
    Transport* transport = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = by_type_.find(ep.type);
        if (it == by_type_.end()) {
            METALOG_ERROR("TransportManager: no transport for type " +
                          std::to_string(static_cast<int>(ep.type)));
            return -2;
        }
        transport = it->second.get();
    }

    return transport->send_to(module, ep, frame);
}
