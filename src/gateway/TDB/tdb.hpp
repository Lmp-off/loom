#pragma once
#include "uplink/uplink.hpp"

class TDB {
public:
    TDB();
    ~TDB();
    
    bool init(size_t workers, size_t senders);
    void shutdown();
    
    UPLINK* uplink() { return uplink_.get(); }
    UPLINK* outlink() { return outlink_.get(); }
    
private:
    std::unique_ptr<UPLINK> uplink_;
    std::unique_ptr<UPLINK> outlink_;
};