#include "tdb.hpp"

#include "../../../metalogger/metalogger.h"

#include <utility>

TDB::TDB() = default;

TDB::~TDB() {
    shutdown();
}

bool TDB::init(size_t workers, size_t senders, size_t queue_capacity,
               std::unique_ptr<DistributionStrategy> strategy,
               size_t max_direct_workers) {
    shutdown();

    if (workers == 0 || senders == 0) {
        METALOG_ERROR("TDB: workers and senders must be > 0");
        return false;
    }

    auto uplink = std::make_unique<UPLINK>();
    if (!uplink->init(workers, queue_capacity, std::move(strategy),
                      max_direct_workers)) {
        METALOG_ERROR("TDB: failed to init uplink");
        return false;
    }

    auto outlink = std::make_unique<OUTLINK>();
    if (!outlink->init(senders, queue_capacity)) {
        METALOG_ERROR("TDB: failed to init outlink");
        uplink->shutdown();
        return false;
    }

    uplink_ = std::move(uplink);
    outlink_ = std::move(outlink);

    // Воркеры отправляют результаты через OUTLINK этого же TDB.
    uplink_->set_outgoing_sink([this](OutgoingPacket&& op) {
        return submit_outgoing(std::move(op));
    });

    return true;
}

void TDB::shutdown() {
    // Сначала останавливаем обработку, потом даём outlink отправить остаток.
    uplink_.reset();
    outlink_.reset();
}

void TDB::pass_packet(IncomingPacket&& ip) {
    if (!uplink_) {
        METALOG_WARNING("TDB: pass_packet before init, packet dropped");
        return;
    }
    uplink_->push(std::move(ip));
}

bool TDB::submit_outgoing(OutgoingPacket&& op) {
    if (!outlink_) {
        METALOG_WARNING("TDB: submit_outgoing before init, packet dropped");
        return false;
    }
    return outlink_->push(std::move(op));
}

void TDB::set_sender(SendCallback callback) {
    if (!outlink_) {
        METALOG_WARNING("TDB: set_sender before init");
        return;
    }
    outlink_->set_sender(std::move(callback));
}

void TDB::set_handler(PacketHandler handler) {
    if (!uplink_) {
        METALOG_WARNING("TDB: set_handler before init");
        return;
    }
    uplink_->set_handler(std::move(handler));
}

size_t TDB::processed_count() const {
    return uplink_ ? uplink_->processed_count() : 0;
}

size_t TDB::invalid_count() const {
    return uplink_ ? uplink_->invalid_count() : 0;
}

size_t TDB::dropped_count() const {
    if (!uplink_) return 0;
    return uplink_->dropped_count();
}
