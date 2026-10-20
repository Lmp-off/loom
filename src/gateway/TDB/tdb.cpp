#include "tdb.hpp"

TDB::TDB() = default;

TDB::~TDB() {
    shutdown();
}

bool TDB::init(size_t workers, size_t senders) {
    uplink_ = std::make_unique<UPLINK>();
    if (!uplink_->init(workers, nullptr)) return false;

    outlink_ = std::make_unique<UPLINK>();
    if (!outlink_->init(senders, nullptr)) return false;

    return true;
}

void TDB::shutdown() {
    if (uplink_)  uplink_->shutdown();
    if (outlink_) outlink_->shutdown();
    uplink_.reset();
    outlink_.reset();
}

void TDB::pass_packet(IncomingPacket&& ip) {
    uplink_->push(std::move(ip));
}