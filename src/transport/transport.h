#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../discovery/cache.h"

class TDB;

// Transport — контракт транспорта.
//
// Архитектурные инварианты:
// - транспорт не парсит GWP и не знает про Cache;
// - входящие кадры уходят в TDB через set_tdb();
// - исходящий кадр уже сериализован, транспорт только доставляет его по endpoint;
// - резолвинг module -> endpoint и выбор транспорта — задача TransportManager;
// - UDS-транспорт переиспользует соединения через LinkManager и ConnectionCache.

class Transport {
public:
    virtual ~Transport() = default;

    virtual bool start() = 0;
    virtual void stop() = 0;

    virtual int send(const endpoint& ep, const std::vector<uint8_t>& frame) = 0;

    virtual int send_to(const std::string& module, const endpoint& ep,
                        const std::vector<uint8_t>& frame) {
        (void)module;
        return send(ep, frame);
    }

    virtual connection_type type() const = 0;
    virtual int get_listen_fd() const = 0;

    void set_tdb(TDB* tdb) { tdb_ = tdb; }

protected:
    TDB* tdb_ = nullptr;
};
