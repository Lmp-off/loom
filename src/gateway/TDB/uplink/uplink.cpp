#include "uplink.hpp"
#include "../../../../metalogger/metalogger.h"

UPLINK::UPLINK() : strategy_(nullptr) {}

UPLINK::~UPLINK() {
    shutdown();
}

bool UPLINK::init(size_t workers, DistributionStrategy* strategy) {
    if (!buffer_.init(1024)) {
        METALOG_ERROR("Failed to init buffer");
        return false;
    }
    
    strategy_ = strategy;
    running_ = true;
    
    for (size_t i = 0; i < workers; ++i) {
        auto worker = std::make_unique<Worker>(i, &buffer_, strategy_);
        if (!worker->start()) {
            METALOG_ERROR("Failed to start worker " + std::to_string(i));
            return false;
        }
        workers_.push_back(std::move(worker));
    }
    
    METALOG_INFO("UPLINK initialized with " + std::to_string(workers) + " workers");
    return true;
}

void UPLINK::shutdown() {
    running_ = false;
    
    for (auto& worker : workers_) {
        worker->stop();
    }
    workers_.clear();
    
    buffer_.shutdown();
    METALOG_INFO("UPLINK shutdown");
}

void UPLINK::push(IncomingPacket&& packet) {
    if (!buffer_.push(std::move(packet))) {
        METALOG_WARNING("Buffer full, packet dropped");
    }
}

size_t UPLINK::queue_size() const {
    return buffer_.size();
}

size_t UPLINK::workers_count() const {
    return workers_.size();
}