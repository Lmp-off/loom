#include "starter.hpp"
#include "TDB/tdb.hpp"

uint8_t start_default_network_gate(){
    static TDB tdb;
    if (!tdb.init(1, 1)) return 1;
    return 0;
}